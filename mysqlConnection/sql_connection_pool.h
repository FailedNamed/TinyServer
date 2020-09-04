#ifndef CONNECTION_POOL_H
#define CONNECTION_POOL_H

#include <error.h>
#include <string.h>
#include <iostream>
#include <string>
#include <list>
#include <mysql/mysql.h>

#include "../lock/locker.h"
#include "../log/log.h"

using namespace std;

class connection_pool
{
public:
    MYSQL* getConnection();     // 获取数据库连接
    bool releaseConnection(MYSQL* conn);    // 释放连接
    int getFreeConn();       // 获取空闲连接数
    void destroyConns();     // 销毁所有连接

    static connection_pool* getInstance();  // 单例模式

    // 初始化连接池
    void init(string url, int port, string user, string passwd, string database, int maxConn, int close_log);


private:
    connection_pool();
    ~connection_pool();

    int m_max_conn;     // 最大连接数
    int m_cur_conn;     // 当前连接数
    int m_free_conn;    // 当前空闲连接数
    locker lock;        // 互斥锁, 保护连接池
    list<MYSQL *> connList;  // 连接池
    sem reserve;        // 信号量，可用连接数

public:
    string m_url;   // 主机地址
    int m_port;     // 数据库端口
    string m_user;  // 用户名
    string m_passwd;   // 密码
    string m_database;  // 数据库名
    int m_close_log;    // 日志开关
};

// 把数据库连接池连接的获取和释放封装成RAII对象
class connRAII
{
public:
    connRAII(MYSQL** conn, connection_pool* conn_pool);
    ~connRAII();

private:
    MYSQL* conn;
    connection_pool* pool;
};

#endif