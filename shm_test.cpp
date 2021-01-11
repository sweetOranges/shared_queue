#include <iostream>
#include <cstring>
#include <errno.h>
#include <sys/time.h>
#include <unistd.h>
#include <fcntl.h>
#include <mqueue.h>

int main(int argc, char *argv[]) {
  if (argv[1][0] == 'c') {
    //mq_unlink("/test");
    // struct mq_attr attr;
    // attr.mq_msgsize = 1024;
    // attr.mq_maxmsg = 5;
    mqd_t mqID = mq_open("/test", O_RDWR | O_CREAT, 0666, NULL);
    if (mqID < 0)
    {
      std::cout<<"open message queue error..."<< strerror(errno) <<"\n";
      return -1;
    }
    struct mq_attr attr;
    mq_getattr(mqID, &attr);
    std::cout << "init success, mqid=" << mqID  << ",mq_flags=" << attr.mq_flags 
    << ",mq_maxmsg=" << attr.mq_maxmsg 
    << ",mq_msgsize=" << attr.mq_msgsize 
    << ",mq_curmsgs=" << attr.mq_curmsgs << "\n";

    char buffer[1024] = {0};
    if (attr.mq_curmsgs > 0) {
      attr.mq_flags = O_NONBLOCK;
      mq_setattr(mqID, &attr, NULL);
      while(mq_receive(mqID, buffer, sizeof(buffer), NULL) > 0) {
        std::cout << "msg expired," << buffer << "\n";
      }
      attr.mq_flags = 0;
      mq_setattr(mqID, &attr, NULL);
    }
    while (true) {
      int flag = mq_receive(mqID, buffer, sizeof(buffer), NULL);
      if (flag < 0)
      {
        std::cout<<"open message queue error..."<< strerror(errno) <<"\n";
        return -1;
      }
      struct timeval tv;
      gettimeofday(&tv, NULL);
      int d = tv.tv_usec - atol(buffer);
      std::cout << flag << "," << buffer << "," << d << "\n";
    }
  }
  if (argv[1][0] == 'p') {
    mqd_t mqID = mq_open("/test", O_RDWR | O_NONBLOCK);
    std::cout << "open " << mqID << "\n";
    char buffer[128] = {0};
    int counter = 0;
    while (true) {
      //if (counter++ > 10) {
        sleep(1);
        //counter = 0;
      //}
      memset(buffer, 0, sizeof(buffer));
      struct timeval tv;
      gettimeofday(&tv, NULL);
      sprintf(buffer, "%d", tv.tv_usec);
      int flag = mq_send(mqID, buffer, sizeof(buffer), 0);
      if (flag == EAGAIN) {
        continue;
      }
      std::cout << buffer << "," << flag << "\n";
    }
  }
}
