#include <time.h>
#include <sys/time.h>

#include "log.h"

// 异步需要设置阻塞队列的长度，同步的不用
bool Log::init(const char* filename, int close_log, int log_buf_size, int max_lines, int max_queue_size)
{
    // 如果设置了队列长度，则设置为异步
    if(max_queue_size >= 1)
    {
        m_is_asyn = true;
        m_log_queue = new block_queue<string>(max_queue_size);
        pthread_t tid;
        // flush_log_thread是回调函数，这里表示创建一条线程来异步写日志
        pthread_create(&tid, NULL, flush_log_thread, NULL);
    }
    m_close_log = close_log;
    m_log_buf_size = log_buf_size;
    m_max_lines = max_lines;

    m_buf = new char[log_buf_size];
    memset(m_buf, '\0', log_buf_size);

    // 获取当前时间， 用于构建文件名
    time_t t = time(NULL);
    struct tm* sys_tm = localtime(&t);
    struct tm my_tm = *sys_tm;

    // strrchr 从后往前找第一个出现'/'字符的位置  
    // eg:  stack_take  找t  返回 take
    // 防止传入的是路径，处理出文件名
    const char* p = strrchr(filename, '/');
    char log_full_name[256] = {0};

    // 不包含路径，默认在当前目录下
    if(p == NULL)
    {
        snprintf(log_full_name, 255, "%d_%02d_%02d_%s",
         my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, filename);
    }
    else
    {
        // 取出文件名
        strcpy(log_file_ame, p + 1);
        // 取出目录名
        strncpy(dir_name, filename, p - filename + 1);
        // 格式化得到最终的文件名
        snprintf(log_full_name, 255, "%s%d_%02d_%02d_%s",
         dir_name, my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, log_file_ame);
        
    }
    m_day = my_tm.tm_mday;
    m_fp = fopen(log_full_name, "a");
    if(m_fp == NULL)
    {
        return false;
    }
    return true;
}


void Log::write_log(int level, const char* format, ...)
{
    // 获取当前的秒数
    struct timeval cur = {0, 0};
    gettimeofday(&cur, NULL);
    time_t t = cur.tv_sec;
    struct tm* sys_tm = localtime(&t);
    struct tm my_tm = *sys_tm;

    // 得到日志类型
    char s[16] = {0};
    switch (level)
    {
    case 0:
        strcpy(s, "[debug]:");
        break;
    case 1:
        strcpy(s, "[info]:");
        break;
    case 2:
        strcpy(s, "[warn]:");
        break;
    case 3:
        strcpy(s, "[error]:");
        break;
    default:
        strcpy(s, "[info]:");
        break;
    }

    // 写入一个log
    // 保护m_fp
    m_mutex.lock();
    ++m_lines;

    // 日期变了 或者 日志文件行数到达最大了，  需要重新建一个文件
    if(m_day != my_tm.tm_mday || m_lines % m_max_lines == 0)
    {
        char new_log[256] = {0};
        fflush(m_fp);
        fclose(m_fp);

        char tail[16] = {0};
        snprintf(tail, 16, "%d_%02d_%02d_", my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday);

        // 日期变了
        if(m_day != my_tm.tm_mday)
        {
            snprintf(new_log, 255, "%s%s%s", dir_name, tail, log_file_ame);
            // 更新日期
            m_day = my_tm.tm_mday;
            m_lines = 0;
        }
        // 文件超过容量
        else
        {
            snprintf(new_log, 255, "%s%s%s.%lld", dir_name, tail, log_file_ame, m_lines / m_max_lines);
        }
        m_fp = fopen(new_log, "a");
    }
    m_mutex.unlock();

    /*
    va_start与va_end是成对被调用的，
    开始的时候被调用va_start，获得各输出变量地址
    结束的时候被调用va_end，释放相应的资源
    */
    va_list valist;
    va_start(valist, format);

    string log_str;
    // 保护缓冲区
    m_mutex.lock();

    // 写入的具体时间内容格式
    int n = snprintf(m_buf, 48, "%d-%02d-%02d %02d:%02d:%02d.%06ld %s ",
                     my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday,
                     my_tm.tm_hour, my_tm.tm_min, my_tm.tm_sec, cur.tv_usec, s);
    // 跟snprintf类似， 不过不定长参数来自va_list
    int m = vsnprintf(m_buf + n, m_log_buf_size - 2, format, valist);

    m_buf[n + m] = '\n';
    m_buf[n + m + 1] = '\0';
    log_str = m_buf;

    m_mutex.unlock();

    // 异步且队列未满, 插入一条记录等待唤醒一条线程写入文件
    if(m_is_asyn && !m_log_queue->full())
    {
        m_log_queue->push(log_str);
    }
    // 同步直接写入
    else
    {
        m_mutex.lock();
        fputs(log_str.c_str(), m_fp);
        m_mutex.unlock();
    }

    va_end(valist);

}

void Log::flush()
{
    m_mutex.lock();
    // 强制刷新写入流缓冲区
    fflush(m_fp);
    m_mutex.unlock();
}