#ifndef LOCKER_H
#define LOCKER_H

// 使用POSIX信号量 + 互斥锁 + 条件变量
// 实现 线程同步机制

#include <exception>
#include <pthread.h>
#include <semaphore.h>

class sem
{
public:
    // 一个信号量
    sem()
    {
        if(sem_init(&m_sem, 0, 0) != 0){
            throw std::exception();
        }
    }
    // 指定信号量
    sem(int num)
    {
        if(sem_init(&m_sem, 0, num) != 0){
            throw std::exception();
        }
    }
    ~sem()
    {
        sem_destroy(&m_sem);
    }
    // 等待信号量
    bool wait()
    {
        // sem_wait返回0表示成功得到信号量
        return sem_wait(&m_sem) == 0;
    }
    // 释放信号量
    bool post()
    {
        return sem_post(&m_sem) == 0;
    }
private:
    sem_t m_sem;
};

// 互斥锁
class locker
{

public:
    locker()
    {
        if(pthread_mutex_init(&m_mutex, NULL) != 0)
        {
            throw std::exception();
        }
    }
    ~locker()
    {
        pthread_mutex_destroy(&m_mutex);
    }
    // 锁上
    bool lock()
    {
        return pthread_mutex_lock(&m_mutex) == 0;
    }
    // 解锁
    bool unlock()
    {
        return pthread_mutex_unlock(&m_mutex) == 0;
    }
    // 拿锁对象
    pthread_mutex_t* get()
    {
        return &m_mutex;
    }

private:
    pthread_mutex_t m_mutex;
};

// 条件变量
class cond
{
public:
    cond()
    {
        if(pthread_cond_init(&m_cond, NULL) != 0)
        {
            throw std::exception();
        }
    }
    ~cond()
    {
        pthread_cond_destroy(&m_cond);
    }
    // 
    bool wait(pthread_mutex_t *m_mutex)
    {
        int ret = 0;
        //pthread_mutex_lock(&m_mutex);
        ret = pthread_cond_wait(&m_cond, m_mutex);
        //pthread_mutex_unlock(&m_mutex);
        return ret == 0;
    }
    bool timewait(pthread_mutex_t *m_mutex, struct timespec t)
    {
        int ret = 0;
        //pthread_mutex_lock(&m_mutex);
        ret = pthread_cond_timedwait(&m_cond, m_mutex, &t);
        //pthread_mutex_unlock(&m_mutex);
        return ret == 0;
    }
    // 唤醒等待条线变量的线程
    bool signal()
    {
        return pthread_cond_signal(&m_cond) == 0;
    }
    // 广播方式 唤醒等待条线变量的线程
    // 叫醒哪个，取决于线程优先级和调度策略
    bool broadcast()
    {
        return pthread_cond_broadcast(&m_cond) == 0;
    }

private:
    pthread_cond_t m_cond;
};




#endif