# shared_queue
对高性能的基于共享内存的queue探索,支持pub sub
# 特性
* 采用环形缓冲区(可配置大小)
* 支持多消费者单生产者
* 消费者的智能管理
* 消费者无新消息自动阻塞
