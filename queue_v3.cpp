#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <string>
#include <sys/stat.h>
#include <fcntl.h>
#include <semaphore.h>
#include <inttypes.h>
#include <string.h>

class TC {
public:
	TC(const std::string &path) {
		buffer_size_ = 1024;
		shm_path_ = path;
		read_buffer_ = new char[buffer_size_];
	};

	bool Create(size_t size) {
		shm_size_ = size;
		shm_fd_ = shm_open(shm_path_.c_str(), O_CREAT | O_RDWR | O_TRUNC, 0777);
        if (shm_fd_ < 0)
        {
            printf("shm_open  failed\n");
            return false;
        }
		uint64_t real_size = shm_size_ + sizeof(SHM_Data);
        if (ftruncate(shm_fd_, real_size) == -1)
        {
            printf("ftruncate  failed\n");
            return false;
        }
        shm_data_ = (SHM_Data *)mmap(NULL, real_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd_, 0);
        if (shm_data_ == MAP_FAILED)
        {
            printf("mmap  failed\n");
            return false;
        }
        pthread_condattr_t attrcond;
        if (pthread_condattr_init(&attrcond) != 0)
        {
            printf("pthread_condattr_init  failed\n");
            return false;
        }
        if (pthread_condattr_setpshared(&attrcond, PTHREAD_PROCESS_SHARED) != 0)
        {
            printf("pthread_condattr_setpshared  failed\n");
            return false;
        }
        if (pthread_cond_init(&shm_data_->cond, &attrcond) != 0)
        {
            printf("pthread_cond_init  failed\n");
            return false;
        }
        pthread_mutexattr_t attrmutex;
        if (pthread_mutexattr_init(&attrmutex) != 0)
        {
            printf("pthread_mutexattr_init  failed\n");
            return false;
        }
        if (pthread_mutexattr_setpshared(&attrmutex, PTHREAD_PROCESS_SHARED) != 0)
        {
            printf("pthread_mutexattr_setpshared  failed\n");
            return false;
        }
        if (pthread_mutexattr_setrobust_np(&attrmutex, PTHREAD_MUTEX_ROBUST_NP) != 0)
        {
            printf("pthread_mutexattr_setrobust  failed\n");
            return false;
        }
        if (pthread_mutex_init(&shm_data_->mutex, &attrmutex) != 0)
        {
            printf("pthread_mutex_init  failed\n");
            return false;
        }
        pthread_t pthreadId_;
        if (pthread_create(&pthreadId_, NULL, wake_up_worker, static_cast<void *>(this)) != 0)
        {
            printf("pthread_create  failed\n");
            return false;
        }
        pthread_detach(pthreadId_);
        if (sem_init(&wakeup_, 0, 0) != 0)
        {
            printf("sem_init  failed\n");
            return false;
        }
        shm_data_->total = 0;
        shm_data_->wait = 0;
        shm_data_->size = shm_size_;
        printf("shm create success\n");
        return true;
	};

	bool Open() {
		shm_fd_ = shm_open(shm_path_.c_str(), O_RDWR, 0777);
        if (shm_fd_ < 0)
        {
            printf("shm_open  failed\n");
            return false;
        }
		SHM_Data *shm_info_ = (SHM_Data *)mmap(NULL, sizeof(SHM_Data), PROT_READ|PROT_WRITE, MAP_SHARED, shm_fd_, 0);
		if (shm_info_ == MAP_FAILED)
		{
			printf("mmap info failed\n");
			return false;
		}
		// 重新挂载大小
		shm_size_ = shm_info_->size;
		uint64_t real_size = shm_info_->size + sizeof(SHM_Data);

		shm_data_ = (SHM_Data *)mremap(shm_info_, sizeof(SHM_Data), real_size, MREMAP_MAYMOVE);
		if (shm_data_ == MAP_FAILED)
		{
			printf("mmap data failed\n");
			return false;
		}
	};
	void Write(const char *buffer, size_t size) {
		if (write_index_ + size < shm_size_) {
			memcpy(shm_data_->buffer + write_index_, buffer, size);
			write_index_ += size;
		} else {
     		int64_t write_tail = shm_size_ - write_index_;
			int64_t write_head = size - write_tail;
			memcpy(shm_data_->buffer + write_index_, buffer, write_tail);
			memcpy(shm_data_->buffer, buffer + write_tail, write_head);
			write_index_ = write_head;
		}
		// 写入 store 屏障 确保消费者察觉到计数后 数据已经写入到内存
		asm volatile("sfence" ::: "memory");
		shm_data_->total++;
		//__atomic_store_n(&shm_data_->total, shm_data_->total + 1, __ATOMIC_RELEASE);
		// 查看是否有消费上报等待状态
		asm volatile("lfence" ::: "memory");
		if (shm_data_->wait == 0) {
			return;
		}
		sem_post(&wakeup_);
		//int state = __atomic_load_n(&shm_data_->state, __ATOMIC_ACQUIRE);
	};
	static void *wake_up_worker(void *obj) {
		TC *self = static_cast<TC *>(obj);
        self->WakeUp();
	};

	void WakeUp() {
		while (true) {
			sem_wait(&wakeup_);
			//printf("wakeup total=%ld\n", shm_data_->total);
			//__atomic_store_n(&shm_data_->state, 0, __ATOMIC_RELEASE);
			if (pthread_mutex_lock(&shm_data_->mutex) == EOWNERDEAD)
            {
                printf("pthread_mutex_consistent_np\n");
                pthread_mutex_consistent_np(&shm_data_->mutex);
            }
            shm_data_->wait = 0;
            pthread_cond_broadcast(&shm_data_->cond);
            pthread_mutex_unlock(&shm_data_->mutex);
        }
	};
	void Debug() {
		printf("\n=========dump==========\n");
		for(int i = 0; i < shm_size_; i++) {
			if (i != 0 && i % 10 == 0) printf("\n");
			printf("%c", shm_data_->buffer[i]);
		}
		printf("\n=============\n");
	};
	void Tail(bool spin) {
		int64_t total = 0;
		while (true) {
			// 写入 load 屏障 确保消费者感知到计数变化后 数据已经写入
			asm volatile("lfence" ::: "memory");
			//total = __atomic_load_n(&shm_data_->total, __ATOMIC_ACQUIRE);
			total = shm_data_->total;
			struct timeval tv;
						gettimeofday(&tv, NULL);
			while (read_total_ < total) {
				for (int i = 0; i < buffer_size_; i++)
				{
					if (read_index_ >= shm_size_)
					{
						read_index_ = 0;
					}
					read_buffer_[i] = shm_data_->buffer[read_index_++];
					if (read_buffer_[i] == '@')
					{
						read_buffer_[i] = '\0';
						
						int ms = atoi(read_buffer_);
						int d =  tv.tv_usec - ms;
						printf("%ld, %ld, %d\n", total,  read_total_, d);
						break;
					}
				}
				read_total_++;
			}
			if (spin) {
				continue;
			}
			pthread_mutex_lock(&shm_data_->mutex);
			// asm volatile("lfence" ::: "memory");
			//total = __atomic_load_n(&shm_data_->total, __ATOMIC_ACQUIRE);
			total = shm_data_->total;
			shm_data_->wait = 1;
			//__atomic_store_n(&shm_data_->state, 1, __ATOMIC_RELEASE);
			pthread_cond_wait(&shm_data_->cond, &shm_data_->mutex);
			pthread_mutex_unlock(&shm_data_->mutex);
		};
	}
private:
	struct SHM_Data
	{
		uint64_t total; // 记录消息总数
	    uint64_t size;           // 环形缓冲区大小
	    int wait;
	    pthread_cond_t cond;
        pthread_mutex_t mutex;
	    char buffer[]; // 环形缓冲区
	};
	sem_t wakeup_;
	char *read_buffer_ = nullptr;
	SHM_Data *shm_data_ = nullptr;

    int shm_fd_;

    std::string shm_path_;
    uint64_t write_index_ = 0;

    uint64_t read_total_ = 0;
    uint64_t read_index_ = 0;

    uint64_t shm_size_ = 0;
    uint64_t buffer_size_ = 0;
};
int main(int argc, char *argv[]) {
	TC *tc = new TC("/test");
	if (strcmp(argv[1], "p") == 0) {
		tc->Create(1024 * 1024 * 10); // 1m
		char buffer[12] = {0};
		sleep(10);
		int counter  = 0;		
		while (true) {
			struct timeval tv;
			gettimeofday(&tv, NULL);
			sprintf(buffer, "%ld@", tv.tv_usec);
			tc->Write(buffer, strlen(buffer));
			// tc->Debug();
			if (counter++ > 10) {
				sleep(2);
				counter = 0;
			}
			//sleep(1);
			//printf("write %s", buffer);
		}
	}
	if (strcmp(argv[1], "c") == 0) {
		tc->Open();
		tc->Tail(strcmp(argv[2], "s") == 0);
	}
}
