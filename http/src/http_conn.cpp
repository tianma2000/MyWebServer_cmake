#include "http_conn.h"
#include <log.h>
#include <locker.h>
#include <stdio.h>


using namespace std;

int http_conn::m_epollfd = -1;
int http_conn::m_user_count = 0;

map<string,string> users;
locker m_lock;

//定义HTTP响应的一些状态信息
const char* ok_200_title = "OK";
const char* error_400_title = "Bad Request";
const char* error_400_form = "Your request has bad syntax or is inherently impossible to staisfy.\n";
const char* error_403_title = "Forbidden";
const char* error_403_form = "You do not have permission to get file form this server.\n";
const char* error_404_title = "Not Found";
const char* error_404_form = "The requested file was not found on this server.\n";
const char* error_500_title = "Internal Error";
const char* error_500_form = "There was an unusual problem serving the request file.\n";

//网站根目录
const char* doc_root = "/home/zy/WebServerCmake/root";

//设置文件描述符非阻塞
void setnoblock(int fd) {
	int old_flag = fcntl(fd, F_GETFL);
	int new_flag = old_flag | O_NONBLOCK;
	fcntl(fd, F_SETFL, new_flag);
}

//将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
void addfd(int epollfd, int fd, bool oneshot) {
	epoll_event event;
	event.data.fd = fd;
	event.events |= EPOLLIN | EPOLLHUP;
	if (oneshot) {
		event.events |= EPOLLONESHOT;
	}
	epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
	setnoblock(fd);
}

//从epoll删除文件描述符
void removefd(int epollfd, int fd) {
	epoll_ctl(epollfd, EPOLL_CTL_DEL, fd,0);
	close(fd);
}

//修改文件描述符，重置socket上的oneshot事件
void modfd(int epollfd, int fd,int ev) {
	epoll_event event;
	event.data.fd = fd;
	event.events = ev | EPOLLHUP | EPOLLONESHOT;
	epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

http_conn::http_conn()
{
}

http_conn::~http_conn()
{
}

//初始化
void http_conn::init(int fd, sockaddr_in &address) {
	m_address = address;
	m_sockfd = fd;

	//设置端口复用
	int reuse = 1;
	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
	//注册监听事件
	addfd(m_epollfd, fd, true);
	m_user_count++;
	init();
}
void http_conn::init() {
	mysql=NULL;
	m_read_index = 0;
	m_check_state = CHECK_STATE_REQUESTLINE;
	m_start_line = 0;
	m_check_index = 0;
	m_method = GET;
	m_url = 0;
	m_version = 0;
	m_content_length = 0;
	m_linger = false;
	m_host = 0;
	m_string = 0;
	cgi = 0;
	m_file_address = 0;

	bzero(m_read_buf, READ_BUFF_SIZE);
	bzero(m_real_file, REAL_FILE_SIZE);
	bzero(m_write_buff, WRITE_BUFF_SIZE);
	m_write_index = 0;

	bytes_to_send = 0;
	bytes_have_send = 0;
}

//关闭链接
void http_conn::close_conn() {
	if (m_sockfd != -1) {
		removefd(m_epollfd, m_sockfd);
		m_user_count--;
		m_sockfd = -1;
	}
}

//将所有的数据都读完   ET模式的读
bool http_conn::read() {
	if (m_read_index >= READ_BUFF_SIZE) {
		return false;
	}
	int bytes_read = 0;//读到的数据
	while (true)
	{
		bytes_read = recv(m_sockfd, m_read_buf+m_read_index, READ_BUFF_SIZE-m_read_index, 0);
		if (bytes_read == -1) {
			if (errno == EAGAIN || errno == EWOULDBLOCK) {
				//说明没有数据
				break;
			}
			return false;
		}
		else if (bytes_read == 0) {
			//对方关闭链接
			return false;
		}
		m_read_index += bytes_read;
	}
	printf("接收到数据:%s\n", m_read_buf);
	return true;
}

//非阻塞写,writev，写操作不会对m_iv中的两个数据进行任何操作?  bool是否还要继续写
bool http_conn::write() {
int temp = 0;

    if (bytes_to_send == 0)
    {
        modfd(m_epollfd, m_sockfd, EPOLLIN);
        init();
        return true;
    }

    while (1)
    {
        temp = writev(m_sockfd, m_iv, m_iv_count);

        if (temp < 0)
        {
            if (errno == EAGAIN)
            {
                modfd(m_epollfd, m_sockfd, EPOLLOUT);
                return true;
            }
            unmap();
            return false;
        }

        bytes_have_send += temp;
        bytes_to_send -= temp;
        if (bytes_have_send >= m_iv[0].iov_len)
        {
            m_iv[0].iov_len = 0;
            m_iv[1].iov_base = m_file_address + (bytes_have_send - m_write_index);
            m_iv[1].iov_len = bytes_to_send;
        }
        else
        {
            m_iv[0].iov_base = m_write_buff + bytes_have_send;
            m_iv[0].iov_len = m_iv[0].iov_len - bytes_have_send;
        }

        if (bytes_to_send <= 0)
        {
            unmap();
            modfd(m_epollfd, m_sockfd, EPOLLIN);

            if (m_linger)
            {
                init();
                return true;
            }
            else
            {
                return false;
            }
        }
    }
}

//解析HTTP请求，回复响应数据
void http_conn::process() {
	//解析HTTP请求
	printf("解析请求，回复响应\n");
	HTTP_CODE read_ret = process_read();
	//NO_REQUEST，表示请求不完整，需要继续接收请求数据
	if (read_ret == NO_REQUEST) {
		modfd(m_epollfd, m_sockfd, EPOLLIN);
		return;
	}
	printf("准备回复响应数据\n");
	//回复响应数据
	bool write_ret = process_write(read_ret);
	if (!write_ret) {
		close_conn();
	}
	modfd(m_epollfd, m_sockfd, EPOLLOUT);
}

//解析HTTP请求(使用状态机）
http_conn::HTTP_CODE http_conn::process_read() {
	//初始化从状态机状态、HTTP请求解析结果
	LINE_STATUS line_status = LINE_OK;
	HTTP_CODE ret = NO_REQUEST;
	char* text = 0;

	//parse_line为从状态机的具体实现
	while (((m_check_state == CHECK_STATE_CONTENT) && (line_status == LINE_OK)) || (line_status = parse_line()) == LINE_OK) {
		text = get_line();
		m_start_line = m_check_index;
		//printf("获得一行待解析数据：%s\n",text);
		LOG_INFO("%s",text);
		Log::get_instance()->flush();
		switch (m_check_state)
		{
		case CHECK_STATE_REQUESTLINE: 
		{
			//从请求头里分析出数据
			ret = parse_request_line(text);
			if (ret == BAD_REQUEST) {
				return BAD_REQUEST;
			}
			break;
		}
		case CHECK_STATE_HEADER: 
		{
			//从请求首部中分析出数据、加上请求首行的构成完整的数据
			ret = parse_headers(text);
			if (ret == BAD_REQUEST) {
				return BAD_REQUEST;
			}
			//完整解析GET请求后，跳转到报文响应函数
			else if (ret == GET_REQUEST) {
				return do_request();
			}
			break;

		}
		case CHECK_STATE_CONTENT:
		{
			//解析消息体
			ret = parse_content(text);
			//完整解析POST请求后，跳转到报文响应函数
			if (ret == GET_REQUEST) {
				return do_request();
			}
			//解析完消息体之后避免再次进入循环，更新line_status
			line_status = LINE_OPEN;
			break;
		}
		default:
		{
			return INTERNAL_ERROR;
		}
		}
	}
	return NO_REQUEST;
}

//根据解析的HTTP请求结果，响应相应报文
bool http_conn::process_write(http_conn::HTTP_CODE ret) {
	switch (ret) {
	//内部错误500
	case INTERNAL_ERROR:
	{
		//状态行
		add_status_line(500, error_500_title);
		//首部行
		add_headers(strlen(error_500_form));
		if (!add_content(error_500_form)) {
			return false;
		}
		break;
	}
	//找不到资源404
	case BAD_REQUEST:
	{
		//状态行
		add_status_line(404, error_404_title);
		//首部行
		add_headers(strlen(error_404_form));
		if (!add_content(error_404_form)) {
			return false;
		}
		break;
	}
	//资源没有访问权限403
	case FORBIDDEN_REQUEST:
	{
		//状态行
		add_status_line(403, error_403_title);
		//行首部
		add_headers(strlen(error_403_form));
		if (!add_content(error_403_form)) {
			return false;
		}
		break;
	}
	//文件存在，200
	case FILE_REQUEST:
	{
		add_status_line(200, ok_200_title);
		//如果请求的资源存在
		if (m_file_stat.st_size != 0) {
			add_headers(m_file_stat.st_size);
			//第一个iovec指针指向响应报文缓冲区，长度指向m_write_index;
			m_iv_count = 2;
			m_iv[0].iov_base = m_write_buff;
			m_iv[0].iov_len = m_write_index;
			//第二个指针指向m_file_address,长度指向文件大小
			m_iv[1].iov_base = m_file_address;
			m_iv[1].iov_len = m_file_stat.st_size;

			//发送的全部数据为响应报文头部信息和文件大小
			bytes_to_send = m_write_index + m_file_stat.st_size;
			return true;
		}
		else {
			//如果请求的资源大小为0，则返回空白html文件
			const char* ok_string = "<html><body></body></html>";
			add_headers(strlen(ok_string));
			if (!add_content(ok_string)) {
				return false;
			}
		}

	}
	default:
		return false;
	}
	//除FILE_REQUEST状态外，其余状态只申请一个iovec，指向响应报文缓冲区
	m_iv[0].iov_base=m_write_buff;
	m_iv[0].iov_len=m_write_index;
	m_iv_count=1;
	bytes_to_send=m_write_index;   //微信里没有
	return true;
}

//解析出一行
http_conn::LINE_STATUS http_conn::parse_line() {
	char temp;
	for (; m_check_index < m_read_index; m_check_index++) {
		temp = m_read_buf[m_check_index];
		if (temp == '\r') {
			if ((m_check_index + 1) == m_read_index) {
				return LINE_OPEN;
			}
			else if (m_read_buf[m_check_index+1] == '\n') {
				m_read_buf[m_check_index++] = '\0';
				m_read_buf[m_check_index++] = '\0';
				return LINE_OK;
			}
			else{
				return LINE_BAD;
			}
		}
		else if (temp == '\n') {
			if ((m_check_index>1)&&(m_read_buf[m_check_index-1] == '\r')) {
				m_read_buf[m_check_index-1] = '\0';
				m_read_buf[m_check_index++] = '\0';
				return LINE_OK;
			}
			else {
				return LINE_BAD;
			}
		}
	}
	return LINE_OPEN;
}
//分析请求行
http_conn::HTTP_CODE http_conn::parse_request_line(char* text) {
	//请求行中最先含有空格和\t任一字符的位置并返回
	m_url = strpbrk(text, " \t");
	if(!m_url){
		return BAD_REQUEST;
	}
	*m_url++ = '\0';
	char* method = text;
	if (strcasecmp(method, "GET")==0) {
		m_method = GET;
	}else if(strcasecmp(method,"POST")==0){
		m_method=POST;
		cgi=1;
	}
	else {
		return BAD_REQUEST;
	}
	m_version = strpbrk(m_url, " \t");
	if (!m_version) {
		return BAD_REQUEST;
	}
	*m_version++ = '\0';
	if (strcasecmp(m_version, "HTTP/1.1") != 0) {
		return BAD_REQUEST;
	}
	//增加http://的情况
	if (strncasecmp(m_url, "http://", 7) == 0) {
		m_url += 7;
		m_url = strchr(m_url, '/');
	}
	//增加https://的情况
	if (strncasecmp(m_url, "https://", 8) == 0) {
		m_url += 8;
		m_url = strchr(m_url, '/');
	}
	if (!m_url || m_url[0] != '/') {
		return BAD_REQUEST;
	}
	//当url为/时，显示欢迎界面
	if (strlen(m_url) == 1) {
		strcat(m_url, "judge.html");
	}
	//全部都处理完之后改变主状态机的状态
	m_check_state = CHECK_STATE_HEADER;
	return NO_REQUEST;
}
//分析请求首部
http_conn::HTTP_CODE http_conn::parse_headers(char* text) {
	//printf("待解析的请求头数据是:%s\n",text);
	//判断是空行还是请求头
	if (text[0] == '\0') {
		//判断是GET请求还是POST请求
		if (m_content_length != 0) {
			m_check_state = CHECK_STATE_CONTENT;
			return NO_REQUEST;
		}
		return GET_REQUEST;
	}
	//分析请求头连接字段
	else if (strncasecmp(text, "Connection:", 11) == 0) {
		text += 11;
		//跳过空个和\t字符
		text += strspn(text, " \t");
		if (strcasecmp(text, "keep-alive") == 0) {
			//长连接设置为true
			m_linger = true;
		}

	}
	//分析请求头部内容长度字段
	else if (strncasecmp(text, "Content-Length:", 15) == 0) {
		text += 15;
		text += strspn(text, " \t");
		m_content_length = atol(text);
	}
	//分析头部HOST字段
	else if (strncasecmp(text, "Host:", 5) == 0) {
		text += 5;
		text += strspn(text, " \t");
		m_host = text;
	}
	else {
		printf("oop!unkonw header:%s\n", text);
	}
	return NO_REQUEST;
}
//解析请求体
http_conn::HTTP_CODE http_conn::parse_content(char* text) {
	//判断buff中是否读入了请求体
	if (m_read_index >= (m_check_index + m_content_length)) {
		text[m_content_length] = '\0';
		//POST请求中最后为输入的用户名和密码
		m_string = text;
		return GET_REQUEST;
	}
	return NO_REQUEST;
}
//做具体处理
http_conn::HTTP_CODE http_conn::do_request() {
	//初始化的m_real_file赋值为网站根目录
	strcpy(m_real_file, doc_root);
	int len = strlen(doc_root);
	printf("m_url=%s\n",m_url);
	//找到m_url中/的位置   需要找吗？ 按理来说m_url[0]一定是'/';
	const char* p = strrchr(m_url, '/');
	
	//实现登录和注册效验
	if (cgi == 1 && (*(p + 1) == '2' || *(p + 1) == '3')) {
		//根据标志判断是登录检测还是注册检测
		char flag=m_url[1];
		char *m_url_real=(char *)malloc(sizeof(char)*200);
		strcpy(m_url_real,"/");
		strcat(m_url_real,m_url+2);
		strncpy(m_real_file+len,m_url_real,REAL_FILE_SIZE-len-1);
		free(m_url_real);
		//将用户名和密码提取出来
		//user=123&password=123
		char name[100],password[100];
		int i;
		//以&为分隔符，前面的为用户名
		for(i=5;m_string[i]!='&';i++){
			name[i - 5] = m_string[i];
		}
		name[i-5]='\0';
		//以&为分隔符，后面是密码
		int j = 0;
        for (i = i + 10; m_string[i] != '\0'; ++i, ++j)
            password[j] = m_string[i];
        password[j] = '\0';

		printf("--------------------解析出name=%s,password==%s==-------------------------\n",name,password);

		//同步线程登录检测

		//CGI多进程登录检测

		 if (*(p + 1) == '3')
        {
            //如果是注册，先检测数据库中是否有重名的
            //没有重名的，进行增加数据
            char *sql_insert = (char *)malloc(sizeof(char) * 200);
            strcpy(sql_insert, "INSERT INTO webdb(name,password) VALUES(");
            strcat(sql_insert, "'");
            strcat(sql_insert, name);
            strcat(sql_insert, "', '");
            strcat(sql_insert, password);
            strcat(sql_insert, "')");
			printf("sql_insert==%s\n",sql_insert);

            if (users.find(name) == users.end())
            {
				// MYSQL * mysql=NULL;
				// mysql=mysql_init(NULL);
				// mysql=mysql_real_connect(mysql,"localhost","root","1","webdb",3306,NULL,0);
                m_lock.lock();

                int res = mysql_query(mysql, sql_insert);
				printf("res==%d\n",res);
                users.insert(pair<string, string>(name, password));
                m_lock.unlock();

                if (!res)
                    strcpy(m_url, "/log.html");
                else
                    strcpy(m_url, "/registerError.html");
            }
            else
                strcpy(m_url, "/registerError.html");
        }
		//如果是登录，直接判断
        //若浏览器端输入的用户名和密码在表中可以查找到，返回1，否则返回0
        else if (*(p + 1) == '2')
        {
            if (users.find(name) != users.end() && users[name] == password)
                strcpy(m_url, "/welcome.html");
            else
                strcpy(m_url, "/logError.html");
        }
	}
	
	//如果请求资源为/0，表示跳转到注册界面
	if (*(p + 1) == '0') {
		char* m_url_real = (char*)malloc(sizeof(char) * 200);
		strcpy(m_url_real, "/register.html");

		//将网站目录和/register.html进行拼接，更新到m_real_file中
		strncpy(m_real_file + len, m_url_real, strlen(m_url_real));
		printf("m_real_file=%s\n",m_real_file);
		free(m_url_real);
	}
	//如果请求资源为/1，表示跳转到登录界面
	else if (*(p + 1) == '1') {
		char* m_url_real = (char*)malloc(sizeof(char) * 200);
		strcpy(m_url_real, "/log.html");

		//将网站目录和/log.html进行拼接，更新到m_real_file中
		strncpy(m_real_file + len, m_url_real, strlen(m_url_real));
		free(m_url_real);
	}
	//图片界面
	else if (*(p + 1) == '5')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/picture.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
	//视频界面
    else if (*(p + 1) == '6')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/video.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
	//关注页面
    else if (*(p + 1) == '7')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/fans.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
	else {
		//如果以上均不符合，即不是登录和注册，直接将url与网站目录拼接
		//这种情况就是welcome(judge.html)界面，请求服务器上的一个图片
		strncpy(m_real_file + len, m_url, REAL_FILE_SIZE - len - 1);
	}
	//通过stat获取请求资源文件属性，成功则更新到m_file_stat结构体中
	//失败返回NO_RESOURCE,表示资源不存在
	if (stat(m_real_file, &m_file_stat) < 0) {
		return NO_RESOURCE;
	}
	//判断文件属性，是否可读
	if (!(m_file_stat.st_mode & S_IROTH)) {
		return FORBIDDEN_REQUEST;
	}
	//判断是否是文件夹，如果是说明请求有误
	if (S_ISDIR(m_file_stat.st_mode)) {
		return BAD_REQUEST;
	}
	//以只读的方式打开文件，并且映射到内存中，加快读取速度
	int fd = open(m_real_file, O_RDONLY);
	m_file_address = (char*)mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);

	//避免文件描述符的浪费和占用
	close(fd);
	//表示请求文件存在，且可以访问
	return FILE_REQUEST;
}
//添加数据到写缓冲区
bool http_conn::add_response(const char* format, ...) {
	if (m_write_index > WRITE_BUFF_SIZE) {
		return false;
	}
	va_list arg_list;
	va_start(arg_list, format);

	//将数据format从可变参数列表写入到写缓冲中，返回写入的长度
	int len = vsnprintf(m_write_buff+m_write_index,WRITE_BUFF_SIZE-m_write_index-1,format,arg_list);
	//如果写入的数据长度超过缓冲区的剩余容量，则报错
	if (len > WRITE_BUFF_SIZE - m_write_index) {
		va_end(arg_list);
		return false;
	}
	//更新m_write_index位置
	m_write_index += len;
	//清空可变参数列表
	va_end(arg_list);
	LOG_INFO("request:%s", m_write_buff);
	Log::get_instance()->flush();
	return true;
}
//添加状态行
bool http_conn::add_status_line(int status, const char* title) {
	return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}
//添加首部行，具体的请求体的长度，链接状态，空白行
bool http_conn::add_headers(int content_len) {
	return add_content_length(content_len)&&add_linger()&&add_blank_line();
}
bool http_conn::add_content_length(int content_len) {
	return add_response("Content-Length:%d\r\n", content_len);
}
bool http_conn::add_linger() {
	return add_response("Connection:%s\r\n", (m_linger == true) ? "keep-alive" : "close");
}
bool http_conn::add_blank_line() {
	return add_response("%s","\r\n");
}
bool http_conn::add_content_type() {
	return add_response("Content-Type:%s\r\n", "text/html");
}
bool http_conn::add_content(const char* content) {
	return add_response("%s", content);
}

void http_conn::unmap() {
	if (!m_file_address) {
		munmap(m_file_address, m_file_stat.st_size);
		m_file_address = 0;
	}
}

void http_conn::initmysql_result(connection_pool *connPool){
	//先从连接池中取一个连接
	MYSQL *mysql=NULL;
	connectionRAII mysqlcon(&mysql,connPool);

	//在user表中检索username,passwd数据，浏览器端输入
	if(mysql_query(mysql,"select name,password from webdb")){
		LOG_ERROR("SELECT error:%s\n",mysql_error(mysql));
	}

	//从表中检索完整的结果集
	MYSQL_RES *result=mysql_store_result(mysql);

	//返回结果集中的列数
	int num_fields=mysql_num_fields(result);

	//返回所有字段结构的数组
	MYSQL_FIELD *fields=mysql_fetch_fields(result);

	//从结果集中获取下一行，将对应的用户名和密码，存入map中
	while(MYSQL_ROW row=mysql_fetch_row(result)){
		string temp1(row[0]);
		string temp2(row[1]);
		users[temp1]=temp2;
		printf("temp1=%s,temp2=%s\n",temp1,temp2);
	}
}
