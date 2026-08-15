#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdbool.h>
#include <errno.h>

#ifndef _THREADPOOL_H // 如果没定义才进入
#define _THREADPOOL_H

#define DEFAULT_THREAD_NUM 10 // 每次新增/销毁的线程数
#define MIN_WAIT_TASK_NUM 10  // 触发扩容的任务数阈值

//========== 改用连链表存储 ==========

// 任务链表节点
typedef struct Task
{
	void (*func)(void *); // 函数指针，存放任务
	void *taskarg;		   // 传递给任务的参数
	struct Task *next;	   // 指针域
} Task;

// threadpool.h里只做声明，加extern
extern struct Task *myhead;
extern struct ThreadPool *mypool;

// ========== 结构体定义 ==========

// 线程池结构体
typedef struct ThreadPool
{
	// ===== 改用任务队列【链表】=====
	Task *taskHead;	   // 头结点（不存数据）
	int queueCapacity; // 队列最大容量
	int queueSize;	   // 当前任务个数

	// 工作线程和管理者线程相关【不改变】
	pthread_t managerID;  // 管理者线程ID
	pthread_t *threadIDs; // 线程ID数组（有多个，因此是个数组）

	// 线程池维护了若干个线程，因此线程池被初始化成功之后，线程池里面就有若干个线程，个数可多可少，但也要有个范围
	int minNum;	 // 线程的最小个数
	int maxNum;	 // 线程的最大个数
	int busyNum; // 忙线程（工作线程）的个数（它少了就增加一些，它多了就削减一些）
	int liveNum; // 存活的线程个数（它等于 忙线程个数 + 空闲线程的个数）
	int exitNum; // 要杀死的线程个数（记录它是为了方便后续操作）

	// 锁和条件变量【不改变】
	pthread_mutex_t mutexPool; // 互斥锁，锁整个的线程池
	pthread_mutex_t mutexBusy; // 互斥锁，锁busyNum变量

	pthread_cond_t notFull;	 // 任务队列是不是满了
	pthread_cond_t notEmpty; // 任务队列是不是空了

	// 再添加一个辅助性的成员，帮助判断当前线程池是否在工作
	int shutdown; // 是否需要销毁线程池，销毁为1，不销毁为0

} ThreadPool; // 关键：简写ThreadPool生效

// ========== 函数声明 ==========

// 这里需要用户传入三个参数：线程池里面线程的最大个数和最少个数，以及任务队列的容量
ThreadPool *threadPoolCreate(int min, int max, int queueSize);

// 销毁线程池（释放全部的线程池资源）
int threadPoolDestroy(ThreadPool *pool);

// 给线程池添加任务
void threadPoolAdd(ThreadPool *pool, void (*func)(void *), void *taskarg);

// 获取线程池中工作的线程的个数
int threadPoolBusyNum(ThreadPool *pool);

// 获取线程池中活着的线程的个数
int threadPoolAliveNum(ThreadPool *pool);

///////////////
void *worker(void *arg);
void *manager(void *arg);
void threadExit(ThreadPool *pool);

#endif
