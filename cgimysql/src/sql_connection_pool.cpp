#include "sql_connection_pool.h"
using namespace std;

connection_pool::connection_pool(){
    m_CurConn=0;
    m_FreeConn=0;
}

connection_pool* connection_pool::getInstance(){
    static connection_pool connPool;
    return &connPool;
}

//初始化
void connection_pool::init(string url,string user,string password,string dbName,int port,int maxconn,int close_log){
    m_url=url;
    m_port=port;
    m_user=user;
    m_password=password;
    m_dbName=dbName;
    m_close_log=close_log;
    for(int i=0;i<maxconn;i++){
        MYSQL * con=NULL;
        con=mysql_init(con);
        if(con==NULL){
            LOG_ERROR("MYSQL Error");
            exit(1);
        }
        con=mysql_real_connect(con,url.c_str(),user.c_str(),password.c_str(),dbName.c_str(),port,NULL,0);
        if(con==NULL){
            LOG_ERROR("MYSQL Error");
            exit(1);
        }
        connList.push_back(con);
        m_FreeConn++;
    }
    reserve=sem(m_FreeConn);
    m_MaxConn=m_FreeConn;
}

//当有请求可以用的时候，从数据库连接池中返回一个可用连接，更新使用和空闲连接数
MYSQL * connection_pool::getConnection(){
    MYSQL *con=NULL;
    if(0==connList.size()){
        return NULL;
    }
    reserve.wait();
    lock.lock();
    con=connList.front();
    connList.pop_front();
    m_FreeConn--;
    m_CurConn++;
    lock.unlock();
    return con;
}

//释放当前使用的链接（可以使用share_ptr 来做？）
bool connection_pool::releaseConnection(MYSQL *con){
    if(con==NULL){
        return false;
    }
    lock.lock();

    connList.push_back(con);
    m_FreeConn++;
    m_CurConn--;
    lock.unlock();
    reserve.post();
    return true;
}

//销毁线程池
void connection_pool::destroyPool(){
    lock.lock();
    if(connList.size()>0){
        list<MYSQL*>::iterator it;
        for(it=connList.begin();it!=connList.end();it++){
            MYSQL *con=*it;
            mysql_close(con);
        }
        m_CurConn=0;
        m_FreeConn=0;
    }
    lock.unlock();
}

//获得当前空闲的连接数
int connection_pool::getFreeConn(){
    return m_FreeConn;
}

connection_pool::~connection_pool(){
    destroyPool();
}

connectionRAII::connectionRAII(MYSQL **SQL, connection_pool *connPool){
	*SQL = connPool->getConnection();
	
	conRAII = *SQL;
	poolRAII = connPool;
}

connectionRAII::~connectionRAII(){
	poolRAII->releaseConnection(conRAII);
}
    
