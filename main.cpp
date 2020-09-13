#include "config/config.h"

int main(int argc, char* argv[])
{
    // 数据库信息
    string user = "crl";
    string passwd = "root";
    string database = "yourdb";

    // 命令行解析
    Config config;
    config.parse_arg(argc, argv);

    Webserver server;
    // 初始化服务器配置
    server.init(config.port, user, passwd, database, config.log_write, config.opt_linger,
                    config.trigmode, config.sql_num, config.thread_num, config.close_log, config.actor_model);
    
    // 启动日志
    server.log_write();
    // 获取数据库连接池
    server.sql_pool();
    // 获取线程池
    server.thread_pool();
    // 设置listenfd与connfd的触发模式组合
    server.trig_mode();
    // 启动监听 默认端口 8888
    server.eventListen();
    // 运行
    server.eventLoop();

    return 0;
}