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
#include <string.h>
#include "locker.h"

#define READ_BUFFER_SIZE 4096
#define WRITE_BUFFER_SIZE 4096
#define FILENAME_LEN 200

class httpConnect{
    public:
    // HTTP请求方法，支持GET
    enum METHOD {GET = 0, POST, HEAD, PUT, DELETE, TRACE, OPTIONS, CONNECT};
    
    /*
        解析客户端请求时，主状态机的状态
        CHECK_STATE_REQUESTLINE:当前正在分析请求行
        CHECK_STATE_HEADER:当前正在分析头部字段
        CHECK_STATE_CONTENT:当前正在解析请求体
    */
    enum CHECK_STATE { CHECK_STATE_REQUESTLINE = 0, CHECK_STATE_HEADER, CHECK_STATE_CONTENT };
    
    // 从状态机的三种可能状态，即行的读取状态，分别表示
    // 1.读取到一个完整的行 2.行出错 3.行数据尚且不完整
    enum LINE_STATUS { LINE_OK = 0, LINE_BAD, LINE_OPEN };

    /*
        服务器处理HTTP请求的可能结果，报文解析的结果
        NO_REQUEST          :   请求不完整，需要继续读取客户数据
        GET_REQUEST         :   表示获得了一个完整的客户请求
        BAD_REQUEST         :   表示客户请求语法错误
        NO_RESOURCE         :   表示服务器没有资源
        FORBIDDEN_REQUEST   :   表示客户对资源没有足够的访问权限
        FILE_REQUEST        :   文件请求,获取文件成功
        INTERNAL_ERROR      :   表示服务器内部错误
        CLOSED_CONNECTION   :   表示客户端已经关闭连接了
    */
    enum HTTP_CODE { NO_REQUEST = 0, GET_REQUEST, BAD_REQUEST, NO_RESOURCE, FORBIDDEN_REQUEST, FILE_REQUEST, INTERNAL_ERROR, CLOSED_CONNECTION };
    
    public:
        
        static int m_epollfd;                   // 所有的socket注册在1个epoll实例中
        static int userCnt;

        httpConnect(){};

        ~httpConnect(){};

        void init(int sockfd, const sockaddr_in &addr); // 初始新连接

        void closeConnect();

        bool read();                            //非阻塞读数据

        bool write();                           // 非阻塞写数据

        void process();                         // 处理client请求

        HTTP_CODE process_read();               // 解析HTTP请求

        HTTP_CODE parse_requsetLine(char* data); // 解析HTTP请求首行

        HTTP_CODE parse_header(char* data);     // 解析HTTP请求头

        HTTP_CODE parse_content(char* data);    // 解析HTTP请求数据

        LINE_STATUS parse_line();               // 解析某一行

        HTTP_CODE solve_request();               // 处理请求

        bool process_write(HTTP_CODE ret);

        inline char* getline(){return readBuf + lineIndex;}

        // 这一组函数被process_write调用以填充HTTP应答报文
        void unmap();
        bool add_response( const char* format, ... );
        bool add_content( const char* content );
        bool add_content_type();
        bool add_status_line( int status, const char* title );
        void add_headers( int content_length );
        bool add_content_length( int content_length );
        bool add_state();
        bool add_blank_line();

    private:
        void init();                            // 初始化http解析的状态

        int m_socketfd;                         // 该HTTP连接的socket
        struct sockaddr_in m_address;           // 通信的socket地址

        char readBuf[READ_BUFFER_SIZE];         // 读缓冲区
        int readIndex;                          // 读指针，指向已读数据的下一个字节
        int lineIndex;                          // 当前解析行在读缓冲的起始位置
        int checkIndex;                         // 当前解析字符在读缓冲中的位置
        CHECK_STATE checkState;                 // 主状态机状态
        METHOD requestMethod;                   // 请求方法
        char* url;                              // 请求的目标文件
        char* httpVersion;                      // http协议版本
        char* host;                             // 主机名
        bool connectState;                      // 是否保持连接
        int contentLength;                      // 请求体长度

        const char* rootDirectory = "/home/yjy/linux/webserver/resources"; // 网站根目录
        char targetFile[FILENAME_LEN];          // 目标文件名称，响应体
        struct stat targetFileStat;             // 目标文件的状态
        char* targetFileAddress;                // 客户请求的目标文件被映射到内存中的起始位置

        char writeBuf[WRITE_BUFFER_SIZE];       // 写缓冲区:响应首行和响应头
        int writeIndex;                         // 写缓冲区中待发送的字节数
        struct iovec m_iv[2];                   // 采用writev来执行写操作
        int m_iv_count;                         // 2块内存，写缓冲区和targetFileAddress
        int bytes_have_send;                    // 已经发送的字节
        int bytes_to_send;                      // 还需要发送的字节
};

#endif

