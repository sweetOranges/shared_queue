
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/inotify.h>
#include <functional>
#include <unistd.h>
#include <cstring>
#include <string>
#include <sys/mman.h>
#include <sys/time.h>
#include <iostream>
#include <semaphore.h>
 
#include <gflags/gflags.h>
 
DEFINE_int64(shm_size, 6, "shm_size m");
DEFINE_string(inotify_file, "/tmp/writer.txt", "inotify file path");
DEFINE_string(shm_file, "/test", "shm file path");
DEFINE_string(shm_key, "", "shm key");
 
class Producer
{
public:
    Producer(const std::string &inotify_path, const std::string &shm_path) : inotify_path_(inotify_path), shm_path_(shm_path)
    {
        shm_size_ = FLAGS_shm_size * 1024 * 1024; // 1g; // 1g
    }
 
    bool Init(const std::string &key)
    {
        fd_ = open(inotify_path_.c_str(), O_WRONLY | O_APPEND | O_CREAT | O_TRUNC, 0644);
        if (fd_ < 0)
        {
            printf("open path failed\n");
            return false;
        }
        // 打开共享内存
        shm_fd_ = shm_open(shm_path_.c_str(), O_CREAT | O_RDWR | O_TRUNC, 0777);
        if (shm_fd_ < 0)
        {
            printf("shm_open  failed\n");
            return false;
        }
        uint64_t size = shm_size_ + sizeof(SHM_Data);
        if (ftruncate(shm_fd_, size) == -1)
        {
            printf("ftruncate  failed\n");
            return false;
        }
        shm_data_ = (SHM_Data *)mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd_, 0);
        if (shm_data_ == MAP_FAILED)
        {
            printf("mmap  failed\n");
            return false;
        }
        shm_data_->total = 0;
        shm_data_->size = shm_size_;
        memcpy(shm_data_->inotify_name, inotify_path_.c_str(), inotify_path_.size());
        memcpy(shm_data_->key, key.c_str(), key.size());
        return true;
    }
    void Write(const char *line)
    {
        for (int i = 0; line[i] != '\0'; i++)
        {
            if (current_offset_ >= shm_size_)
            {
                current_offset_ = 0;
            }
            shm_data_->buffer[current_offset_++] = line[i];
        }
        if (current_offset_ >= shm_size_)
        {
            current_offset_ = 0;
        }
        shm_data_->buffer[current_offset_++] = '\0';
        shm_data_->total++;
        write(fd_, "8", 1);
private:
    struct SHM_Data
    {
        uint64_t total;         // 记录消息总数
        char inotify_name[512]; // inotify 文件名
        char key[64];           // 当前数据标识
        uint64_t size;          // 环形缓冲区大小
        char buffer[];          // 环形缓冲区
    };
    SHM_Data *shm_data_ = nullptr; // 共享内存
    int fd_;
    int shm_fd_;
    uint64_t shm_size_ = 0;
    uint64_t buffer_size_ = 0;
    uint64_t total_read = 0;
    uint64_t current_offset_ = 0;
    std::string inotify_path_;
    std::string shm_path_;
};

class Consumer
{
 
public:
    Consumer(int tag, const std::string &shm_path)
        : tag_(tag), shm_path_(shm_path)
    {
        line_size_ = 1024;
        inotify_buffer_ = new char[BUF_LEN];
        line_ = new char[line_size_];
    }
    ~Tail()
    {
        delete[] line_;
        delete[] inotify_buffer_;
    }
    bool Init(const std::string& key)
    {
        shm_fd_ = shm_open(shm_path_.c_str(), O_RDWR, 0777);
        if (shm_fd_ < 0)
        {
            printf("shm_open  failed\n");
            return false;
        }
 
        SHM_Data *shm_info_ = (SHM_Data *)mmap(NULL, sizeof(SHM_Data), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd_, 0);
        if (shm_info_ == MAP_FAILED)
        {
            printf("mmap info failed\n");
            return false;
        }
        printf("info size=%ld inotify_name=%s,key=%s\n", shm_info_->size, shm_info_->inotify_name, shm_info_->key);
        if (strcasecmp(shm_info_->key, key.c_str()) != 0)
        {
            printf("key not match \n");
            return false;
        }
 // 开始监听文件变化
        inotify_fd_ = inotify_init();
        if (inotify_fd_ < 0)
        {
            printf("inotify_init  failed\n");
            return false;
        }
 
        shm_size_ = shm_info_->size;
        uint64_t real_size = shm_info_->size + sizeof(SHM_Data);
 
        inotify_add_watch(inotify_fd_, shm_info_->inotify_name, IN_MODIFY | IN_CREATE | IN_DELETE);
        shm_data_ = (SHM_Data *)mremap(shm_info_, sizeof(SHM_Data), real_size, MREMAP_MAYMOVE);
        if (shm_data_ == MAP_FAILED)
        {
            printf("mmap data failed\n");
            return false;
        }
        return true;
    }
    void Loop()
    {
        while (true)
        {
            while (total_read < shm_data_->total)
            {
                for (int i = 0; i < line_size_; i++)
                {
                    if (current_offset_ >= shm_size_)
                    {
                        current_offset_ = 0;
                    }
                    line_[i] = shm_data_->buffer[current_offset_++];
                    if (line_[i] == '\0')
                    {
                        break;
                    }
                }
                total_read++;
                printf("current_offset=%d, total=%d, read=%d, %s", current_offset_, shm_data_->total, total_read, line_);
            }
            if (!FLAGS_shm_nowait){
                read(inotify_fd_, inotify_buffer_, BUF_LEN);
            }
        }
    }
 
private:
    struct SHM_Data
    {
        uint64_t total;         // 记录消息总数
        char inotify_name[512]; // inotify 文件名
        char key[64];           // 当前数据标识
        uint64_t size;          // 环形缓冲区大小
        char buffer[];          // 环形缓冲区
    };
    SHM_Data *shm_data_ = nullptr; // 共享对象指针
    uint64_t shm_size_ = 0;        // 共享内存大小
    uint64_t line_size_ = 0;       // 每条数据最大值
    uint64_t total_read = 0;       // 当前读取总记录数
    uint64_t current_offset_ = 0;  // 当前读取的偏移量
 
    std::string shm_path_;
 
    int inotify_fd_;
    int shm_fd_;
    int tag_;
 
    char *line_;
    char *inotify_buffer_;
};


int main(int argc, char *argv[])
{
 
    gflags::ParseCommandLineFlags(&argc, &argv, true);
 
    Producer producer(FLAGS_inotify_file, FLAGS_shm_file);
    Producer.Init(FLAGS_shm_key);
}
