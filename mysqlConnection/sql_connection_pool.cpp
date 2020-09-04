#include <iostream>
#include <pthread.h>
#include <stdlib.h>
#include "sql_connection_pool.h"

using namespace std;

connection_pool::connection_pool()
{
    m_max_conn = 0;
    m_cur_conn = 0;
    m_free_conn = 0;
}

connection_pool::~connection_pool()
{
    destroyConns();
}

connection_pool* connection_pool::getInstance()
{
    static connection_pool instance;
    return &instance;
}

void connection_pool::init(string url, int port, string user, string passwd, string database, int maxConn, int close_log)
{
    m_url = url;
    m_port = port;
    m_user = user;
    m_passwd = passwd;
    m_database = database;
    m_close_log = close_log;
    // m_max_conn = maxConn;

    // 创建数据库连接
    for(int i = 0; i < maxConn; ++i)
    {
        MYSQL* conn = NULL;
        conn = mysql_init(conn);
        // 初始化失败
        if(conn == NULL)
        {
            LOG_ERROR("MYSQL ERROR1");
        }
        // 连接数据库
        conn = mysql_real_connect(conn, url.c_str(), user.c_str(), passwd.c_str(), 
                                database.c_str(), port, NULL, 0);

        if(conn == NULL)
        {
            LOG_ERROR("MYSQL ERROR2");
        }

        connList.push_back(conn);
        ++m_free_conn;
    }

    m_max_conn = m_free_conn;
    reserve = sem(m_max_conn);
}

// 从连接池取出一条连接
MYSQL* connection_pool::getConnection()
{
    MYSQL* conn = NULL;
    if(connList.size() == 0) return NULL;
    
    reserve.wait();
    
    lock.lock();

    conn = connList.front();
    connList.pop_front();

    lock.unlock();

    --m_free_conn;
    ++m_cur_conn;

    return conn;
}

// 把连接放回连接池
bool connection_pool::releaseConnection(MYSQL* conn)
{
    if(conn == NULL) return false;

    lock.lock();
    connList.push_back(conn);
    lock.unlock();

    ++m_free_conn;
    --m_cur_conn;
    reserve.post();

    return true;
}

int connection_pool::getFreeConn()
{
    return this->m_free_conn;
}

// 销毁所有连接，即关闭所有mysql连接
void connection_pool::destroyConns()
{
    lock.lock();
    if(connList.size() > 0)
    {
        list<MYSQL*>::iterator it;
        for(it = connList.begin(); it != connList.end(); ++it)
        {
            MYSQL* conn = *it;
            mysql_close(conn);
        }
        m_cur_conn = 0;
        m_free_conn = 0;
        m_max_conn = 0;
        connList.clear();
    }
    lock.unlock();
}

// 从conn_pool获取一条连接, 放入con中， 然后可以不用手动释放连接
connRAII::connRAII(MYSQL** con, connection_pool* conn_pool)
{
    *con = conn_pool->getConnection();
    conn = *con;
    pool = conn_pool;
}

// 释放连接
connRAII::~connRAII()
{
    pool->releaseConnection(conn);
}
