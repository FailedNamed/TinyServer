/*
    循环数组实现的阻塞队列，  m_back = (m_back + 1) % m_max_size;
    存放许多条日志记录
    线程安全，每次对队列操作前都要加锁，再解锁
    相当于一个生产者消费者模型
*/

#ifndef BLOCK_QUEUE_H
#define BLOCK_QUEUE_H

#include <iostream>
#include <stdlib.h>
#include <pthread.h>
#include <sys/time.h>
#include "../lock/locker.h"

using namespace std;

template<typename T>
class block_queue
{
public:
    block_queue(int max_size = 100)
    {
        if(max_size <= 0 || max_size >= MAX_SIZE)
        {
            exit(-1);
        }
        m_max_size = max_size;
        m_array = new T[max_size];
        m_size = 0;
        m_front = -1;
        m_back = -1;
    }
    ~block_queue()
    {
        m_mutex.lock();
        if(m_array != NULL)
        {
            delete [] m_array;
        }
        m_mutex.unlock();
    }
    // 队列清空
    void clear()
    {
        m_mutex.lock();
        m_size = 0;
        m_front = -1;
        m_back = -1;
        m_mutex.unlock();
    }
    // 判断队列是否为空
    bool empty()
    {
        m_mutex.lock();
        if(m_size == 0)
        {
            m_mutex.unlock();
            return true;
        }
        m_mutex.unlock();
        return false;
    }
    // 判断队列是否满了
    bool full()
    {
        m_mutex.lock();
        if(m_size >= m_max_size)
        {
            m_mutex.unlock();
            return true;
        }
        m_mutex.unlock();
        return false;
    }
    // 返回队头元素
    bool front(T& value)
    {
        m_mutex.lock();
        if(m_size == 0)
        {
            m_mutex.unlock();
            return false;
        }
        value = m_array[m_front];
        m_mutex.unlock();
        return true;
    }
    // 返回队尾元素
    bool back(T& value)
    {
        m_mutex.lock();
        if(m_size >= m_max_size)
        {
            m_mutex.unlock();
            return false;
        }
        value = m_array[m_back];
        m_mutex.unlock();
        return true;
    }
    // 队列的当前容量
    int size()
    {
        int tmp = 0;
        m_mutex.lock();
        tmp = m_size;
        m_mutex.unlock();
        return tmp;
    }
    // 队列的最大容量
    int max_size()
    {
        int tmp = 0;
        m_mutex.lock();
        tmp = m_max_size;
        m_mutex.unlock();
        return tmp;
    }
    // 往队列添加元素，需要把所有使用队列的线程先唤醒
    // push进一个元素，相当于生产者生产了一个元素
    // 若当前没有线程等待条件变量，唤醒无意义
    bool push(const T& item)
    {
        m_mutex.lock();
        if(m_size >= m_max_size)
        {
            m_cond.broadcast();
            m_mutex.unlock();
            return false;
        }
        m_back = (m_back + 1) % m_max_size;
        m_array[m_back] = item;
        ++m_size;
        m_cond.broadcast();
        m_mutex.unlock();
        return true;
    }
    // 出队，相当于消费者来消费
    // 如果当前队列没有元素，需要等待条件变量
    // 会把item填为新的队头元素
    bool pop(T& item)
    {
        m_mutex.lock();
        if(m_size <= 0)
        {
            if(!m_cond.wait(m_mutex.get()))
            {
                m_mutex.unlock();
                return false;
            }
        }
        m_front = (m_front + 1) % m_max_size;
        --m_size;
        item = m_array[m_front];
        m_mutex.unlock();
        return true;
    }
    // 有超时处理的出队操作
    // 防止线程无限等待条件变量，造成浪费
    // timeout 是毫秒
    bool pop(T& item, int timeout)
    {
        struct timespec t = {0, 0};
        struct timeval cur = {0, 0};
        gettimeofday(&cur, NULL);
        m_mutex.lock();
        if(m_size <= 0)
        {
            t.tv_sec = cur.tv_sec + timeout / 1000;
            t.tv_nsec = (timeout % 1000) * 1000;
            if(!m_cond.timewait(m_mutex.get(), t))
            {
                m_mutex.unlock();
                return false;
            }
        }
        if(m_size <= 0)
        {
            m_mutex.unlock();
            return false;
        }
        m_front = (m_front + 1) % m_max_size;
        --m_size;
        item = m_array[m_front];
        m_mutex.unlock();
        return true;
    }

private:
    locker m_mutex;  // 互斥锁
    cond m_cond;     // 条件变量

    T* m_array;
    int m_max_size;  // 队列最大容量
    int m_size;      // 已使用容量
    int m_front;     // 队头下标
    int m_back;      // 队尾下标
    static int MAX_SIZE;    // 允许的最大容量，不允许超过
};
template<typename T>
int block_queue<T>::MAX_SIZE = 100000000;

#endif