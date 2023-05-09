#pragma once
#include <fcntl.h>
#include <sys/epoll.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <sys/uio.h>

#include <sql_connection_pool.h>
#include <map>
class http_conn
{
public:
	//报文的请求方法，本项目只实现了GET,和POST
	enum METHOD{GET=0,POST,HEAD,PUT,DELETE,TRACE,OPTIONS,CONNECT,PATH};
	//主状态机的状态
	enum CHECK_STATE{CHECK_STATE_REQUESTLINE=0,CHECK_STATE_HEADER,CHECK_STATE_CONTENT};
	//报文解析的结果
	enum HTTP_CODE{NO_REQUEST,GET_REQUEST,BAD_REQUEST,NO_RESOURCE,FORBIDDEN_REQUEST,
		FILE_REQUEST,INTERNAL_ERROR,CLOSED_CONNETCTION};
	//从状态机的状态
	enum LINE_STATUS { LINE_OK = 0,LINE_BAD,LINE_OPEN };

public:
	http_conn();
	~http_conn();
	static int m_epollfd;
	static int m_user_count;
	MYSQL * mysql;
	static const int READ_BUFF_SIZE = 2048;//读缓冲区的大小
	static const int WRITE_BUFF_SIZE = 1024;//写缓冲区的大小
	static const int REAL_FILE_SIZE = 200; //用来存文件名
	void init(int fd, sockaddr_in& address);
	void close_conn();
	bool read();      //读入HTTP数据
    bool write();
	void process();    //执行任务，处理业务逻辑。（数据通过请求队列（就是m_read_buf）传递），接下来就是解析数据，返回响应数据

	void initmysql_result(connection_pool *connPool);  //将数据库中的用户名载入到服务器的map中来，map中key为用户名，value为密码

private:
	int m_sockfd;
	sockaddr_in m_address;
	char m_read_buf[READ_BUFF_SIZE];
	int m_read_index;  //已经读入用户读缓冲区的数据的下标的下个位置。
	char m_write_buff[WRITE_BUFF_SIZE]; 
private:
	void init();
	HTTP_CODE process_read(); //解析HTTP请求
	bool process_write(HTTP_CODE ret); //根据服务器处理HTTP请求的结果，回应响应数据
	CHECK_STATE m_check_state;
	int m_start_line;//解析出一行的起始位置
	int m_check_index;//当前正在分析的字符在读缓冲区中的位置
	char* get_line() { return m_read_buf + m_start_line; };
	LINE_STATUS parse_line();  //在HTTP报文中，分析出一行
	HTTP_CODE parse_request_line(char* text);  //解析请求行

	METHOD m_method;  //请求方法
	char* m_url;   //请求url
	char* m_version;  //http版本

	HTTP_CODE parse_headers(char* text);   //解析消息头
	int m_content_length;  //请求体数据长度
	bool m_linger;    //是长连接还是短连接
	char* m_host; //主机名

	HTTP_CODE parse_content(char* text);  //解析请求体
	char* m_string;    //存储请求体

	HTTP_CODE do_request(); //根据分析好的数据，做响应的处理
	char m_real_file[REAL_FILE_SIZE];

	int cgi;  //是否启用POST

	struct stat m_file_stat;  //文件状态
	char* m_file_address;   //文件映射地址

	bool add_response(const char* format, ...);  //添加相应数据到写缓冲区
	int m_write_index;  //写缓冲中待发送的字节数，也就是已经写入的字节数

	bool add_status_line(int stauts,const char* title); //添加状态行(status状态码，title状态描述）
	bool add_headers(int content_len);  //添加首部行  具体的添加文本长度、链接状态和空行
	bool add_content_length(int content_len);
	bool add_linger();
	bool add_blank_line();
	bool add_content_type();  //添加文本类型
	bool add_content(const char* content); //添加文本
	struct iovec m_iv[2];   //用于集中写
	int m_iv_count;   //记录有几块内存用于集中写

	int bytes_to_send;  //需要发送的字节数


	int bytes_have_send;  //已经发送了的数据

	void unmap(); //取消映射
private:
	map<string,string> m_users;
};

