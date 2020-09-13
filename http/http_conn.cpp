#include "http_conn.h"

#include <fstream>

// 定义HTTP响应的一些状态信息
const char* ok_200_title = "OK";
const char* error_400_title = "BAD Request";
const char* error_400_form = "Your request has bad syntax or is inherently impossible to satisfy.";
const char* error_403_title = "Forbidden";
const char* error_403_form = "You don't have permission to get file from this server.";
const char* error_404_title = "Not Found";
const char* error_404_form = "The requested file was not not found on this server.";
const char* error_500_title = "Internal Error";
const char* error_500_form = "There was an unusual problem serving the requested file.";

locker m_lock;
map<string, string> users;

int http_conn::m_epollfd = -1;
int http_conn::m_user_count = 0;


// 获取一条数据库连接， 并把users数据存入map中
void http_conn::initmysql_result(connection_pool* connPool)
{
    // 从数据库连接池取出一条连接
    MYSQL* mysql = NULL;
    connRAII mysqlconn(&mysql, connPool);

    // 在user表中查找所有username, passwd数据， 浏览器输入
    if(mysql_query(mysql, "select username, passwd from user"))
    {
        LOG_ERROR("select error: %s\n", mysql_error(mysql));
    }

    MYSQL_RES *result;    // 指向查询结果的指针
    MYSQL_ROW result_row; // 按行返回的查询信息
    MYSQL_FIELD *field;   // 字段结构指针

    //从表中检索完整的结果集
    result = mysql_store_result(mysql);

    int row = mysql_num_rows(result);

    // 把用户名和密码存入map users中
    for(int i = 1; i <= row; ++i)
    {
        result_row = mysql_fetch_row(result);
        string name(result_row[0]);
        string passwd(result_row[1]);
        users[name] = passwd;
    }
}

// 设置文件描述符非阻塞
int setnonblocking(int fd)
{
    int old_opt = fcntl(fd, F_GETFL);
    int new_opt = old_opt | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_opt);
    return old_opt;
}

// 给epollfd在内核上注册读事件，TRIMode有LT（0）和ET（1），和选择开启EPOLLONESHOT
void addfd(int epollfd, int fd, bool one_shot, int TRIGMode)
{
    epoll_event event;
    event.data.fd = fd;
    if(1 == TRIGMode) event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    else event.events = EPOLLIN | EPOLLRDHUP;
    if(one_shot) event.events |= EPOLLET;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}

// 从epoll内核事件表删除fd
void removefd(int epollfd, int fd)
{
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}

// 将事件重置为EPOLLONESHOT， ET + EPOLLRDHUP
// 在每次处理完fd上的IO事件后调用，重新注册进内核，不然下次该fd就不会有事件触发了
void modfd(int epollfd, int fd, int ev, int TRIGMode)
{
    epoll_event event;
    event.data.fd = fd;
    event.events = ev | EPOLLONESHOT | EPOLLRDHUP;
    if(1 == TRIGMode) event.events |= EPOLLET;
    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}


void http_conn::init(int sockfd, const sockaddr_in &addr, char *root, int TRIGMode, int close_log, string user, string passwd, string sqlname)
{
    m_sockfd = sockfd;
    m_address = addr;

    addfd(m_epollfd, sockfd, true, TRIGMode);
    ++m_user_count;

    // //当浏览器出现连接重置时，可能是网站根目录出错或http响应格式出错或者访问的文件中内容完全为空
    doc_root = root;
    m_TRIGMode = TRIGMode;
    m_close_log = close_log;

    strcpy(sql_user, user.c_str());
    strcpy(sql_passwd, passwd.c_str());
    strcpy(sql_name, sqlname.c_str());

    init();
}

// 初始化一些变量（跟外部参数无关的）
// 一次请求结束后，若连接还要保持，也需要调用，重置这些参数
void http_conn::init()
{
    mysql = NULL;
    m_file_address = NULL;
    m_state = 0;
    m_read_idx = 0;
    m_checked_idx = 0;
    m_start_line = 0;
    m_write_idx = 0;
    m_check_state = CHECK_STATE_LINE;
    m_method = GET;
    m_url = 0;
    m_version = 0;
    m_host = 0;
    m_content_length = 0;
    m_linger = false;
    bytes_to_send = 0;
    bytes_have_send = 0;
    cgi = 0;

    // 这两个玩意感觉没啥用， 考虑删掉
    timer_flag = 0;
    improv = 0;
    
    memset(m_read_buf, '\0', READ_BUFFER_SIZE);
    memset(m_write_buf, '\0', WRITE_BUFFER_SIZE);
    memset(m_read_file, '\0', FILENAME_LEN);
}

void http_conn::close_conn(bool real_close)
{
    if(real_close && m_sockfd != -1)
    {
        // printf("close fd %d\n", m_sockfd);
        removefd(m_epollfd, m_sockfd);
        m_sockfd = -1;
        --m_user_count;
    }
}

// 循环读取客户数据，直到无数据可读或者对方关闭连接
// 非阻塞ET模式需要把数据一次性读完，LT模式不需要
bool http_conn::read()
{
    if(m_read_idx >= READ_BUFFER_SIZE) return false;
    int bytes_read = 0;
    // LT模式读数据
    if(0 == m_TRIGMode)
    {
        bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
        if(bytes_read <= 0){
            return false;
        }
        m_read_idx += bytes_read;
        return true;
    }
    // ET模式读数据
    else
    {
        while(true)
        {
            bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
            if(-1 == bytes_read)
            {
                if(errno == EAGAIN || errno == EWOULDBLOCK) break;
                return false;
            }
            // 对端关闭连接
            else if(0 == bytes_read)
            {
                return false;
            }
            m_read_idx += bytes_read;
            return true;
        }
    }
    
}


// 从状态机, 从读缓冲区中读出一行，分析是请求报文的哪一部分
// 返回值为该行的状态，完整行，坏行，不完整行
http_conn::LINE_STATUS http_conn::parse_line()
{
    char tmp;
    // 读出\r\n，Line_OK, 出错 LINE_BAD, 读完了读不到\r\n, LINE_OPEN
    for(; m_checked_idx < m_read_idx; ++m_checked_idx)
    {
        tmp = m_read_buf[m_checked_idx];
        if(tmp == '\r')
        {
            if(m_checked_idx + 1 == m_read_idx) return LINE_OPEN;
            else if(m_read_buf[m_checked_idx + 1] == '\n')
            {
                m_read_buf[m_checked_idx++] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
        else if(tmp == '\n')
        {
            if(m_checked_idx > 1 && m_read_buf[m_checked_idx - 1] == '\r')
            {
                m_read_buf[m_checked_idx - 1] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    return LINE_OPEN;
}


//解析http请求行，获得请求方法，目标url及http版本号
// 请求行组成： 请求方法+分隔符+url+分隔符+http版本号   分隔符为空格或\t
http_conn::HTTP_CODE http_conn::parse_request_line(char *text)
{
    //printf("http_conn:parse_request_line(), text is %s\n", text);
    // strpbrk(s, p) 在s里找p中的任意字符，找到就返回s中包含p中某个字符的开始到结束的字符串
    m_url = strpbrk(text, " \t");
    if(!m_url) return BAD_REQUEST;
    // 剔除掉分隔符 ' ' 或 '\t'， 从分割处截断，这样text就只包含请求方法了
    *m_url++ = '\0';
    // 获取请求方法
    char* method = text;
    //printf("http_conn:parse_request_line(), method is %s\n", method);
    if(strcasecmp(method, "GET") == 0) m_method = GET;
    else if(strcasecmp(method, "POST") == 0) 
    {
        m_method = POST;
        cgi = 1;
    }
    else return BAD_REQUEST;

    // 这里是防止出现多个空格或者tab，所以把url移动到第一个非空格或者tab
    m_url += strspn(m_url, " \t");
    
    // 提取出http版本号
    m_version = strpbrk(m_url, " \t");
    //printf("http_conn:parse_request_line(), m_version is %s\n", m_version);
    if(!m_version) return BAD_REQUEST;
    // 截断url
    *m_version++ = '\0';
    m_version += strspn(m_version, " \t");

    // 只支持http/1.1
    if(strcasecmp(m_version, "http/1.1") != 0) return BAD_REQUEST;

    // 检查url是否合法, 把其中的资源路径提取出来
    if(strncasecmp(m_url, "http://", 7) == 0)
    {
        m_url += 7;
        // 跳到url中第一个'/'那里
        m_url = strchr(m_url, '/');
    }
    else if (strncasecmp(m_url, "https://", 8) == 0)
    {
        m_url += 8;
        m_url = strchr(m_url, '/');
    }
    //printf("http_conn:parse_request_line(), m_url is %s\n", m_url);
    if(!m_url || m_url[0] != '/') return BAD_REQUEST;
    // 当url为/时，显示index界面
    if(strlen(m_url) == 1) strcat(m_url, "judge.html");

    // 状态转移
    m_check_state = CHECK_STATE_HEADER;
    return NO_REQUEST;
}


// 解析HTTP请求的一个头部信息， 会被多次调用，每次解析一个字段
// 解析的字段有Connection、 Content-Length、 Host 三个
// 其他字段忽略掉， 不处理
http_conn::HTTP_CODE http_conn::parse_headers(char* text)
{
    // 得到一个空行表示头部解析完了，头部和消息体就是用空行\r\n隔开的
    if(text[0] == '\0')
    {
        if(m_content_length != 0)
        {
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        return GET_REQUEST;
    }
    else if(strncasecmp(text, "Connection:", 11) == 0)
    {
        text += 11;
        text += strspn(text, " \t");
        if(strcasecmp(text, "keep-alive") == 0)
        {
            m_linger = true;
        }
    }
    else if(strncasecmp(text, "Content-Length:", 15) == 0)
    {
        text += 15;
        text += strspn(text, " \t");
        m_content_length = atol(text);
    }
    else if(strncasecmp(text, "Host:", 5) == 0)
    {
        text += 5;
        text += strspn(text, " \t");
        m_host = text;
    }
    else
    {
        LOG_INFO("opp! unknow header: %s", text);
    }
    return NO_REQUEST;
}


//判断http请求是否被完整读入
http_conn::HTTP_CODE http_conn::parse_content(char *text)
{
    // 完整读入
    if(m_read_idx >= (m_checked_idx + m_content_length))
    {
        text[m_content_length] = '\0';
        // POST请求中最后为输入的用户名和密码(在content里)
        m_string = text;
        return GET_REQUEST;
    }
    return NO_REQUEST;
}


// 主状态机。 分析HTTP请求的入口函数
http_conn::HTTP_CODE http_conn::process_read()
{
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    char* text = 0;
    // 从读缓冲区读出所有完整行
    // 状态是CHECK_STATE_CONTENT时就不需要从解析该行数据了
    while((m_check_state == CHECK_STATE_CONTENT && line_status == LINE_OK)
            || (line_status = parse_line()) == LINE_OK)
    {
        text = get_line();   // 取出一行数据
        m_start_line = m_checked_idx;   // 指向下一行的起始位置
        //printf("http_conn:process_read(), text is %s\n", text);
        switch (m_check_state)
        {
            case CHECK_STATE_LINE:
            {
                ret = parse_request_line(text);
                if(ret == BAD_REQUEST) return ret;
                break;
            }
            case CHECK_STATE_HEADER:
            {
                ret = parse_headers(text);
                if(ret == GET_REQUEST) return do_request();
                break;
            }
            case CHECK_STATE_CONTENT:
            {
                ret = parse_content(text);
                if(ret == GET_REQUEST) return do_request();
                line_status = LINE_OPEN;
                break;
            }
        
            default:
                return INTERNAL_ERROR;
        }
    }
    
    // 若没有读到一个完整的行，则表示还需要继续读取客户数据才能进一步分析
    if(line_status == LINE_OPEN) return NO_REQUEST;
    return BAD_REQUEST;
}


// 当得到一个完整、正确的HTTP请求时，我们就分析目标文件属性
// 如果目标文件存在，对所有文件可读，且不是目录
// 则使用mmap将其映射到内存地址m_file_address处，并告诉调用者获取文件成功
http_conn::HTTP_CODE http_conn::do_request()
{
    // 记录请求资源路径
    strcpy(m_read_file, doc_root);
    int len = strlen(doc_root);
    //printf("in do_request, m_url is %s\n", m_url);
    // 从右往左找到url第一个'/'位置
    const char* p = strrchr(m_url, '/');
    // 登录判断   这个字段是自己页面跳转设置的
    if(cgi == 1 && (*(p + 1) == '2' || *(p + 1) == '3'))
    {
        //根据标志判断是登录检测还是注册检测
        char flag = m_url[1];

        // 资源绝对路径
        char* m_url_real = (char*)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/");
        strcat(m_url_real, m_url + 2);
        strncpy(m_read_file + len, m_url_real, FILENAME_LEN - len - 1);
        free(m_url_real);

        // 将用户名和密码提取出来
        // user=123&passwd=123   // post请求传过来被接收的数据
        //printf("in do_request m_string is %s\n", m_string);
        char name[100], passwd[100];
        int i;
        for(i = 5; m_string[i] != '&'; ++i)
        {
            name[i - 5] = m_string[i];
        }
        name[i - 5] = '\0';
        int j = 0;
        for(i = i + 10; m_string[i] != '\0'; )
        {
            passwd[j++] = m_string[i++];
        }
        passwd[j] = '\0';

        // 3 注册页面
        if(*(p + 1) == '3')
        {
            // 重名，返回注册错误页面
            if(users.find(name) != users.end())
            {
                strcpy(m_url, "/registerError.html");
            }

            // 没重名的就插入数据库
            else
            {
                char* sql_insert = (char*)malloc(sizeof(char) * 200);
                strcpy(sql_insert, "insert into user(username, passwd) values('");
                strcat(sql_insert, name);
                strcat(sql_insert, "','");
                strcat(sql_insert, passwd);
                strcat(sql_insert, "')");

                m_lock.lock();
                int res = mysql_query(mysql, sql_insert);
                users.insert(pair<string, string>(name, passwd));
                m_lock.unlock();
                // printf("in /0 res is %d\n", res);
                if(!res) strcpy(m_url, "/log.html");
                else strcpy(m_url, "/registerError.html");
            }

        }
        // 2 登录界面
        else if(*(p + 1) == '2')
        {
            // 密码正确，登陆成功
            if(users.find(name) != users.end() && users[name] == passwd)
            {
                strcpy(m_url, "/welcome.html");
            }
            // 密码错误
            else strcpy(m_url, "/logError.html");

        }
    }

    // 注册页面
    if(*(p + 1) == '0')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/register.html");
        strncpy(m_read_file + len, m_url_real, strlen(m_url_real));
        free(m_url_real);
    }
    // 登录页面
    else if (*(p + 1) == '1')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/log.html");
        strncpy(m_read_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    // 图片页面
    else if (*(p + 1) == '5')
    {
        //printf("picture!!!!\n");
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/picture.html");
        strncpy(m_read_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    // 视频页面1
    else if (*(p + 1) == '6')
    {
        //printf("video!!!!\n");
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/video.html");
        strncpy(m_read_file + len, m_url_real, strlen(m_url_real));
        free(m_url_real);
    }
    // 视频页面2
    else if (*(p + 1) == '7')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/video2.html");
        strncpy(m_read_file + len, m_url_real, strlen(m_url_real));
        free(m_url_real);
    }
    // 普通资源请求
    else
    {
        strncpy(m_read_file + len, m_url, FILENAME_LEN - len + 1);
        //printf("normal file request, m_read_file is %s\n", m_read_file);
    }
    
    // 下面判断资源是否存在
    if(stat(m_read_file, &m_file_stat) < 0) return NO_RESOURCE;
    // 没有读取权限
    if(!(m_file_stat.st_mode & S_IROTH)) return FORBIDDEN_REQUEST;
    // 请求路径是目录
    if(S_ISDIR(m_file_stat.st_mode)) return BAD_REQUEST;
    
    // 打开文件，通过mmap将文件内存地址映射到m_file_address
    int fd = open(m_read_file, O_RDONLY);
    // m_file_stat.st_size 文件长度
    m_file_address = (char*)mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    // printf("file size is %d !\n", m_file_stat.st_size);
    close(fd);
    return FILE_REQUEST;
}

// 解除映射
void http_conn::unmap()
{
    if(m_file_address)
    {
        munmap(m_file_address, m_file_stat.st_size);
        m_file_address = 0;
    }
}


bool http_conn::write()
{
    int tmp = 0;
    // 没数据发送，重新注册读事件，初始化连接，等待请求
    if(bytes_to_send == 0)
    {
        modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);
        init();
        return true;
    }
    //time_t start = time(NULL);
    while(true)
    {
        tmp = writev(m_sockfd, m_iv, m_iv_count);
        if(tmp <= -1)
        {
            // 如果tcp写缓冲区没有空闲，则等待下一轮EPOLLOUT事件
            // 虽然在此期间，服务器无法立即接收同一客户的下一个请求
            // 但可以保证连接的完整性
            if(errno == EAGAIN)
            {
                modfd(m_epollfd, m_sockfd, EPOLLOUT, m_TRIGMode);
                return true;
            }
            unmap();
            return false;
        }
        bytes_to_send -= tmp;
        bytes_have_send += tmp;
        if(bytes_to_send <= 0)
        {
            unmap();
            modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);
            //time_t end = time(NULL);
            //printf("%d\n", end - start);
            if(m_linger){
                init();
                return true;
            }
            return false;
        }

        // 当数据没一次性写完时，需要调整m_iv的基址和长度
        // 不然将导致下次还是发送原有的数据
        // printf("tmp %d; to %d; have %d\n", tmp, bytes_to_send, bytes_have_send);
        if (bytes_have_send >= m_iv[0].iov_len)
        {
            m_iv[0].iov_len = 0;
            m_iv[1].iov_base = m_file_address + (bytes_have_send - m_write_idx);
            m_iv[1].iov_len = bytes_to_send;
        }
        else
        {
            m_iv[0].iov_base = m_write_buf + bytes_have_send;
            m_iv[0].iov_len = m_iv[0].iov_len - bytes_have_send;
        }

    }
}

// 往写缓冲区写入一个格式化字符串（待发送的数据）
bool http_conn::add_response(const char *format, ...)
{
    if(m_write_idx >= WRITE_BUFFER_SIZE) return false;
    // 可变参数
    va_list arg_list;
    va_start(arg_list, format);

    int len = vsnprintf(m_write_buf + m_write_idx, WRITE_BUFFER_SIZE - m_write_idx - 1,
                        format, arg_list);
    
    if(len >= (WRITE_BUFFER_SIZE - m_write_idx - 1))
    {
        va_end(arg_list);
        return false;
    } 
    m_write_idx += len;
    va_end(arg_list);
    return true;
}

// 响应行   HTTP协议号 + 状态码 + 状态描述
bool http_conn::add_status_line(int status, const char* title)
{
    return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}

// 响应头
bool http_conn::add_headers(int content_len)
{
    return add_content_length(content_len) && add_content_type() 
            && add_linger() && add_blank_line();
}

// 响应信息
bool http_conn::add_content(const char* content)
{
    return add_response("%s", content);
}

// 消息体长度
bool http_conn::add_content_length(int content_len)
{
    return add_response("Content-Length:%d\r\n", content_len);
}

// 消息体类型
bool http_conn::add_content_type()
{
    return add_response("Content-Type:%s\r\n", "text/html");
}

// 空白行   分隔消息头和消息体
bool http_conn::add_linger()
{
    return add_response("Connection:%s\r\n", (m_linger == true) ? "keep-alive" : "close");
}

bool http_conn::add_blank_line()
{
    return add_response("%s", "\r\n");
}

// 根据服务器处理HTTP请求的结果，决定返回给客户端的内容
// 只是填充响应头而已，不包含请求的资源   
// 分块使用writev发送   m_iv[0] 响应头   m_iv[1] 响应文件内容
bool http_conn::process_write(HTTP_CODE ret)
{
    switch (ret)
    {
    case BAD_REQUEST:
    {
        add_status_line(400, error_400_title);
        add_headers(strlen(error_400_form));
        if(!add_content(error_400_form)) return false;
        break;
    }
    case FORBIDDEN_REQUEST:
    {
        add_status_line(403, error_403_title);
        add_headers(strlen(error_403_form));
        if(!add_content(error_403_form)) return false;
        break;
    }
    case NO_RESOURCE:
    {
        add_status_line(404, error_404_title);
        add_headers(strlen(error_404_form));
        if(!add_content(error_404_form)) return false;
        break;
    }
    case INTERNAL_ERROR:
    {
        add_status_line(500, error_500_title);
        add_headers(strlen(error_500_form));
        if(!add_content(error_500_form)) return false;
        break;
    }
    case FILE_REQUEST:
    {
        add_status_line(200, ok_200_title);
        if(m_file_stat.st_size != 0)
        {
            add_headers(m_file_stat.st_size);
            m_iv[0].iov_len = m_write_idx;
            m_iv[0].iov_base = m_write_buf;
            m_iv[1].iov_len = m_file_stat.st_size;
            m_iv[1].iov_base = m_file_address;
            m_iv_count = 2;
            bytes_to_send = m_write_idx + m_file_stat.st_size;
            return true;
        }
        else
        {
            const char *ok_string = "<html><body></body></html>";
            add_headers(strlen(ok_string));
            if (!add_content(ok_string))
                return false;
        }
    }
    default:
        break;
    }
    m_iv[0].iov_len = m_write_idx;
    m_iv[0].iov_base = m_write_buf;
    m_iv_count = 1;
    bytes_to_send = m_write_idx;
    return true;
}


// 线程池中的工作线程调用，这是处理HTTP请求的入口函数
void http_conn::process()
{
    HTTP_CODE ret = process_read();
    //printf("process(), ret is %d\n", ret);
    if(ret == NO_REQUEST)
    {
        modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);
        return;
    }
    bool write_ret = process_write(ret);
    //printf("process(), write_ret is %d\n", write_ret);
    //printf("%s\n", m_write_buf);
    if(!write_ret) 
    {
        close_conn();
        return;
    }
    modfd(m_epollfd, m_sockfd, EPOLLOUT, m_TRIGMode);
}
