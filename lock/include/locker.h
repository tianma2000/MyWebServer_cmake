#pragma once
#include <pthread.h>
#include <semaphore.h>


//定义用来互斥的类

//互斥锁
class locker {
public:
	locker(){
		pthread_mutex_init(&m_mutex, NULL);
	}
	~locker() {
		pthread_mutex_destroy(&m_mutex);
	}
	void lock() {
		pthread_mutex_lock(&m_mutex);
	}
	void unlock() {
		pthread_mutex_unlock(&m_mutex);
	}
	pthread_mutex_t * get(){
		return &m_mutex;
	}
private:
	pthread_mutex_t m_mutex;
};

//条件变量
class cond {
public:
	cond() {
		pthread_cond_init(&m_cond,NULL);
	}
	~cond() {
		pthread_cond_destroy(&m_cond);
	}
	bool wait(pthread_mutex_t *m_mutex)
    {
        int ret = 0;
        //pthread_mutex_lock(&m_mutex);
        ret = pthread_cond_wait(&m_cond, m_mutex);
        //pthread_mutex_unlock(&m_mutex);
        return ret == 0;
    }
	bool timewait(pthread_mutex_t *m_mutex, struct timespec t)
    {
        int ret = 0;
        //pthread_mutex_lock(&m_mutex);
        ret = pthread_cond_timedwait(&m_cond, m_mutex, &t);
        //pthread_mutex_unlock(&m_mutex);
        return ret == 0;
    }
	void signal() {
		pthread_cond_signal(&m_cond);
	}
	void broadcast(){
		pthread_cond_broadcast(&m_cond);
	}
private:
	pthread_cond_t m_cond;
};

//信号量
class sem {
public:
	sem() {
		sem_init(&m_sem, 0, 0);
	}
	sem(int num) {
		sem_init(&m_sem, 0, num);
	}
	~sem() {
		sem_destroy(&m_sem);
	}
	bool wait() {
		sem_wait(&m_sem);
		return true;
	}
	bool post() {
		sem_post(&m_sem);
		return true;
	}
private:
	sem_t m_sem;
};

