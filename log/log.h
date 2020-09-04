#ifndef LOG_H
#define LOG_H

#include <stdio.h>
#include <iostream>
#include <string.h>
#include <stdarg.h>
#include <pthread.h>
#include "block_queue.h"

using namespace std;


class Log
{
public:
    // C++11以后,使用局部变量懒汉不用加锁
    // 使用指针而不是引用是为了避免拷贝构造函数进行拷贝
    static Log* get_instance()
    {
        // 局部变量，而不是作为全局变量
        static Log instance;
        return &instance;
    }
    // 刷新日志, 即把队列内容写入日志文件，给线程调用
    static void* flush_log_thread(void* args)
    {
        Log::get_instance()->async_write_log();
    }
    // 可选择参数有 日志文件名，写缓冲区大小，最大行数以及日志队列大小
    bool init(const char* filename, int close_log, int log_buf_size = 8192, int max_lines = 5000000, int max_queue_size = 0);

    // 格式化一条日志
    void write_log(int level, const char* format, ...);

    // 刷新缓冲区
    void flush(void);

private:
    Log()
    {
        m_lines = 0;
        m_is_asyn = false;
    }
    virtual ~Log()
    {
        if(m_fp != NULL)
        {
            fclose(m_fp);
        }
    }
    // 异步写日志
    void* async_write_log()
    {
        string single_log;
        // 从阻塞队列中取出一条日志记录，写入日志文件
        // 没记录就会等待
        while(m_log_queue->pop(single_log))
        {
            m_mutex.lock();
            fputs(single_log.c_str(), m_fp);
            m_mutex.unlock();
        }
    }

private:
    char dir_name[128];   // 路径
    char log_file_ame[128];   // 文件名
    int m_max_lines;     // 日志最大行数
    int m_log_buf_size;      // 日志缓冲区大小
    long long m_lines;   // 今天的日志行数，可以超过文件最大行数
    int m_day;           // 几号
    FILE* m_fp;          // 文件指针
    char* m_buf;         // 缓冲区
    block_queue<string> *m_log_queue;   // 阻塞队列
    bool m_is_asyn;          // 异步标志位
    locker m_mutex;      // 互斥锁, 保护缓冲区和文件描述符
    int m_close_log;     // 关闭日志
};

// 如果可变参数被忽略或为空，'##'操作将使预处理器(preprocessor)去除掉它前面的那个逗号。
// 这里使用的 m_close_log 不是来自Log类， 而是调用他的类
#define LOG_DEBUG(format, ...) if(0 == m_close_log) {Log::get_instance()->write_log(0, format, ##__VA_ARGS__); Log::get_instance()->flush();}
#define LOG_INFO(format, ...) if(0 == m_close_log) {Log::get_instance()->write_log(1, format, ##__VA_ARGS__); Log::get_instance()->flush();}
#define LOG_WARN(format, ...) if(0 == m_close_log) {Log::get_instance()->write_log(2, format, ##__VA_ARGS__); Log::get_instance()->flush();}
#define LOG_ERROR(format, ...) if(0 == m_close_log) {Log::get_instance()->write_log(3, format, ##__VA_ARGS__); Log::get_instance()->flush();}

#endif