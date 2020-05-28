#pragma once

#include <cstring>
#include <cinttypes>
#include <sys/shm.h>
#include <semaphore.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/time.h>
#include <map>
#include <thread>
#include <mutex>
#include <iostream>
#include <functional>

class SharedQueue
{

public:
    SharedQueue(uint64_t buffer_size) : buffer_size_(buffer_size), shm_data_(nullptr), current_read_(0)
    {
        start_ = true;
        page_size_ = sysconf(_SC_PAGESIZE);
        data_size_ = (sizeof(SHM_Data) + sizeof(char) * buffer_size_ + page_size_ - 1) & (~(page_size_ - 1));
        line_buffer_size_ = 10240;
        current_offset_ = 0;
        index_ = 0;
        id_ = 0;
        key_t key = (key_t)666;
        int shmid = shmget(key, data_size_, 0644 | IPC_CREAT);
        shm_data_ = (SHM_Data *)shmat(shmid, NULL, 0);
        lock_ = sem_open("lock1", O_CREAT | O_RDWR, 0777, 1);
        lock_sems_ = sem_open("lock2", O_CREAT | O_RDWR, 0777, 0);
    }
    // 以生产者身份初始化
    void InitAsProducer()
    {
        mode_ = 0;
        memset(shm_data_, 0, data_size_);
        // 管理消费者信号量的注册释放
        std::function<void()> register_conumser = [=]() {
            //std::cout << "wait reg sem thread start\n";
            while (start_)
            {
                sem_wait(lock_sems_);
                sem_wait(lock_);
                // add consumer
                //std::cout << " reg sem thread wakeup\n";
                // 注册信号量
                if (shm_data_->op == 1)
                {
                    std::lock_guard<std::mutex> guard(mtx_);
                    std::string key = get_sem_name(shm_data_->max_id);
                    sems_[key] = sem_open(key.c_str(), O_CREAT | O_RDWR, 0777, 0);
                    //std::cout << "reg sem sucess key=" << key << "\n";
                }
                // remove consumer
                // 删除信号量
                if (shm_data_->op == 2)
                {
                    std::lock_guard<std::mutex> guard(mtx_);
                    int target_id = shm_data_->args;
                    std::string key = get_sem_name(target_id);
                    sems_.erase(key);
                    //std::cout << "del sem sucess key=" << key << ",size=" << sems_.size() << "\n";
                }
                sem_post(lock_);
            }
        };
        register_thread_ = new std::thread(register_conumser);
        //std::cout << "InitAsProducer\n";
    }
    // 以消费者身份初始化
    void InitAsConsumer()
    {
        mode_ = 1;
        sem_wait(lock_);
        shm_data_->op = 1;
        shm_data_->max_id++;
        id_ = shm_data_->max_id;
        sem_post(lock_);
        //std::cout << "InitAsConsumer alloc id\n";
        sem_post(lock_sems_); // 通知注册信号量
        //std::cout << "InitAsConsumer wakeup thread\n";
        consumer_lock_ = sem_open(get_sem_name(id_).c_str(), O_CREAT | O_RDWR, 0777, 0);
        line_buffer_ = new char[line_buffer_size_];
        //std::cout << "InitAsConsumer\n";
        return;
    }
    // 清理收场
    void Exit()
    {
        if (mode_ == 0 && register_thread_)
        {
            start_ = false;
            register_thread_->join();
            return;
        }
        if (mode_ == 1)
        {
            sem_wait(lock_);
            //std::cout << "Exit start\n";
            shm_data_->op = 2;
            shm_data_->args = id_;
            sem_post(lock_);
            sem_post(lock_sems_); // 通知注册信号量
            return;
        }
    }
    ~SharedQueue()
    {

        delete[] line_buffer_;
        shmdt(shm_data_);
    }
    /**
     *  生产者清零缓冲区 消费者不要使用
     */
    void Reset()
    {
        memset(shm_data_, 0, data_size_);
    }
    /**
     *  共享内存消息总条数
     */
    uint64_t Total()
    {
        return shm_data_->total;
    }
    /**
     *  当前生产者共享内存的写入位置
     */
    uint64_t Offset()
    {
        return shm_data_->offset;
    }
    /**
     *  当前消费者读取消息总条数
     */
    uint64_t Current_Read()
    {
        return current_read_;
    }
    /**
     * 
     *  当前消费者共享内存的读取位置
     */
    uint64_t Current_Offest()
    {
        return current_offset_;
    }
    /**
     * 写入数据
     */
    void Write(const char *str)
    {
        int str_len = strlen(str);
        for (int i = 0; i < str_len; i++)
        {
            shm_data_->buffer[shm_data_->offset] = str[i];
            shm_data_->offset++;
            if (shm_data_->offset >= buffer_size_)
            {
                shm_data_->offset = 0;
            }
        }
        shm_data_->total++;
        if (sems_.empty())
        {
            return;
        }
        std::lock_guard<std::mutex> guard(mtx_);
        for (auto s : sems_)
        {
            sem_post(s.second);
        }
    }
    /**
     *  读取一行 自动阻塞 
     */
    const char *ReadLine()
    {
        if (current_read_ >= shm_data_->total)
        {
            sem_wait(consumer_lock_);
            return NULL;
        }

        while (index_ < line_buffer_size_)
        {
            line_buffer_[index_] = shm_data_->buffer[current_offset_];
            if (line_buffer_[index_] == '\n')
            {
                break;
            }
            index_++;
            current_offset_++;
            if (current_offset_ > line_buffer_size_)
            {
                current_offset_ = 0;
            }
        }
        current_offset_++;
        line_buffer_[index_ + 1] = '\0';
        index_ = 0;
        current_read_++;
        return line_buffer_;
    }

private:
    /**
     * 根据id获取一个有名信号量名称
    */
    std::string get_sem_name(int id)
    {
        char buffer[1024];
        sprintf(buffer, "shared_queue_consumer_lock_%d", id);
        return buffer;
    }

private:
    struct SHM_Data
    {
        uint64_t total; // 记录消息总数
        int offset;     // 记录当前写入缓冲区位置 环形缓冲区
        int max_id;     // 为消费者分配sem
        int op;         // 进程间通信使用
        int args;       // 进程间通信使用
        char buffer[];  // 环形缓冲区
    };
    SHM_Data *shm_data_;                  // 共享内存
    int page_size_;                       // 内存页大小
    int mode_;                            // 标识当前是消费者还是生产者
    int id_;                              // 消费者id
    uint64_t data_size_;                  // 共享内存总大小 主要是清零使用
    uint64_t buffer_size_;                // 行缓冲最大数
    sem_t *lock_;                         // 保护shm_data_
    sem_t *lock_sems_;                    // 唤醒管理消费者线程
    sem_t *consumer_lock_;                // 消费者当前的信号量
    std::map<std::string, sem_t *> sems_; // 关系表 做删除信号量使用
    std::thread *register_thread_;        // 消费者线程
    bool start_;                          // 消费者线程退出标识
    uint64_t current_read_;               // 当前总共读取数目
    uint64_t current_offset_;             // 行缓冲当前读共享内存位置
    uint64_t line_buffer_size_;           // 行缓冲最大值
    uint64_t index_;                      // 行缓冲当前写入位置
    char *line_buffer_;                   // 行缓冲区
    std::mutex mtx_;                      // 保护 关系表
};