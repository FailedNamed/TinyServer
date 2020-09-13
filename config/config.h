#ifndef CONFIG_H
#define CONFIG_H

#include "../webserver/webserver.h"

class Config
{
public:
    Config();
    ~Config(){}

    void parse_arg(int argc, char* argv[]);

    // 端口号
    int port;
    // 日志写入方式  同步0和异步1
    int log_write;
    // 组合触发模式 ET与LT的四种组合（listenfd和connfd）
    int trigmode;
    // listenfd 触发模式
    int listen_trigmode;
    // connfd 触发模式
    int conn_trigmode;
    // linger选项， 优雅关闭连接
    int opt_linger;
    // 数据库连接池连接数量
    int sql_num;
    // 线程池线程数
    int thread_num;
    // 日志开关 1开启 0关闭
    int close_log;
    // 并发模型选择  reactor 1  同步模拟proactor 0
    int actor_model;
};

Config::Config()
{
    // 默认端口 8888
    port = 8888;
    // 日志写入方式，默认同步
    log_write = 0;
    // 触发组合模式  默认listenfd LT + connfd LT  0
    trigmode = 1;
    /*
    // listenfd 触发模式  默认 LT
    listen_trigmode = 0;
    // connfd 触发模式 默认 LT
    conn_trigmode = 0;
    */
    // 优雅关闭连接， 默认不使用
    opt_linger = 0;
    // 数据库连接池数量，默认8
    sql_num = 8;
    // 线程池连接池数量，默认8
    thread_num = 8;
    // 日志开关，默认打开
    close_log = 0;
    // 并发模型， 默认是 同步模拟 proactor 0
    actor_model = 1;
}

void Config::parse_arg(int argc, char* argv[])
{
    int opt;
    const char* format = "p:l:m:o:s:t:c:a:";
    while((opt = getopt(argc, argv, format)) != -1)
    {
        switch (opt)
        {
        case 'p':
        {
            port = atoi(optarg);
            break;
        }
        case 'l':
        {
            log_write = atoi(optarg);
            break;
        }
        case 'm':
        {
            // 默认0
            trigmode = atoi(optarg);
            break;
        }
        case 'o':
        {
            opt_linger = atoi(optarg);
            break;
        }
        case 's':
        {
            thread_num = atoi(optarg);
            break;
        }
        case 'c':
        {
            close_log = atoi(optarg);
            break;
        }
        case 'a':
        {
            actor_model = atoi(optarg);
            break;
        }
        default:
            break;
        }
    }
}


#endif