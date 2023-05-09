#pragma once
#include <stdio.h>
#include <list>
#include <mysql/mysql.h>
#include <error.h>
#include <iostream>
#include <string>
#include <locker.h>
#include <log.h>
using namespace std;

class connection_pool
{
public:
    MYSQL *getConnection();//获得数据库连接
    bool releaseConnection(MYSQL * conn); //释放连接
    int getFreeConn();   //获得空闲的链接
    void destroyPool();  //销毁数据库连接池

    //单例模式
    static connection_pool * getInstance();
    void init(string url,string user,string password,string dbName,int port,int maxconn,int close_log);
private:
    connection_pool();
    ~connection_pool();

    int m_MaxConn;  //最大连接数
    int m_CurConn;  //当前已经使用的连接池
    int m_FreeConn; //当前空闲的连接池
    locker lock;
    list<MYSQL *> connList;  //连接池
    sem reserve;
public:
    string m_url;   //主机地址
    int m_port;  //数据库登录端口
    string m_user;  //登录数据库用户名
    string m_password;  //登录数据库密码
    string m_dbName;   //使用数据库名
    int m_close_log;  //日志开关 
};


class connectionRAII{
public:
    //双指针对MYSQL *con修改
    connectionRAII(MYSQL** con,connection_pool * connPool);
    ~connectionRAII();
private:
    MYSQL *conRAII;
    connection_pool *poolRAII;
};


