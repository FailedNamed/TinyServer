#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_H
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include <map>

#include "../lock/locker.h"
#include "../mysqlConnection/sql_connection_pool.h"
#include "../timer/lst_timer.h"
#include "../log/log.h"

class http_conn
{
public:
    // 文件名最大长度
    static const int FILENAME_LEN = 200;
    // 读缓冲区大小
    static const int READ_BUFFER_SIZE = 2048;
    // 写缓冲区大小
    static const int WRITE_BUFFER_SIZE = 1024;
    // HTTP请求方法，只支持GET和POST
    enum METHOD {GET = 0, POST, HEAD, PUT, DELETE, TRACE, OPTIONS, CONNECT, PATCH};
    // 解析客户请求，主状态机的状态
    enum CHECK_STATE {CHECK_STATE_LINE = 0, CHECK_STATE_HEADER, CHECK_STATE_CONTENT};
    // 处理HTTP请求的可能结果
    enum HTTP_CODE {NO_REQUEST, GET_REQUEST, BAD_REQUEST,
                    NO_RESOURCE, FORBIDDEN_REQUEST, FILE_REQUEST,
                    INTERNAL_ERROR, CLOSE_CONNECTION};
    // 读到一个完整的行、行出错和行数据尚且不完整 
    enum LINE_STATUS {LINE_OK = 0, LINE_BAD, LINE_OPEN};

public:
    http_conn() {}
    ~http_conn() {}

public:
    // 初始化新的连接, 函数内部调用私有方法init()
    void init(int sockfd, const sockaddr_in& addr, char *root, int TRIGMode, int close_log, string user, string passwd, string sqlname);
    // 关闭连接
    void close_conn(bool real_close = true);
    // 处理客户请求
    void process();
    // 非阻塞读操作，读取客户端发来的所有数据
    bool read();
    // 非阻塞写操作，响应报文写入函数（发送给客户端）
    bool write();
    // 获取客户端地址
    sockaddr_in* get_address(){
        return &m_address;
    }
    // 同步线程初始化数据库读取表
    void initmysql_result();
    // CGI使用线程池初始化数据库表
    void initmysql_result(connection_pool* connPool);
    

private:
    void init();
    // 从读缓冲区读取，并解析HTTP请求
    HTTP_CODE process_read();
    // 向写缓冲区写入HTTP响应报文（只是报文头），文件是通过writev发送的，不拷贝入写缓冲区
    bool process_write(HTTP_CODE ret);

    // 下面这一组函数被process_read调用以解析HTTP请求
    // 主状态机解析报文中的请求行， 对应主状态机状态CHECK_STATE_LINE
    HTTP_CODE parse_request_line(char* text);
    // 主状态机解析报文中的请求头， 对应主状态机状态CHECK_STATE_HEADER
    HTTP_CODE parse_headers(char* text);
    // 主状态机解析报文中的请求内容,  对应主状态机状态CHECK_STATE_CONTENT
    HTTP_CODE parse_content(char* text);
    // 生成响应报文 
    HTTP_CODE do_request();
    // 将指针往后移动，偏移到将要处理的行开头
    char* get_line() {return m_read_buf + m_start_line;}
    // 从状态机读取一行，分析是请求报文的哪一部分
    LINE_STATUS parse_line();

    void unmap();
    // 下面这一组用于被process_write()调用，写HTTP响应报文到写缓冲区
    bool add_response(const char* format, ...);
    bool add_content(const char* content);
    bool add_status_line(int status, const char* title);
    bool add_headers(int content_length);
    // 下面四个被add_headers调用 填充头部信息
    bool add_content_type();
    bool add_content_length(int content_length);
    bool add_linger();
    bool add_blank_line();


public:
    // 统一事件源epollfd，用于监听所有客户端的epollfd
    static int m_epollfd;
    static int m_user_count;
    // 数据库连接
    MYSQL* mysql;
    // 下面三个都只有reacotr模式用到
    // 读为0，写为1 (是准备给reacotr模式用的，区分任务)
    int m_state;    
    // 也是给reactor模式用的， 1代表当前可以处理任务， 0代表任务正在处理
    int improv;
    // 是否处理定时器, 1 代表连接对应定时器到期，  0代表未到期
    int timer_flag;

private:
    // 客户端连接socket
    int m_sockfd;
    // 客户端地址信息
    sockaddr_in m_address;
    
    // 读缓冲区， 读取存储请求报文数据
    char m_read_buf[READ_BUFFER_SIZE];
    // 读缓冲区数据中最后一个字节的的下一个位置
    int m_read_idx;
    // 当前读取读缓冲区数据读到的位置
    int m_checked_idx;
    // 将要解析的行的起始位置
    int m_start_line;


    // 写缓冲区，保存响应报文
    char m_write_buf[WRITE_BUFFER_SIZE];
    // 写缓存区待发送字节数
    int m_write_idx;

    // 主状态机当前的状态
    CHECK_STATE m_check_state;

    // 下面的用于存储请求报文解析出来的数据
    // 请求方法
    METHOD m_method;
    // 客户请求的目标文件的完整路径， 其内容等于doc_root + m_url, doc_root是网站根目录
    char m_read_file[FILENAME_LEN];
    
    // 客户请求的目标文件名
    char* m_url;
    // HTTP版本协议号，只支持HTTP/1.1
    char* m_version;
    // 主机名
    char* m_host;
    // HTTP请求的消息体长度
    int m_content_length;
    // HTTP是否保活
    bool m_linger;

    // 客户请求的目标文件被mmap映射到内存中的起始位置
    // 就是文件地址
    char* m_file_address;
    // 目标文件的属性，通过它我们可以判断文件是否存在，是否为目录，是否可读，并获取文件大小等
    struct stat m_file_stat;
    // 我们用writev来执行写操作，所以定义下面两个成员,是后两个参数
    // m_iv[idx] 表示第 idx 块内存   iov_base 指向内存的指针  iov_len 内存数据字节数
    struct iovec m_iv[2];
    // 被写内存块的数量
    int m_iv_count;

    // 是否启用POST
    int cgi;    
    // 存储请求头数据 (POST请求携带的数据)
    char* m_string; 
    // 剩余待发送字节数
    int bytes_to_send;
    // 已发送字节数
    int bytes_have_send;
    // 网站根目录
    char* doc_root;

    // ET模式开关
    int m_TRIGMode;
    // 日志开关
    int m_close_log;

    char sql_user[100];
    char sql_passwd[100];
    char sql_name[100];

};



#endif