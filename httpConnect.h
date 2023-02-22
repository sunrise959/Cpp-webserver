#ifndef HTTPCONNECT_H
#define HTTPCONNECT_H

#include <sys/epoll.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <errno.h>
#include <stdarg.h>
#include <sys/uio.h>
#include "locker.h"


class httpConnect{
    public:
        
        static int m_epollfd; // 所有的socket注册在1个epoll实例中
        static int userCnt;

        httpConnect(){};

        ~httpConnect(){};

        void init(int sockfd, const sockaddr_in &addr); // 初始新连接

        void closeConnect();

        bool read(); //非阻塞读数据

        bool write(); // 非阻塞写数据

        void process(); // 处理client请求
    private:
        int m_socketfd; // 该HTTP连接的socket
        struct sockaddr_in m_address; // 通信的socket地址
};

#endif