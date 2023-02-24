/*
    模拟practor模式实现服务器对http请求的处理
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <error.h>
#include <sys/epoll.h>
#include <signal.h>
#include "locker.h"
#include "threadPool.h"
#include "httpConnect.h"

#define MAX_CONN 65535 // 最大连接数
#define MAX_EVENT 10000 // 最大监听事件数量
// 信号捕捉
void addsig(int sig, void(*handler)(int)){
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handler;
    sigfillset(&sa.sa_mask);
    sigaction(sig, &sa, NULL);
}

// 添加文件描述符至epoll
extern void addfd(int epollf, int fd, bool oneshot);
// 从epoll删除文件描述符  
extern void removefd(int epollfd, int fd);
// 修改文件描述符
extern void modfd(int epollfd, int fd, int event);
// 设置文件描述符非阻塞
extern void setNonblock(int fd);

int main(int argc, char* argv[]){// argc: 参数个数 argv[]: 存储各个参数
    if(argc <= 1){
        printf("Please input in the following format: %s port number.\n", argv[0]);
        exit(-1);
    }

    // 获取端口号
    int port = atoi(argv[1]);

    // 处理SIGPIPE信号
    addsig(SIGPIPE, SIG_IGN);

    // 线程池，任务类型HTTP通信
    threadPool<httpConnect>* pool = NULL;
    try{
        pool = new threadPool<httpConnect>;
    }catch(...){// 接收所有异常
        exit(-1);
    }

    // http数组记录客户端信息
    httpConnect * clients = new httpConnect[MAX_CONN];

    // 套接字通信
    int listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if(listenfd == -1){
        perror("socket");
        exit(-1);
    }
    // 端口复用
    int optval = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = INADDR_ANY;
    int ret = bind(listenfd, (struct sockaddr*) &address, sizeof(address));
    if(ret == -1){
        perror("bind");
        exit(-1);
    }
    listen(listenfd, 8);
    
    // epoll实例，监听文件描述符
    struct epoll_event events[MAX_EVENT];// 文件描述符数组
    int epollfd = epoll_create(1);

    // 将监听的文件描述符添加到epoll
    struct epoll_event event;
    setNonblock(listenfd);
    event.data.fd = listenfd;
    event.events =  EPOLLIN | EPOLLRDHUP;//EPOLLRDHUP事件判断client断开连接
    epoll_ctl(epollfd, EPOLL_CTL_ADD, listenfd, &event);
    httpConnect::m_epollfd = epollfd;

    while(1){
        int num = epoll_wait(epollfd, events, MAX_EVENT, -1);
        if(num < 0 && errno != EINTR){
            perror("epoll_wait");
            break;
        }

        // 处理事件
        for(int i = 0; i < num; i++){
            int sockfd = events[i].data.fd;
            if(sockfd == listenfd){ // 新连接
                struct sockaddr_in clientAddr;
                socklen_t len = sizeof(clientAddr);
                int connectfd = accept(listenfd, (sockaddr*) &clientAddr, &len);
                if(connectfd == -1){
                    continue;
                }
                if(httpConnect::userCnt >= MAX_CONN){
                    // 连接达到上限
                    // 给客户端写：服务器正忙
                    close(connectfd);
                    continue;
                }
                // 客户数据初始化
                clients[connectfd].init(connectfd, clientAddr);
            }else if(events[i].events & (EPOLLERR | EPOLLRDHUP | EPOLLHUP)){
                // 客户端异常或断开连接
                clients[sockfd].closeConnect();
            }else if(events[i].events & EPOLLIN){ // 读事件就绪
                if(clients[sockfd].read()){
                    // 1次读完数据
                    pool->append(&clients[sockfd]);
                }else{ // 读失败
                    clients[sockfd].closeConnect();
                }
            }else if(events[i].events & EPOLLOUT){ //写事件就绪
                if(!clients[sockfd].write()){ 
                    // 写数据失败
                    clients[sockfd].closeConnect();
                }
            }
        }
    }
    close(epollfd);
    close(listenfd);
    delete pool;
    delete [] clients;

    return 0;
}