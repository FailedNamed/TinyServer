#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <sys/socket.h>
#include <sys/stat.h>
#include <assert.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <stdlib.h>
#include <errno.h>
#include <netinet/in.h>

#include "../threadpool/threadpool.h"
#include "../http/http_conn.h"

const int MAX_FD = 65536;               // 最大文件描述符
const int MAX_EVENT_NUMBER = 10000;     // 最大事件数
const int TIMESLOT = 5;                 // 最小超时单位

class Webserver
{
public:
    Webserver();
    ~Webserver();

    void init(int port, string user, string passwd, string database, int log_write,
        int opt_linger, int trigmode, int sql_num, int thread_num, int close_log, int actor_model);

    void thread_pool();
    void sql_pool();
    void log_write();
    void trig_mode();
    void eventListen();
    void eventLoop();
    void timer(int connfd, struct sockaddr_in client_addr);
    void adjust_timer(util_timer *timer);
    void dealTimer(util_timer* timer, int sockfd);
    bool dealClientData();
    bool dealWithSignal(bool& timeout, bool& stop_server);
    void dealWithRead(int sockfd);
    void dealWithWrite(int sockfd);

private:
    // 基础数据
    int m_port;
    char* m_root;
    // 异步写1  同步0
    int m_log_write;
    int m_close_log;
    // 模型选择 Reactor / Profactor
    int m_actor_model;  

    int m_pipefd[2];
    int m_epollfd;
    http_conn* users;

    int m_listenfd;
    int m_opt_linger;
    // epoll模型组合，看trig_mode函数  LT/ET
    int m_TRIGMode;        
    int m_listen_trigmode;   // 监听socket模式
    int m_conn_trigmode;     // 连接socket模式

    // 数据库相关
    connection_pool* m_connpool;
    string m_user;      // 用户名
    string m_passwd;    // 密码
    string m_database;  // 数据库名
    int m_sql_num;      // 连接数

    // 线程池相关
    threadpool<http_conn>* m_pool;
    int m_thread_num;

    // epoll_event
    epoll_event events[MAX_EVENT_NUMBER];

    // 定用户数据，和定时器绑定在一起
    client_data* users_timers;
    // 工具类
    Utils utils;

};

#endif