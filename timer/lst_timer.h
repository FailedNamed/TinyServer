#ifndef LST_TIMER
#define LST_TIMER

#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <assert.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <sys/epoll.h>

#include "../log/log.h"

// 一个升序双向链表，管理定时器


class util_timer;

// 绑定定时器与一个客户端连接
struct client_data
{
    int sockfd;
    sockaddr_in address;
    util_timer* timer;
};

// 链表节点
class util_timer
{
public:
    util_timer() : pre(NULL), next(NULL) {}

public:
    // 期满，即定时器到期时间(绝对时间)
    time_t expire;
    // 定时器回调函数的函数指针
    void (*cb_func) (client_data*);
    // 定时器对应的客户端数据
    client_data* user_data;
    util_timer* pre;
    util_timer* next;
};

// 一个升序双向链表
class sort_timer_lst
{
public:
    sort_timer_lst();
    ~sort_timer_lst();

    void add_timer(util_timer* timer);
    void del_timer(util_timer* timer);
    // 当对方在超时时间未到之前有了其他动作，就要延长他的到期时间
    void adjust_timer(util_timer* timer);
    // SIGALRM信号触发时，执行tick函数，处理链表上到期节点对应的的客户端
    void tick();

private:
    // 重载的辅助函数，被公有的add_timer和adjust_timer函数调用
    // 将timer添加到lst_head之后的部分链表中
    void add_timer(util_timer* timer, util_timer* lst_head);

    util_timer* head;
    util_timer* tail;
};

// 封装定时器链表的工具类， 包括一些操作定时器时要用到的一些函数
class Utils
{
public:
    Utils() {}
    ~Utils() {}

    // 初始化，给一个时钟时间
    void init(int timeslot);

    // 设置文件描述符非阻塞
    int setnonblocking(int fd);

    // 给epollfd在内核上注册读事件，TRIMode有LT（0）和ET（1），和选择开启EPOLLONESHOT
    void addfd(int epollfd, int fd, bool one_shot, int TRIMode);

    // 信号处理函数
    static void sig_handler(int sig);

    // 设置信号函数
    void addsig(int sig, void(handler)(int), bool restart = true);

    // 定时处理任务
    void timer_handler();

    // 发送错误给对应连接
    void show_error(int connfd, const char* info);

public:
    static int* u_pipefd;  // 与主线程通信的管道
    sort_timer_lst m_timer_lst;
    static int u_epollfd;  // 用户程序的epollfd
    int m_TIMESLOT;
};

// 定时器回调函数
void cb_func(client_data *user_data);

#endif