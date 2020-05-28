#include <iostream>

#include "./shared_queue.h"

int main()
{
    uint64_t buffer_size = 6291456;
    SharedQueue shared{buffer_size};
    shared.InitAsProducer();

    char line[4096];
    while (true)
    {
        sleep(1);
        struct timeval tv;
        gettimeofday(&tv, NULL);
        sprintf(line, "%ld\n", tv.tv_usec);
        shared.Write(line);
        printf("write success, total=%ld, offset=%ld\n", shared.Total(), shared.Offset());
    }
   
}