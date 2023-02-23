#include "httpConnect.h"

// 静态变量初始化，记录总的连接数
int httpConnect::m_epollfd = -1;
int httpConnect::userCnt = 0;

// 设置文件描述符为非阻塞
void setNoblock(int fd){
    int flag = fcntl(fd, F_GETFL);
    flag  |= O_NONBLOCK;
    fcntl(fd, F_SETFL, flag);
}

// 在epoll中添加需监听的文件描述符
void addfd(int epollfd, int fd, bool oneshot){// 默认LT模式，可改为ET
    struct epoll_event event;
    setNoblock(fd);
    event.data.fd = fd;
    event.events =  EPOLLIN | EPOLLET | EPOLLRDHUP;//EPOLLRDHUP事件判断client断开连接
    if(oneshot){
        event.events |= EPOLLONESHOT; // 设定1个socket同一时间仅由1个线程访问
    }
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
}

// 在epoll中删除文件描述符
void removefd(int epollfd, int fd){
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, NULL);//0
    close(fd);
}

// 在epoll中修改文件描述符，重置EPOLLONESHT事件
void modfd(int epollfd, int fd, int ev){
    struct epoll_event event;
    event.events = ev | EPOLLONESHOT | EPOLLRDHUP;
    event.data.fd = fd;
    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

// 初始化
void httpConnect::init(int sockfd, const sockaddr_in &addr){
    m_socketfd = sockfd;
    m_address = addr;
    // 端口复用
    int optval = 1;
    setsockopt(m_socketfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
    // 添加到epoll实例
    addfd(m_epollfd, m_socketfd, true);
    userCnt++;
}

// 关闭连接
void httpConnect::closeConnect(){
    if(m_socketfd != -1){
        removefd(m_epollfd, m_socketfd);
        m_socketfd = -1;
        userCnt--;
    }
}

// 循环读数据
bool httpConnect::read(){
    if(readIndex >= READ_BUFFER_SIZE){ // 缓冲区已满
        return false;
    }
    int readBytes = 0;
    while(1){
        readBytes = recv(m_socketfd, readBuf + readIndex, READ_BUFFER_SIZE - readIndex, 0);
        if(readBytes == -1){
            // 无数据，读取结束
            if(errno == EAGAIN || errno == EWOULDBLOCK){
                break;
            }
            return false;
        }else if(readBytes == 0){
            // 连接已关闭
            return false;
        }
        readIndex += readBytes; 
    }
    printf("读取数据：%s\n", readBuf);
    return true;
}

// 写数据
bool httpConnect::write(){
    printf("start write.\n");
    return true;
}

// 线程池的业务逻辑，处理HTTP请求
void httpConnect::process(){
    // 解析HTTP请求

    // 生成响应
    printf("成功响应！\n");
}