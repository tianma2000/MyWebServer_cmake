#pragma once
#include <locker.h>
#include <list>
#include <cstdio>
#include <exception>
#include <sql_connection_pool.h>
//将任务设置为模板
template <typename T>
class threadpool {
public:
	threadpool(connection_pool* connPool,int threadnumber = 8, int max_request = 10000);
	~threadpool();
	bool append(T* request);
private:
	static void* work(void* arg);//工作线程
private:
	locker m_queuelocker;//互斥锁
	sem m_queuesem;//信号量
	pthread_t* m_threads;//线程数组
	int m_max_request;//最大任务量
	std::list<T*> m_workqueue;//请求队列
	int m_threadnumber;//线程的数量
	bool m_shotdown;  //是否关闭线程池
	connection_pool * m_connPool;   //数据库
};
template <typename T>
threadpool<T>::threadpool(connection_pool * connPool,int threadnumber, int max_requst) :m_connPool(connPool),m_threadnumber(threadnumber), m_max_request(max_requst),
m_shotdown(false), m_threads(NULL) {
    if((m_threadnumber<=0)||(max_requst<=0)){
        throw std::exception();
    }
	m_threads = new pthread_t[m_threadnumber];
    if(!m_threads){
        throw std::exception();
    }
	//创建线程并设置为线程分离
	for (int i = 0; i < m_threadnumber; i++) {
        printf("正在创建线程%d\n",i);
        if(pthread_create(m_threads + i, NULL, work, this)!=0){
            delete [] m_threads;
            throw std::exception();
        }
		if(pthread_detach(m_threads[i])){
            delete [] m_threads;
            throw std::exception();
        }
	}
};

template <typename T>
threadpool<T>::~threadpool() {
	delete[] m_threads;
	m_shotdown = true;
}

//往线程池的任务队列中添加任务
template <typename T>
bool threadpool<T>::append(T* request) {
    m_queuelocker.lock();
	if (m_workqueue.size() >= m_max_request) {
		printf("任务队列已满\n");
        m_queuelocker.unlock();
		return false;
	}
	m_workqueue.push_back(request);
	m_queuelocker.unlock();
    m_queuesem.post();
	return true;
}
template <typename T>
void* threadpool<T>::work(void* arg) {
	threadpool* pool = (threadpool*)arg;

	while (!pool->m_shotdown) {
		//从任务队列取任务去执行，需要互斥	
		pool->m_queuesem.wait();
		pool->m_queuelocker.lock();
		if (pool->m_workqueue.size() == 0) {
			pool->m_queuelocker.unlock();
			continue;
		}
		T* request = pool->m_workqueue.front();
		pool->m_workqueue.pop_front();
		pool->m_queuelocker.unlock();
		//执行任务
        if(!request){
            continue;
        }
		connectionRAII mysqlcon(&request->mysql, pool->m_connPool);//更改
		request->process();
	}
    return pool;
}



