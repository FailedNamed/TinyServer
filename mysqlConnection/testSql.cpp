#include <iostream>
#include "sql_connection_pool.h"

using namespace std;

void select(connection_pool* m_sql_pool)
{
    int row, col;
    MYSQL* conn1 = NULL;
    connRAII *con1 = new connRAII(&conn1, m_sql_pool);
    
    MYSQL_RES *result;    // 指向查询结果的指针
    MYSQL_ROW result_row; // 按行返回的查询信息
    MYSQL_FIELD *field;   // 字段结构指针

    const char* sql = "select username, passwd from user";
    int res1 = mysql_query(conn1, sql);

    //从表中检索完整的结果集
    result = mysql_store_result(conn1);
    
    row = mysql_num_rows(result);
    col = mysql_num_fields(result);

    // 打印字段名
    for(int i = 0; field = mysql_fetch_field(result); ++i)
    {
        cout << field->name << "\t";
    }
    cout << endl;
    
    for(int i = 1; i < row; ++i)
    {
        result_row = mysql_fetch_row(result);
        for(int j = 0; j < col; ++j)
        {
            cout << result_row[j] << "\t";
        }
        cout << endl;
    }
}

void insert(connection_pool* m_sql_pool)
{
    MYSQL* conn1 = NULL;
    connRAII *con1 = new connRAII(&conn1, m_sql_pool);
    const char* sql = "insert into user(username, passwd) values('test', 'test')";
    int res1 = mysql_query(conn1, sql);
    cout << res1 << endl;
}

void deleteSql(connection_pool* m_sql_pool)
{
    MYSQL* conn1 = NULL;
    connRAII *con1 = new connRAII(&conn1, m_sql_pool);
    const char* sql = "delete from user where username = 'test'";
    int res1 = mysql_query(conn1, sql);
    cout << res1 << endl;
}

int main()
{
    connection_pool* m_sql_pool = connection_pool::getInstance();
    m_sql_pool->init("localhost", 3306, "crl", "root", "yourdb", 10, 0);
    
    select(m_sql_pool);
    
    // insert(m_sql_pool);
    deleteSql(m_sql_pool);

    select(m_sql_pool);
    return 0;
}