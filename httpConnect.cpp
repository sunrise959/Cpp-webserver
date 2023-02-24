#include "httpConnect.h"

// 定义HTTP响应的一些状态信息
const char* ok_200_title = "OK";
const char* error_400_title = "Bad Request";
const char* error_400_form = "Your request has bad syntax or is inherently impossible to satisfy.\n";
const char* error_403_title = "Forbidden";
const char* error_403_form = "You do not have permission to get file from this server.\n";
const char* error_404_title = "Not Found";
const char* error_404_form = "The requested file was not found on this server.\n";
const char* error_500_title = "Internal Error";
const char* error_500_form = "There was an unusual problem serving the requested file.\n";


// 静态变量初始化，记录总的连接数
int httpConnect::m_epollfd = -1;
int httpConnect::userCnt = 0;

// 设置文件描述符为非阻塞
void setNonblock(int fd){
    int flag = fcntl(fd, F_GETFL);
    flag  |= O_NONBLOCK;
    fcntl(fd, F_SETFL, flag);
}

// 在epoll中添加需监听的文件描述符
void addfd(int epollfd, int fd, bool oneshot){// 默认LT模式，可改为ET
    struct epoll_event event;
    setNonblock(fd);
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
    init();
}

// 初始化http解析的状态
void httpConnect::init(){
    memset(readBuf, 0, sizeof(readBuf));
    memset(writeBuf, 0, sizeof(writeBuf));
    memset(targetFile, 0, sizeof(targetFile));
    bytes_have_send = 0;
    bytes_to_send = 0;
    checkState = CHECK_STATE_REQUESTLINE; 
    readIndex = 0; // 相当于char* = NULL 
    lineIndex = 0;
    checkIndex = 0;
    writeIndex = 0;
    requestMethod = GET;
    url = 0;
    httpVersion = 0;
    host = 0;
    connectState = false;
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
    return true;
}

// 解析HTTP请求，主状态机
httpConnect::HTTP_CODE httpConnect::process_read(){
    LINE_STATUS lineStatus = LINE_OK;
    HTTP_CODE res = NO_REQUEST;

    char* data = 0;
    // 循环解析
    while((checkState == CHECK_STATE_CONTENT && lineStatus == LINE_OK)||
      (lineStatus == parse_line() &&  lineStatus == LINE_OK)){
        // 正在解析请求体或解析了一行完整数据
        // 读取数据
        data  = getline();
        lineIndex = checkIndex;
        //printf( "got 1 http line: %s\n", text );
        switch(checkState){
            case CHECK_STATE_REQUESTLINE:{
                res = parse_requsetLine(data); // 解析首行
                if(res == BAD_REQUEST){
                    return BAD_REQUEST;
                }
                break;
            }

            case CHECK_STATE_HEADER:{
                res = parse_header(data); // 解析请求头
                if(res == BAD_REQUEST){
                    return BAD_REQUEST;
                }else if(res == GET_REQUEST){ // 获取到完整请求
                    return solve_request(); // 处理请求
                }
                break;
            }

            case CHECK_STATE_CONTENT:{
                res = parse_content(data); // 解析请求体
                if(res == GET_REQUEST){
                    return solve_request();
                }
                lineStatus = LINE_OPEN; 
                break;
            }
            
            default:{
                return INTERNAL_ERROR;
            }
        }
    }
    return NO_REQUEST; // 数据不完整
}

// 解析HTTP请求首行：请求方法，目标URL，HTTP协议版本
// GET url HTTP/1.1
httpConnect::HTTP_CODE httpConnect::parse_requsetLine(char* data){
    url = strpbrk(data, " \t"); // 在data中定位第一个匹配字符串" \t"中字符的字符
    *url++ = '\0';
    if(strcasecmp(data, "GET")== 0){ // 不计大小写比较字符串
        requestMethod = GET;
    }else{ // 暂不支持其他请求
        return BAD_REQUEST;
    }

    httpVersion = strpbrk(url, " \t");
    *httpVersion++ = '\0';
    // webbench需注释
    /*
    if(strcasecmp(httpVersion, "HTTP/1.1")!= 0){ // 暂支持HTTP/1.1
        return BAD_REQUEST; 
    }*/

    // eg: http://192.168.3.100:1000/index.html
    if(strncasecmp(url, "http://", 7)== 0){
        url += 7; // 192.168.3.100:1000/index.html
        url = strchr(url, '/'); // /index.html
    }
    if(url[0] != '/'){
        return BAD_REQUEST;
    }
    // 请求行解析结束
    checkState = CHECK_STATE_HEADER;
    return NO_REQUEST;
}

// 解析HTTP请求头
httpConnect::HTTP_CODE httpConnect::parse_header(char* data){
    // 遇空行，表示头部字段解析完毕
    if(data[0] == '\0'){
        // 如果HTTP请求有请求体，则还需要读取contentLength字节的消息体，
        // 状态机转移到CHECK_STATE_CONTENT状态
        if(contentLength != 0){
            checkState = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        // 已经得到了一个完整的HTTP请求
        return GET_REQUEST;
    }else if(strncasecmp(data, "Connection:", 11)== 0){
        // 处理Connection 头部字段  Connection: keep-alive
        data += 11;
        data += strspn(data, " \t");
        if(strcasecmp(data, "keep-alive")== 0){
            connectState = true;
        }
    }else if(strncasecmp(data, "Content-Length:", 15)== 0){
        // 处理Content-Length头部字段
        data += 15;
        data += strspn(data, " \t");
        contentLength = atol(data);
    }else if(strncasecmp(data, "Host:", 5)== 0){
        // 处理Host头部字段
        data += 5;
        data += strspn(data, " \t");
        host = data;
    }else{
        //printf("Error! Unknow header %s\n", data);
    }
    return NO_REQUEST;
}

// 解析HTTP请求体 ：仅判断是否完整读入
httpConnect::HTTP_CODE httpConnect::parse_content(char* data){
    if(readIndex >= contentLength + checkIndex){
        data[contentLength] = '\0'; 
        return GET_REQUEST;
    }
    return NO_REQUEST;  
}

// 解析某一行，\r\n替换为分界符'\0'
httpConnect::LINE_STATUS httpConnect::parse_line(){
    char c;
    // 遍历一行数据
    for(; checkIndex < readIndex; ++ checkIndex){
        c = readBuf[checkIndex];
        if(c == '\r'){
            if(checkIndex + 1 == readIndex){ // 不完整，缺少'\n'
                return LINE_OPEN;
            }else if(readBuf[checkIndex + 1] == '\n'){
                readBuf[checkIndex ++] = '\0'; // 字符串分隔
                readBuf[checkIndex ++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }else if(c == '\n'){
            if(checkIndex > 1 && readBuf[checkIndex - 1] == 'r'){
                readBuf[checkIndex - 1] == '\0';
                readBuf[checkIndex ++] == '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    return LINE_OPEN;
}

// 处理请求
httpConnect::HTTP_CODE httpConnect::solve_request(){
    strcpy(targetFile, rootDirectory);
    int len = strlen(rootDirectory);
    strncpy(targetFile + len, url, FILENAME_LEN - len - 1);
    // 获取targetFile文件的相关的状态信息，-1失败，0成功
    if(stat(targetFile, &targetFileStat)< 0){
        return NO_RESOURCE;
    }

    // 判断访问权限
    if(!(targetFileStat.st_mode & S_IROTH)){
        return FORBIDDEN_REQUEST;
    }

    // 判断是否是目录
    if(S_ISDIR(targetFileStat.st_mode)){
        return BAD_REQUEST;
    }

    // 以只读方式打开文件
    int fd = open(targetFile, O_RDONLY);
    // 创建内存映射，提高效率
    targetFileAddress =(char*)mmap(0, targetFileStat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    return FILE_REQUEST;
}

// 释放内存映射
void httpConnect::unmap(){
    if(targetFileAddress){
        munmap(targetFileAddress, targetFileStat.st_size);
        targetFileAddress = 0;
    }
}

// 线程池的业务逻辑，处理HTTP请求
void httpConnect::process(){
    // 解析HTTP请求
    HTTP_CODE read_ret = process_read();
    if(read_ret == NO_REQUEST){
        modfd(m_epollfd, m_socketfd, EPOLLIN);
        return;
    }

    // 生成响应
    bool write_ret = process_write(read_ret);
    if(!write_ret){ // 失败
        closeConnect();
    }
    modfd(m_epollfd, m_socketfd, EPOLLOUT); //监听写事件
}

// 写HTTP响应
bool httpConnect::write()
{
    int temp = 0;    
    if(bytes_to_send == 0){
        // 将要发送的字节为0，这一次响应结束。
        modfd(m_epollfd, m_socketfd, EPOLLIN); 
        init();
        return true;
    }

    while(1){
        // 分散写
        temp = writev(m_socketfd, m_iv, m_iv_count);
        printf("写数据:%d 字节\n", temp);
        if(temp <= -1){
            // 如果TCP写缓冲没有空间，则等待下一轮EPOLLOUT事件，虽然在此期间，
            // 服务器无法立即接收到同一客户的下一个请求，但可以保证连接的完整性。
            if(errno == EAGAIN){
                modfd(m_epollfd, m_socketfd, EPOLLOUT);
                return true;
            }
            unmap();
            return false;
        }
        bytes_to_send -= temp;
        bytes_have_send += temp;
        if(bytes_have_send >= m_iv[0].iov_len){ // 响应头发送介绍
            m_iv[0].iov_len = 0;
            m_iv[1].iov_base = targetFileAddress + bytes_have_send - writeIndex;
            m_iv[1].iov_len = bytes_to_send;
        }else{ // 未发送完毕 
            m_iv[0].iov_base = writeBuf + bytes_have_send;
            m_iv[0].iov_len -= temp;
        }
        if(bytes_to_send <= 0){
            // 发送HTTP响应成功，根据HTTP请求中的Connection字段决定是否立即关闭连接
            unmap();
            if(connectState){
                init();
                modfd(m_epollfd, m_socketfd, EPOLLIN);
                return true;
            } else {
                modfd(m_epollfd, m_socketfd, EPOLLIN);
                return false;
            } 
        }
    }
}

// 往写缓冲中写入待发送的数据
bool httpConnect::add_response(const char* format, ...){
    if(writeIndex >= WRITE_BUFFER_SIZE){ // 写缓冲已满
        return false;
    }
    va_list arg_list;
    va_start(arg_list, format);
    int len = vsnprintf(writeBuf + writeIndex, WRITE_BUFFER_SIZE - 1 - writeIndex, format, arg_list);
    if(len >=(WRITE_BUFFER_SIZE - 1 - writeIndex)){
        return false;
    }
    writeIndex += len;
    va_end(arg_list);
    return true;
}

bool httpConnect::add_status_line(int status, const char* title){
    return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}

void httpConnect::add_headers(int content_len){
    add_content_length(content_len);
    add_content_type();
    add_state();
    add_blank_line();
}

bool httpConnect::add_content_length(int content_len){
    return add_response("Content-Length: %d\r\n", content_len);
}

bool httpConnect::add_state()
{
    return add_response("Connection: %s\r\n",(connectState == true)? "keep-alive" : "close");
}

bool httpConnect::add_blank_line()
{
    return add_response("%s", "\r\n");
}

bool httpConnect::add_content(const char* content)
{
    return add_response("%s", content);
}

bool httpConnect::add_content_type(){
    return add_response("Content-Type:%s\r\n", "text/html");
}

// 根据处理请求的结果，确定要写给client的内容
bool httpConnect::process_write(HTTP_CODE read_ret){
    switch(read_ret)
        {
            case INTERNAL_ERROR:
                add_status_line(500, error_500_title);
                add_headers(strlen(error_500_form));
                if(! add_content(error_500_form)){
                    return false;
                }
                break;
            case BAD_REQUEST:
                add_status_line(400, error_400_title);
                add_headers(strlen(error_400_form));
                if(! add_content(error_400_form)){
                    return false;
                }
                break;
            case NO_RESOURCE:
                add_status_line(404, error_404_title);
                add_headers(strlen(error_404_form));
                if(! add_content(error_404_form)){
                    return false;
                }
                break;
            case FORBIDDEN_REQUEST:
                add_status_line(403, error_403_title);
                add_headers(strlen(error_403_form));
                if(! add_content(error_403_form)){
                    return false;
                }
                break;
            case FILE_REQUEST:
                add_status_line(200, ok_200_title);
                add_headers(targetFileStat.st_size);
                m_iv[0].iov_base = writeBuf;
                m_iv[0].iov_len = writeIndex;
                m_iv[1].iov_base = targetFileAddress;
                m_iv[1].iov_len = targetFileStat.st_size;
                m_iv_count = 2;
                bytes_to_send = writeIndex + targetFileStat.st_size;
                return true;
            default:
                return false;
        }

        m_iv[0].iov_base = writeBuf;
        m_iv[0].iov_len = writeIndex;
        m_iv_count = 1;
        return true;   
}