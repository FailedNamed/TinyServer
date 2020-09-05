#include "lst_timer.h"
#include "../http/http_conn.h"

sort_timer_lst::sort_timer_lst() : head(NULL), tail(NULL) {}

sort_timer_lst::~sort_timer_lst()
{
    util_timer* tmp = head;
    while (tmp)
    {
        head = tmp->next;
        delete tmp;
        tmp = head;
    } 
}

// 往链表中添加定时器
void sort_timer_lst::add_timer(util_timer *timer)
{
    // 定时器空
    if(!timer) return;
    // 链表空
    if(!head)
    {
        head = tail = timer;
        return;
    }
    // 插入在链表头部
    if(timer->expire < head->expire)
    {
        timer->next = head;
        head->pre = timer;
        head = timer;
        return;
    }
    // 不在头部，则遍历插入，调用重载的函数（私有的，只被类内部方法调用）
    add_timer(timer, head);
}

// 删除定时器
void sort_timer_lst::del_timer(util_timer* timer)
{
    if(!timer) return;
    // 单节点
    if(timer == head && timer == tail)
    {
        delete timer;
        head = NULL;
        tail = NULL;
        return;
    }
    // 头结点
    if(timer == head)
    {
        head = head->next;
        head->pre = NULL;
        delete timer;
        return;
    }
    // 尾结点
    if(timer == tail)
    {
        tail = tail->pre;
        tail->next = NULL;
        delete timer;
        return;
    }
    timer->pre->next = timer->next;
    timer->next->pre = timer->pre;
    delete timer;
}

// 调整定时器位置，一般是延长到期时间才需要
void sort_timer_lst::adjust_timer(util_timer *timer)
{
    // 定时器空
    if(!timer) return;

    util_timer* tmp = timer->next;
    // 在尾部或者到期时间小于下一个定时器不用调整
    if(!tmp || (timer->expire < tmp->expire))
    {
        return;
    }
    // 是头结点, 把头结点拿出来，重新插入
    if(timer == head)
    {
        head = head->next;
        head->pre = NULL;
        timer->next = NULL;
        add_timer(timer, head);
    }
    else
    {
        // 把节点剥离，在下一个节点之后找位置插入
        timer->pre->next = timer->next;
        timer->next->pre = timer->pre;
        add_timer(timer, timer->next);
    }
}

// 从lst_head节点往后找插入位置
void sort_timer_lst::add_timer(util_timer *timer, util_timer *lst_head)
{
    util_timer* tmp = lst_head->next;
    util_timer* prev = lst_head;
    while(tmp)
    {
        if(timer->expire < tmp->expire)
        {
            prev->next = timer;
            timer->pre = prev;
            timer->next = tmp;
            tmp->pre = timer;
            break;
        }
        prev = tmp;
        tmp = tmp->next;
    }
    // 遍历完也没有满足条件的，插在尾部
    if(!tmp)
    {
        prev->next = timer;
        timer->pre = prev;
        timer->next = NULL;
        tail = timer;
    }
}

//SIGALRM信号触发时，执行tick函数，遍历处理链表上到时的任务
void sort_timer_lst::tick()
{
    if(!head) return;
    time_t cur = time(NULL);
    util_timer* tmp = head;
    // 遍历处理定时器，直到遇到一个没过期的就停止
    while(tmp)
    {
        if(cur < tmp->expire) break;
        // 调用定时器回调函数，执行定时任务
        tmp->cb_func(tmp->user_data);
        // 调用定时器回调函数之后，删除，重置表头节点
        head = tmp->next;
        if(head) head->pre = NULL;
        delete tmp;
        tmp = head;
    }
}



void Utils::init(int timeslot)
{
    m_TIMESLOT = timeslot;
}

int Utils::setnonblocking(int fd)
{
    int old_opt = fcntl(fd, F_GETFL);
    int new_opt = old_opt | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_opt);
    return old_opt;
}

void Utils::addfd(int epollfd, int fd, bool one_shot, int TRIGMode)
{
    epoll_event event;
    event.data.fd = fd;
    if(1 == TRIGMode) event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    else event.events = EPOLLIN | EPOLLRDHUP;
    if(one_shot) event.events |= EPOLLET;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}

// 信号处理函数，就是把信号通过管道发送到主线程
// 这样使得信号事件也和跟IO事件一样被epoll监听，实现统一事件源
void Utils::sig_handler(int sig)
{
    int save_errno = errno;
    int msg = sig;
    send(u_pipefd[1], (char *)&msg, 1, 0);
    errno = save_errno;
}

//设置信号函数
void Utils::addsig(int sig, void(handler)(int), bool restart)
{
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    // SA_RESTART 结束后重新调用被该信号终止的系统调用
    if(restart) sa.sa_flags |= SA_RESTART;
    // 处理该信号时屏蔽其他信号的集合  sa.mask
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig, &sa, NULL) != -1);
}

// 定时处理任务，并重新设置闹钟，以不断触发SIGALRM信号
void Utils::timer_handler()
{
    m_timer_lst.tick();
    alarm(m_TIMESLOT);
}

void Utils::show_error(int connfd, const char *info)
{
    send(connfd, info, strlen(info), 0);
    close(connfd);
}

int* Utils::u_pipefd = 0;
int Utils::u_epollfd = 0;

class Utils;
// 定时器回调函数  删除非活动连接socket上的注册事件，并关闭
void cb_func(client_data* user_data)
{
    epoll_ctl(Utils::u_epollfd, EPOLL_CTL_DEL, user_data->sockfd, 0);
    assert(user_data);
    close(user_data->sockfd);
    --http_conn::m_user_count;
}