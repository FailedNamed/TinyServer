#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <list>
#include <pthread.h>
#include <cstdio>
#include <exception>

#include "../lock/locker.h"
#include "../mysqlConnection/sql_connection_pool.h"

template<typename T>
class threadpool
{
public:
    // 参数： 数据库连接池指针， 线程池线程数， 请求队列容量
    threadpool(connection_pool* conn_pool, int thread_num = 8, int max_requests = 10000);

    ~threadpool();

    // 往请求队列插入任务请求 
    bool append(T* request);

private:
    // 工作线程运行入口函数， 调用了run
    static void* worker(void* arg);
    // 循环不断从工作队列取出任务执行
    void run();

private:
    int m_thread_number;    // 线程池里的线程数
    int m_max_requests;     // 请求队列最大请求数
    pthread_t* m_threads;   // 线程数组
    std::list<T*> m_workqueue;  // 任务（请求）队列

    locker m_queuelock;     // 保护请求队列的互斥锁
    sem m_queuestat;        // 是否有任务需要处理
    bool m_stop;            // 是否结束线程

    connection_pool* m_connpool;    // 数据库连接池
    int m_actor_model;      // 模型切换  
};

template<typename T>
threadpool<T>::threadpool(connection_pool* conn_pool, int thread_num, int max_requests):
        m_thread_number(thread_num), m_max_requests(max_requests), m_threads(NULL), m_stop(false), m_connpool(conn_pool)
{
    if(thread_num <= 0 || max_requests <= 0)
    {
        throw std::exception();
    }

    // 创建线程id数组
    m_threads = new thread_t[thread_num];

    if(!m_threads)
    {
        throw std::exception();
    }

    // 创建线程
    for(int i = 0; i < thread_num; ++i)
    {
        // 创建失败
        if(pthread_create(m_threads + i, NULL, worker, this) != 0)
        {
            delete [] m_threads;
            throw std::exception();
        }

        // 创建成功，设置为分离线程
        // 线程结束时,它的资源会被系统自动的回收, 而不再需要在其它线程中对其进行 pthread_join() 操作
        if(pthread_detach(m_threads[i]) != 0)
        {
            delete [] m_threads;
            throw std::exception();
        }
    }
}

template<typename T>
threadpool<T>::~threadpool()
{
    delete [] m_threads;
    // m_stop = true;
}

template<typename T>
bool threadpool<T>::append(T* request)
{
    m_queuelock.lock();
    // 超过容量了
    if(m_workqueue.size() >= m_max_requests)
    {
        m_queuelock.unlock();
        return false;
    }

    m_workqueue.push_back(request);
    m_queuelock.unlock();
    
    // 增加信号量
    m_queuestat.post();

    return true;
}

template<typename T>
void* threadpool<T>::worker(void* arg)
{
    threadpool* pool = (threadpool*) arg;
    pool->run();
    return pool;
}

template<typename T>
void threadpool<T>::run()
{
    while(true)
    {
        m_queuestat.wait();     // 等待任务
        m_queuelock.lock();     // 锁上队列
        if(m_workqueue.empty())
        {
            m_queuelock.unlock();
            continue;
        }

        T* request = m_workqueue.front();
        m_workqueue.pop_front();
        m_queuelock.unlock();

        // 取出的任务是空的
        if(!request)
        {
            continue;
        }

        // 从数据库连接池取出一条连接, 利用了RAII封装类
        connRAII mysqlConn(&request->mysql, m_connpool);
        
        request.process();
    }
}


#endif