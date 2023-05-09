#include <cstdio>
#include <locker.h>
#include <threadpool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>
#include <http_conn.h>
#include <exception>
#include <errno.h>
#include <sys/epoll.h>
#include <lst_timer.h>
#include <signal.h>
#include <assert.h>
#include <log.h>

#define MAX_FD 65535 //最多的文件描述符个数
#define MAX_EVENT_NUMBER 10000  //最大监听的事件的个数
#define TIMESLOT 5
static int pipefd[2];
static sort_timer_lst timer_lst;
static int epollfd=0;

//添加epoll监听事件
extern void addfd(int epollfd, int fd, bool oneshot);

//删除epoll监听事件
extern void removefd(int epollfd, int fd);

//修改文件描述符
extern void modfd(int epollfd,int fd);

//设置文件非阻塞
extern void setnoblock(int fd);

//信号处理函数
void sig_handler(int sig){
	//为保证函数的可重入性，保留原来的errno
	//可重入性表示中断后再次进入该函数，环境变量与之前不同，不会丢失数据
	int save_errno = errno;
	int msg = sig;
	//将信号值从管道写端写入，传输字符类型，而非整型
	send(pipefd[1], (char*)&msg, 1, 0);
	errno = save_errno;
}
//添加监听信号,启动信号处理函数
void addsig(int sig) {
	//创建sigaction结构体变量
	struct sigaction sa;
	memset(&sa, '\0', sizeof(sa));
	//信号处理函数中仅仅发送信号值，不做逻辑处理
	sa.sa_handler = sig_handler;
	sa.sa_flags |= SA_RESTART;
	sigfillset(&sa.sa_mask);
	assert(sigaction(sig, &sa, NULL) != -1);
}

void timer_handler() {
	/*定时处理任务，实际上就是调用tick函数*/
	timer_lst.tick();
	/*因为一次alarm调用只会引起一次SIGALRM信号，所以我们需要重新定时，以不断触发SIGALRM信号*/
	alarm(TIMESLOT);
}

/*定时器回调函数，它删除非活动连接socket上的注册事件，并关闭*/
void cb_func(client_data* user_data) {
	epoll_ctl(epollfd, EPOLL_CTL_DEL, user_data->sockfd, 0);
	assert(user_data);
	close(user_data->sockfd);
	//减少链接数
	http_conn::m_user_count--;
	printf("close fd %d\n", user_data->sockfd);
}

// //初始化数据库连接池
// connection_pool *m_connPool = connection_pool::getInstance();

int main(int argc, char* argv[]) {
	if (argc <= 1) {
		printf("请输入 %s port", basename(argv[0]));
        exit(-1);
	}
	int port = atoi(argv[1]);

	//创建日志对象，管理日志
	Log::get_instance()->init("./runLog", 0, 2000, 800000, 800);


	//初始化数据库连接池
    connection_pool *m_connPool = connection_pool::getInstance();
    m_connPool->init("localhost", "root", "1", "test", 3306, 10, 1);

	//创建线程池    任务为http_conn类,处理用户连接
	threadpool<http_conn>* pool = new threadpool<http_conn>(m_connPool);

	//创建一个数组用于保存用户的信息，处理业务
	http_conn* users = new http_conn[MAX_FD];

	// //初始化数据库连接池
    // connection_pool *m_connPool = connection_pool::getInstance();
    //m_connPool->init("localhost", "root", "1", "test", 3306, 10, 1);

    //初始化数据库读取表
    users->initmysql_result(m_connPool);

	//创建监听socket
	int listenfd = socket(AF_INET, SOCK_STREAM, 0);

	//设置端口复用
	int reuse = 1;
	setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

	//绑定ip地址和端口号
	struct sockaddr_in saddr;
	saddr.sin_family = AF_INET;
	saddr.sin_port = htons(port);
	saddr.sin_addr.s_addr = INADDR_ANY;
	int ret=bind(listenfd, (struct sockaddr*)&saddr, sizeof(saddr));
	if (ret == -1) {
		std::exception();
	}

	//设置监听
	listen(listenfd, 5);

	//创建epoll对象，事件数组
	int epollfd = epoll_create(5);
	epoll_event events[MAX_EVENT_NUMBER];

	ret=socketpair(PF_UNIX,SOCK_STREAM,0,pipefd);
	assert(ret!=-1);
	setnoblock(pipefd[1]);
	addfd(epollfd,pipefd[0],false);

	/*设置信号处理函数*/
	addsig(SIGALRM);
	addsig(SIGTERM);

	client_data * users_timer=new client_data[MAX_FD];
	//超时默认为false
	bool timeout=false;
	alarm(TIMESLOT);/*定时*/



	//将监听文件描述符加入到epoll对象中
	addfd(epollfd, listenfd, false);
	http_conn::m_epollfd = epollfd;

	//循环检查
	while (true) {
		int num = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);
		if (num <= 0 && errno != EINTR) {//是系统中断错误
			printf("epoll_wait failed..\n");
			break;
		}
		//循环遍历就绪事件
		for (int i = 0; i < num; i++) {
			int sockfd = events[i].data.fd;
			if (sockfd == listenfd) {
				struct sockaddr_in addr;
				socklen_t addrlen = sizeof(addr);
				int connfd=accept(sockfd,(struct sockaddr *)&addr,&addrlen);
				if (http_conn::m_user_count >= MAX_FD) {
					//链接已满
					close(connfd);
					continue;
				}
				//将HTTP类中的信息更新。
				users[connfd].init(connfd,addr);

				//定时器类更新
				users_timer[connfd].address=addr;
				users_timer[connfd].sockfd=connfd;

				/*创建定时器，设置回调函数与超时时间，然后绑定定时器与用户数量，最后将定时器添加到链表timer_lst中*/
				util_timer* timer = new util_timer;
				//设置定时器对应的连接资源
				timer->user_data = &users_timer[connfd];
				//设置回调函数
				timer->cb_func = cb_func;
				time_t cur = time(NULL);
				//设置定时器的绝对超长时间
				timer->expire = cur + 3 * TIMESLOT;
				//创建该连接对应的定时器，初始化为前述临时变量
				users_timer[connfd].timer = timer;
				//将定时器添加到定时器容器（升序链表中）
				timer_lst.add_timer(timer);

				//users[connfd].initmysql_result(m_connPool);
			}
			else if (events[i].events & (EPOLLHUP | EPOLLERR | EPOLLRDHUP)) {
				//对方异常或者错误断开
				//users[sockfd].close_conn();
				util_timer *timer=users_timer[sockfd].timer;
				timer->cb_func(&users_timer[sockfd]);  //用定时器来关闭相应的文件描述符
				if(timer){
					timer_lst.del_timer(timer);
				}
			}
			//处理定时器信号
			else if((sockfd==pipefd[0])&&(events[i].events&EPOLLIN)){
				//接收到SIGALRM信号，timeout设置为True；
				int sig;
				char signals[1024];
				ret = recv(pipefd[0], signals, sizeof(signals), 0);
				if (ret == -1) {
					//handle the error
					continue;
				}
				else if (ret == 0) {
					continue;
				}
				else {
					for (int i = 0; i < ret; i++) {
						switch (signals[i]) {
						case SIGALRM:
						{
							/*用timeout变量标记有定时任务需要处理，但不立即处理定时任务。这是因为定时任务的优先级不是很高，我们优先去处理其他更重要的任务*/
							timeout = true;
							//printf("改变 timeout的取值为true\n");
							break;
						}
						case SIGTERM:
						{
							return -1;
						}
						}
					}
				}
			}
			//处理客户连接上接受到的数据
			else if (events[i].events & EPOLLIN) {
				//检测到读事件就绪
				//主线程去读数据，让子线程去处理逻辑

				//创建定时器临时变量，将该连接对应的定时器取出来
				util_timer *timer=users_timer[sockfd].timer;
				if(users[sockfd].read()){
					//若检测到读事件，将事件放入请求队列
                    pool->append(users+sockfd);
					
					//若有数据传输，则将定时器往后延迟3个单位
					//对其在链表上的位置进行调整
					if(timer){
						time_t cur = time(NULL);
						timer->expire = cur + 3 * TIMESLOT;
						LOG_INFO("%s","adjust timer once");
						printf("adjust timer once\n");
						timer_lst.adjust_timer(timer);
					}
                }else{
                    //users[sockfd].close_conn();
					timer->cb_func(&users_timer[sockfd]);
					if(timer){
						timer_lst.del_timer(timer);
					}
                }
			}
			else if (events[i].events & EPOLLOUT) {
				util_timer * timer=users_timer[sockfd].timer;
				//写事件就绪
				if(users[sockfd].write()){
					//若有数据传输，则将定时器往后延迟3个单位
					//对其在链表上的位置进行调整
					if(timer){
						time_t cur = time(NULL);
						timer->expire = cur + 3 * TIMESLOT;
						printf("adjust timer once\n");
						timer_lst.adjust_timer(timer);
					}
				}
				else{
					//服务器端关闭连接，移除对应的定时器
                   //users[sockfd].close_conn(); 
				   timer->cb_func(&users_timer[sockfd]);
				   if(timer){
						timer_lst.del_timer(timer);
				   }
                }
			}
		}
		/*最后处理定时事件，因为I/O事件有更高的优先级。当然，我们这样做将导致定时任务不能精确的按照预期的时间执行*/
		//完成读写事件后，再进行处理
		if(timeout){
			timer_handler();
			timeout=false;
		}
	}
	close(listenfd);
	close(epollfd);
	delete[] users;
	close(pipefd[1]);
	close(pipefd[0]);
	delete[] users_timer;
    delete pool;
	return 0;
}
