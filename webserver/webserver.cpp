#include "../threadpool/threadpool.h"
#include "../http/http_conn.h"
#include "../log/log.h"
#include "webserver.h"

Webserver::Webserver()
{
    // 新建http_conn类对象
    users = new http_conn[MAX_FD];

    // root文件夹路径
    char server_path[200];
    getcwd(server_path, 200);
    char root[6] = "/root";
    m_root = (char*)malloc(strlen(server_path) + strlen(root) + 1);
    strcpy(m_root, server_path);
    strcat(m_root, root);

    users_timers = new client_data[MAX_FD];
}

Webserver::~Webserver()
{
    close(m_epollfd);
    close(m_listenfd);
    close(m_pipefd[0]);
    close(m_pipefd[1]);
    delete [] users;
    delete [] users_timers;
    delete m_pool;
    // 连接池虽然是个指针，但不是new出来的，，不需要我们自己析构，跟程序生命周期一样
    // delete m_connpool;
}


void Webserver::init(int port, string user, string passwd, string database, int log_write,
        int opt_linger, int trigmode, int sql_num, int thread_num, int close_log, int actor_model)
{
    m_port = port;
    m_user = user;
    m_passwd = passwd;
    m_database = database;
    m_sql_num = sql_num;
    m_thread_num = thread_num;
    m_log_write = log_write;
    m_opt_linger = opt_linger;
    m_TRIGMode = trigmode;
    m_close_log = close_log;
    m_actor_model = actor_model;
}

// 获取线程池
void Webserver::thread_pool()
{
    m_pool = new threadpool<http_conn>(m_actor_model, m_connpool, m_thread_num);
}

// 获取数据库连接池
void Webserver::sql_pool()
{
    m_connpool = connection_pool::getInstance();
    m_connpool->init("localhost", 3306, m_user, m_passwd, m_database, m_sql_num, m_close_log);
    users->initmysql_result(m_connpool);
}

// 初始化日志
void Webserver::log_write()
{
    if(0 == m_close_log)
    {
        //初始化日志
        // m_log_write == 1 表示异步写
        if (1 == m_log_write)
            Log::get_instance()->init("./ServerLog", m_close_log, 2000, 800000, 800);
        else
            Log::get_instance()->init("./ServerLog", m_close_log, 2000, 800000, 0);
    }
}

// 设置触发模型  监听socket  和  连接socket 的四种组合
void Webserver::trig_mode()
{
    //LT + LT
    if (0 == m_TRIGMode)
    {
        m_listen_trigmode = 0;
        m_conn_trigmode = 0;
    }
    //LT + ET
    else if (1 == m_TRIGMode)
    {
        m_listen_trigmode = 0;
        m_conn_trigmode = 1;
    }
    //ET + LT
    else if (2 == m_TRIGMode)
    {
        m_listen_trigmode = 1;
        m_conn_trigmode = 0;
    }
    //ET + ET
    else if (3 == m_TRIGMode)
    {
        m_listen_trigmode = 1;
        m_conn_trigmode = 1;
    }
}

// 创建监听套接字，创建epoll监听时间内核表注册事件，创建一对套接字pipe[2]
// 初始化定时器里的工具类， 添加要处理的信号
void Webserver::eventListen()
{
    m_listenfd = socket(AF_INET, SOCK_STREAM, 0);
    assert(m_listenfd >= 0);

    // 优雅关闭连接
    if(1 == m_opt_linger)
    {
        struct linger tmp = {1, 1};
        setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
    }
    else if(0 == m_opt_linger)
    {
        struct linger tmp = {0, 1};
        setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
    }

    int ret = 0;
    struct sockaddr_in address;
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(m_port);
    address.sin_addr.s_addr = INADDR_ANY;
    // inet_pton(AF_INET, INADDR_ANY, NULL);

    int flag = 1;
    setsockopt(m_listenfd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));
    ret = bind(m_listenfd, (struct sockaddr *)&address, sizeof(address));
    assert(ret >= 0);

    ret = listen(m_listenfd, 5);
    assert(ret >= 0);

    utils.init(TIMESLOT);

    // epoll创建内核事件表
    epoll_event events[MAX_EVENT_NUMBER];
    m_epollfd = epoll_create(5);
    assert(m_epollfd != -1);

    utils.addfd(m_epollfd, m_listenfd, false, m_listen_trigmode);
    http_conn::m_epollfd = m_epollfd;

    // 创建一对套接字给主线程和工作线程通信用
    ret = socketpair(AF_UNIX, SOCK_STREAM, 0, m_pipefd);
    assert(ret != -1);
    // 写端非阻塞，1端是给主线程用的
    utils.setnonblocking(m_pipefd[1]);
    utils.addfd(m_epollfd, m_pipefd[0], false, 0);
    utils.addsig(SIGPIPE, SIG_IGN);
    utils.addsig(SIGTERM, utils.sig_handler, false);
    utils.addsig(SIGALRM, utils.sig_handler, false);

    // 工具类，提供信号和文件描述符基础操作
    Utils::u_pipefd = m_pipefd;
    Utils::u_epollfd = m_epollfd;

    alarm(TIMESLOT);
}

// 初始化新连接connfd，并为其创建一个定时器，添加到定时器链表中
void Webserver::timer(int connfd, struct sockaddr_in client_addr)
{
    users[connfd].init(connfd, client_addr, m_root, m_conn_trigmode, m_close_log, m_user, m_passwd, m_database);
    // 初始化client_data数据
    // 创建定时器，设置回调函数和超时事件，绑定用户数据，将定时器添加到链表
    users_timers[connfd].address = client_addr;
    users_timers[connfd].sockfd = connfd;

    util_timer* timer = new util_timer;
    timer->user_data = &users_timers[connfd];
    timer->cb_func = cb_func;

    time_t cur = time(NULL);
    timer->expire = cur + TIMESLOT * 3;
    // printf("timer for %d\n", connfd);
    users_timers[connfd].timer = timer;
    utils.m_timer_lst.add_timer(timer);
}

// 若有数据传输，则将定时器往后延迟3个单位（TIMESLOT）
// 并调整其在链表中的位置
void Webserver::adjust_timer(util_timer *timer)
{
    time_t cur = time(NULL);
    timer->expire = cur + 3 * TIMESLOT;
    utils.m_timer_lst.adjust_timer(timer);
    LOG_INFO("%s", "adjust time once");
}

// 处理定时器  
void Webserver::dealTimer(util_timer* timer, int sockfd)
{
    //printf("in dealTimer, fd %d\n", sockfd);
    // 删除非活动连接socket上的注册事件，并关闭连接
    timer->cb_func(&users_timers[sockfd]);
    // 删除定时器
    if(timer)
    {
        utils.m_timer_lst.del_timer(timer);
    }
    LOG_INFO("close fd %d", users_timers[sockfd].sockfd);
}

// 处理客户端
bool Webserver::dealClientData()
{
    struct sockaddr_in client_addr;
    socklen_t c_len = sizeof(client_addr);
    if(0 == m_listen_trigmode)
    {
        int connfd = accept(m_listenfd, (struct sockaddr*)&client_addr, &c_len);
        if(connfd < 0)
        {
            LOG_ERROR("accept error:error is:%d", errno);
            return false;
        }
        if(http_conn::m_user_count >= MAX_FD)
        {
            utils.show_error(connfd, "Internal server busy");
            LOG_ERROR("%s", "Internal server busy");
            return false;
        }
        timer(connfd, client_addr);
    }
    else
    {
        while(1)
        {
            int connfd = accept(m_listenfd, (struct sockaddr*)&client_addr, &c_len);
            if(connfd < 0)
            {
                LOG_ERROR("accept error:error is:%d", errno);
                return false;
            }
            if(http_conn::m_user_count >= MAX_FD)
            {
                utils.show_error(connfd, "Internal server busy");
                LOG_ERROR("%s", "Internal server busy");
                return false;
            }
            timer(connfd, client_addr);
        }
        return false;
    }
    return true;
}


// 处理信号
bool Webserver::dealWithSignal(bool& timeout, bool& stop_server)
{
    char signals[1024];
    int ret = recv(m_pipefd[0], signals, 1024, 0);
    if(ret <= 0) return false;
    else
    {
        for (int i = 0; i < ret; ++i)
        {
            switch (signals[i])
            {
            case SIGALRM:
            {
                timeout = true;
                break;
            }
            case SIGTERM:
            {
                stop_server = true;
                break;
            }
            }
        }
    }
    return true;
}

// 处理读事件
void Webserver::dealWithRead(int sockfd)
{
    util_timer* timer = users_timers[sockfd].timer;
    // reactor 模式  通知工作线程有数据可读，工作线程自己读和处理
    if(1 == m_actor_model)
    {
        if(timer) adjust_timer(timer);
        // 往线程池任务队列添加任务
        m_pool->append(users + sockfd, 0);

        while(true)
        {
            if(1 == users[sockfd].improv)
            {
                if(1 == users[sockfd].timer_flag)
                {
                    dealTimer(timer, sockfd);
                    users[sockfd].timer_flag = 0;
                }
                users[sockfd].improv = 0;
                break;
            }
        }
    }
    // proactor模式   自己读数据，然后再通知工作线程来处理数据
    else
    {
        if(users[sockfd].read())
        {
            LOG_INFO("deal with the client(%s)", inet_ntoa(users[sockfd].get_address()->sin_addr));

            //若监测到读事件，将该事件放入请求队列
            m_pool->append(users + sockfd);
            if (timer)
            {
                adjust_timer(timer);
            }
        }
        else
        {
            //printf("dealTimer int read, fd %d\n", sockfd);
            dealTimer(timer, sockfd);
        }
    }
    
}

// 处理写事件
void Webserver::dealWithWrite(int sockfd)
{
    util_timer* timer = users_timers[sockfd].timer;
    // reactor 主线程通知工作线程来写数据和发送（处理）
    if(1 == m_actor_model)
    {
        if(timer) adjust_timer(timer);
        m_pool->append(users + sockfd, 1);
        while(true)
        {
            if(1 == users[sockfd].improv)
            {
                if(1 == users[sockfd].timer_flag)
                {
                    dealTimer(timer, sockfd);
                    users[sockfd].timer_flag = 0;
                }
                users[sockfd].improv = 0;
                break;
            }
        }
    }
    else
    {
        if(users[sockfd].write())
        {
            LOG_INFO("send data to the client(%s)", inet_ntoa(users[sockfd].get_address()->sin_addr));
            if(timer)
            {
                adjust_timer(timer);
            }
        }
        else
        {
            //printf("dealTimer int write, fd %d\n", sockfd);
            dealTimer(timer, sockfd);
        }
        
    }
}

void Webserver::eventLoop()
{
    bool timeout = false;
    bool stop_server = false;
    while(!stop_server)
    {
        int number = epoll_wait(m_epollfd, events, MAX_EVENT_NUMBER, -1);
        if(number < 0 && errno != EINTR)
        {
            LOG_ERROR("%s", "epoll failure");
            break;
        }
        for(int i = 0; i < number; ++i)
        {
            int sockfd = events[i].data.fd;
            // 处理新连接
            if(sockfd == m_listenfd)
            {
                bool flag = dealClientData();
                if(!flag) continue;
            }
            // EPOLLIN和EPOLLRDHUP才是对方正常关闭，，这个好像不对，要测试一下
            else if(events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
            {
                //服务器端关闭连接，移除对应的定时器
                // ("dealTimer int close, fd %d\n", sockfd);
                util_timer *timer = users_timers[sockfd].timer;
                dealTimer(timer, sockfd);
            }
            //处理信号
            else if ((sockfd == m_pipefd[0]) && (events[i].events & EPOLLIN))
            {
                bool flag = dealWithSignal(timeout, stop_server);
                if (!flag)
                    LOG_ERROR("%s", "dealclientdata failure");
            }
            //处理客户连接上接收到的数据
            else if (events[i].events & EPOLLIN)
            {
                dealWithRead(sockfd);
            }
            else if (events[i].events & EPOLLOUT)
            {
                dealWithWrite(sockfd);
            }
        }
        if(timeout)
        {
            utils.timer_handler();
            LOG_INFO("%s", "timer tick");
            timeout = false;
        }
    }
}

