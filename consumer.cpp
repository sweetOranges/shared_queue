#include <iostream>
#include "./shared_queue.h"

int main()
{
    uint64_t buffer_size = 6291456;
    SharedQueue shared{buffer_size};
    shared.InitAsConsumer();

    printf("total = %ld\n", shared.Total());
    int i = 0;
    while (i < 5)
    {
        const char *line = shared.ReadLine();
        if (line == NULL) {
            continue;
        }
        struct timeval tv;
        gettimeofday(&tv, NULL);
        int start = atoi(line);
        int end = tv.tv_usec;
        i++;
        printf("read success, total=%ld, offset=%ld, ms=%d\n", shared.Total(), shared.Current_Read(), end - start);
    }
    shared.Exit();
    getchar();
}