// threadpool.c 推荐顺序
#include "threadpool.h" // 先加载本模块专属头文件，拿到结构体、函数原型
#include "../myhead.h"	// 再加载公共头文件，补系统API（pthread、mkdir、open等）

// ========== 全局变量定义 ==========
struct Task *myhead = NULL;
struct ThreadPool *mypool = NULL;

const int NUMBER = 2; // 管理者线程一次性/批量添加的线程个数

// 【数组队列】 --> 【链表】
ThreadPool *threadPoolCreate(int min, int max, int queueSize) // 最小最大，队列大小
{
	// 创建线程池对象
	ThreadPool *pool = (ThreadPool *)malloc(sizeof(ThreadPool));
	// pool->threadIDs = (pthread_t *)malloc(sizeof(pthread_t) * max);
	int X[4] = {-1, -1, -1, -1}; // 初始化为-1，防止没初始化就被当成"成功"去destroy
	int managerCreated = 0;

	do
	{
		if (pool == NULL)
		{
			printf("线程池创建失败！\n");
			break;
		}

		// 创建线程ID数组
		pool->threadIDs = (pthread_t *)malloc(sizeof(pthread_t) * max); // 按最大线程数分配空间,应对所有线程都在工作的情况
		if (pool->threadIDs == NULL)
		{
			printf("工作线程ID数组创建失败！\n");
			perror("malloc fail");
			// return NULL;  //疑似会泄露pool，改成break（统一由下面清理）
			break;
		}

		memset(pool->threadIDs, 0, sizeof(pthread_t) * max);

		pool->minNum = min;	 // 线程池里面线程的最小个数(只要线程池还在,它里面的线程个数就不能少于这个数,即使刚创建的时候也应该是这样)
		pool->maxNum = max;	 // 线程池里面线程的最大个数
		pool->busyNum = 0;	 // 正在工作的线程为0
		pool->liveNum = min; // 注意它应该和minNum相等,表示当前活着的线程数,也就是马上可以投入工作的线程数
		pool->exitNum = 0;	 // 要杀死的线程个数

		// 初始化互斥锁 和 初始化条件变量
		X[0] = pthread_mutex_init(&pool->mutexPool, NULL);
		X[1] = pthread_mutex_init(&pool->mutexBusy, NULL);
		X[2] = pthread_cond_init(&pool->notFull, NULL);
		X[3] = pthread_cond_init(&pool->notEmpty, NULL);

		int initFail = 0;
		for (int i = 0; i < 4; ++i)
			if (X[i] != 0)
				initFail = 1;
		if (initFail)
		{ // 原来的break只跳出for循环，不会进清理，是bug
			printf("初始化锁和条件变量失败！\n");
			break;
		}

		// 初始化线程池销毁标记
		pool->shutdown = 0;

		pool->taskHead = (Task *)malloc(sizeof(Task));
		if (pool->taskHead == NULL)
		{
			printf("创建任务队列失败！\n");
			perror("malloc fail");
			break;
		}
		pool->taskHead->func = NULL;
		pool->taskHead->taskarg = NULL;
		pool->taskHead->next = NULL;

		pool->queueCapacity = queueSize;
		pool->queueSize = 0;

		// 创建管理者的线程
		// 批量创建N个工作线程 + 单独创建1个管理者线程
		//最后结果会发现threadEixt() 始终被调用比子线程多一次，就是管理者线程（消费者线程)
		//但是最后回收线程会比子线程少一条，
		if (pthread_create(&pool->managerID, NULL, manager, pool) != 0)
		{
			printf("创建管理线程失败！\n");
			break;
		}
		managerCreated = 1; //

		// 创建【最少】的工作的线程
		int flag = 0;
		for (int i = 0; i < min; ++i)
		{
			// 这里将pool作为任务函数的参数，是因为worker工作函数是从链表taskHead中取任务的，而taskHead属于pool这个线程池实例
			if (pthread_create(&pool->threadIDs[i], NULL, worker, pool) != 0)
			{
				printf("线程池创建工作线程失败！\n");
				flag = 1;
				break;
			}
		}
		if (flag == 1)
		{
			break;
		}
		return pool; // 返回【创建成功】的线程池实例对象
	} while (0);

	// ===== 异常清理 =====
	// 执行到这,需要释放资源 --> 先挨个退出（避免join卡死）
	// 注意释放资源的顺序,不能先把pool给释放掉了,每一个"嵌套的"对象都应该从内到外进行"析构"
	if (pool)
	{
		pool->shutdown = 1;
		pthread_cond_broadcast(&pool->notEmpty);
	}

	if (managerCreated)
		pthread_join(pool->managerID, NULL);
	for (int i = 0; i < min; ++i)
	{
		if (pool && pool->threadIDs && pool->threadIDs[i] != 0)
			pthread_join(pool->threadIDs[i], NULL);
	}

	if (pool && pool->taskHead)
	{
		Task *p = pool->taskHead;
		while (p != NULL)
		{
			Task *tmp = p;
			p = p->next;
			free(tmp);
		}
		pool->taskHead = NULL;
	}

	// 销毁已创建成功的锁
	if (pool)
	{
		if (X[0] == 0)
			pthread_mutex_destroy(&pool->mutexPool);
		if (X[1] == 0)
			pthread_mutex_destroy(&pool->mutexBusy);
		if (X[2] == 0)
			pthread_cond_destroy(&pool->notEmpty);
		if (X[3] == 0)
			pthread_cond_destroy(&pool->notFull);
	}

	// pool->threadIDs并不是指针，更是可以直接释放了
	if (pool && pool->threadIDs)
	{
		free(pool->threadIDs);
		pool->threadIDs = NULL;
	}

	// 销毁线程池结构体
	if (pool)
	{
		free(pool);
		pool = NULL;
	}

	return NULL;
}

// ========== 销毁线程池 ==========
int threadPoolDestroy(ThreadPool *pool)
{
	if (pool == NULL)
	{
		return -1;
	}
	// 释放资源前，先“关闭”线程池
	pool->shutdown = 1;

	// 先把所有有效线程ID快照到临时数组，再唤醒
	// 不能边遍历边join，因为worker退出时会把threadIDs[i]清零
	pthread_t *tmpIDs = (pthread_t *)malloc(sizeof(pthread_t) * pool->maxNum);
	int joinCount = 0;
	pthread_mutex_lock(&pool->mutexPool);
	for (int i = 0; i < pool->maxNum; ++i)
	{
		if (pool->threadIDs[i] != 0)
		{
			tmpIDs[joinCount++] = pool->threadIDs[i];
		}
	}
	pthread_mutex_unlock(&pool->mutexPool);

	pthread_cond_broadcast(&pool->notEmpty); // 直接唤醒所用线程，防止隐患

	// 唤醒阻塞的消费者线程（为了释放它们），回收线程
	// 用快照数组逐个join，不受threadIDs被清零影响
	for (int i = 0; i < joinCount; ++i)
	{
		pthread_join(tmpIDs[i], NULL);
		printf("目前我回收的线程是: %lu\n", tmpIDs[i]);
	}
	free(tmpIDs);

	// 回收管理者线程
	pthread_join(pool->managerID, NULL);

	// 释放链表中剩余的节点
	Task *p = pool->taskHead;
	while (p != NULL)
	{
		Task *tmp = p;
		p = p->next;
		free(tmp);
	}

	// 释放互斥锁和条件变量
	pthread_mutex_destroy(&pool->mutexPool);
	pthread_mutex_destroy(&pool->mutexBusy);
	pthread_cond_destroy(&pool->notEmpty);
	pthread_cond_destroy(&pool->notFull);

	// 最后释放线程池本体
	free(pool->threadIDs);
	free(pool);

	return 0;
}

// 为线程池（的任务队列）添加任务 --> 改成链表尾插
void threadPoolAdd(ThreadPool *pool, void (*func)(void *), void *taskarg)
{
	// 由于线程池的任务链表是公共资源，因此需要添加互斥锁
	pthread_mutex_lock(&pool->mutexPool);

	// 判断当前任务链表是否满了且线程池有没有被关闭
	while (!pool->shutdown && pool->queueSize == pool->queueCapacity)
	{
		// 阻塞生产者线程（它的唤醒需要依靠消费者线程，也就是工作线程，看worker函数）
		pthread_cond_wait(&pool->notFull, &pool->mutexPool);
	}
	if (pool->shutdown)
	{
		pthread_mutex_unlock(&pool->mutexPool);
		return;
	}

	// 新建节点，【尾插】到【链表末尾】
	Task *newTask = (Task *)malloc(sizeof(Task));
	if (newTask == NULL)
	{
		pthread_mutex_unlock(&pool->mutexPool);
		return;
	}
	newTask->func = func;
	newTask->taskarg = taskarg;
	newTask->next = NULL;

	Task *p = pool->taskHead;
	while (p->next != NULL)
		p = p->next;   // 找到最后一个节点
	p->next = newTask; // 尾插

	pool->queueSize++;

	// 还有一件事要做，生产者需要唤醒阻塞在条件变量上的那些工作的线程
	// 当生产者生产产品后就需要告诉（唤醒）消费者，唤醒一个空闲worker
	pthread_cond_signal(&pool->notEmpty); // 注意这里的条件变量是pool->notEmpty

	pthread_mutex_unlock(&pool->mutexPool);
}

int threadPoolBusyNum(ThreadPool *pool)
{
	// 记得加锁(当然也可以加mutexPool这把锁，不过这样的话效率太低！)
	pthread_mutex_lock(&pool->mutexBusy);
	int busyNum = pool->busyNum;
	pthread_mutex_unlock(&pool->mutexBusy);

	return busyNum;
}

int threadPoolAliveNum(ThreadPool *pool)
{
	// 记得加锁(也可以给pool->liveNum单独配一把锁，而本线程池没配，因为没有太大的必要)
	pthread_mutex_lock(&pool->mutexPool);
	int liveNum = pool->liveNum;
	pthread_mutex_unlock(&pool->mutexPool);

	return liveNum;
}

void *worker(void *taskarg)
{
	// 先将传入进来的参数进行类型转换（因为为了普适性传入的是void* 类型，而实际传入的是线程池指针类型）
	ThreadPool *pool = (ThreadPool *)taskarg;

	// 每个线程的工作函数（消费者）一直尝试读任务链表taskHead。什么情况跳出，需要根据实际情况。
	// 由于每个线程都需要对任务链表taskHead进行操作（不只是读），而taskHead属于同一个线程池实例对象pool，并且对链表操作不仅会改变taskHead本身，也会改变线程池里面的内容，因此会出现竞争的情况，所以得对线程池pool加锁
	while (1)
	{
		pthread_mutex_lock(&pool->mutexPool); // 给线程池加互斥锁，一次只能有一个线程对线程池进行操作
		// 判断当前任务队列是否为空且线程池有没有被关闭
		while (!pool->shutdown && pool->queueSize == 0)
		{ // 防止伪唤醒
			// 阻塞工作线程
			// 注意，当它被唤醒的时候，它会尝试获得锁。也就是说，可能会唤醒多个线程，但只有一个线程能拿到锁，并继续执行。其他没拿到锁的但被唤醒的线程将继续等待。
			pthread_cond_wait(&pool->notEmpty, &pool->mutexPool); //  pthread_cond_wait 函数在等待条件变量变量的时候，并在等待期间释放互斥锁。

			if (pool->exitNum > 0)
			{
				pool->exitNum--; // 注意它不能放到下面的if代码块去
				if (pool->liveNum > pool->minNum)
				{ // 只有满足这个条件才真正销毁线程
					pool->liveNum--;
					pthread_mutex_unlock(&pool->mutexPool); // 注意在让当前线程退出（自杀）之前，需要让它接除（交出）它获得的锁，否则就造成死锁了，没线程可解
					threadExit(pool);						// 让【管理者线程唤醒】的阻塞在上面pool->notEmpty的线程自杀，并且将其对应threadIDs里面的位置重置为0
				}
			}
		}

		// 判断线程池是否被关闭了
		if (pool->shutdown)
		{
			pthread_mutex_unlock(&pool->mutexPool); // 解锁，这里解锁的原因是为了避免死锁。因为如果线程池关闭了，而当前线程已经执行了加锁操作了，但是后面的操作由于线程池关闭已经不能正常进行了，也就不能正常解锁，因此要在这里把锁解开
			threadExit(pool);
			// pthread_exit(NULL); // 退出当前线程，当线程调用 pthread_exit 后，它会立即终止当前线程的执行，而不会影响其他线程的运行。会在线程退出之前清理线程相关的资源。
			//  这里也可以直接调用pthread_exit(NULL);因为线程池都被关闭了，threadIDs里面的位置是否重置为0已经不重要了
		}

		// 开始消费（从任务链表中取出一个任务）
		Task *taskNode = pool->taskHead->next; // 取出头结点后面的第一个节点（真正存任务的节点）
		if (taskNode == NULL)				   // 【新增】防御性检查，防止队列为空时取任务
		{
			pthread_mutex_unlock(&pool->mutexPool);
			continue;
		}
		Task task;						  // 将要执行的任务
		task.func = taskNode->func;		  // 从线程池任务链表头节点取任务函数
		task.taskarg = taskNode->taskarg; // 从线程池任务链表头节点取任务参数

		// 移动链表头结点的next指针，跳过被取出的节点 --> 类似于数组队列中出队
		pool->taskHead->next = taskNode->next;
		// 修改线程池关于维护任务队列的元素
		pool->queueSize--;
		// 【链表】释放被取出的节点，防止内存泄露
		// 【数组】空间可以服用
		free(taskNode);
		taskNode = NULL;

		pthread_cond_signal(&pool->notFull); // 当消费者消费产品后就需要告诉（唤醒）生产者

		// 以上对线程池关于任务队列的操作就算完毕了
		pthread_mutex_unlock(&pool->mutexPool); // 解锁

		// ----------------但是别忘了线程池的其他元素也需要时刻考虑到---------------------
		// 因为虽然每次只允许一个线程从任务队列中取任务，在本设计中也就是每次只允许一个线程操作线程池
		// 但是如果任务队列中有多个任务待做，那么将会有多个线程同时执行到这里（上一个拿到锁后取完，下一个拿到锁继续取，取完就立马都到了这里准备执行）
		// 因此，需要考虑多个线程的pool->busyNum的竞争问题
		pthread_mutex_lock(&pool->mutexBusy);
		printf("Child thread %ld starts working...\n", pthread_self());
		pool->busyNum++;
		pthread_mutex_unlock(&pool->mutexBusy);

		// 在当前线程中执行任务
		task.func(task.taskarg);
		//(*task.func)(task.taskarg); // 用函数指针调用函数的时候，*号加多少个都无所谓，因为(*f)()和f()的效果一样，所以(**f)()的效果等于(*f)()的效果等于f()的效果，依此类推
		// 本线程池设计的时候，任务函数的传入参数需要是一块堆内存，以保证它不会在某处被意外释放
		// 因此执行完任务后，需要释放掉传入参数的这块堆内存，避免内存泄露
		// free(task.taskarg);
		// task.taskarg = NULL;

		pthread_mutex_lock(&pool->mutexBusy);
		printf("子线程 %ld 结束工作！\n", pthread_self());
		pool->busyNum--;
		pthread_mutex_unlock(&pool->mutexBusy);
	}

	return NULL;
}

void *manager(void *taskarg)
{
	// 先将传入进来的参数进行类型转换（因为为了普适性传入的是void* 类型，而实际传入的是线程池指针类型）
	ThreadPool *pool = (ThreadPool *)taskarg;

	while (!pool->shutdown)
	{
		// 每隔3秒钟检测一次
		sleep(3);

		// 取出线程池中任务的数量和当前线程的数量
		pthread_mutex_lock(&pool->mutexPool); // 加锁（pool->mutexPool，锁线程池的那把锁）
		int queueSize = pool->queueSize;
		int liveNum = pool->liveNum;
		pthread_mutex_unlock(&pool->mutexPool); // 解锁

		// 需要取出工作线程（忙线程）的数量
		pthread_mutex_lock(&pool->mutexBusy); // 加锁（pool->mutexBusy，锁忙线程数量的那把锁）
		int busyNum = pool->busyNum;
		pthread_mutex_unlock(&pool->mutexBusy);

		// 添加线程（根据业务逻辑修改添加线程的策略，并没有一个统一的标准）
		if (queueSize > liveNum - busyNum && liveNum < pool->maxNum)
		{										  // 注意这里不能是pool->queueSize > pool->liveNum，否则逻辑就有问题了
			pthread_mutex_lock(&pool->mutexPool); // 在这里【加锁】的原因是下面for循环中也操作了线程池的变量pool->liveNum
			int counter = 0;					  // 这个放上面也没问题，因为它不是共享资源
			for (int i = 0; i < pool->maxNum && counter < NUMBER && pool->liveNum < pool->maxNum; ++i)
			{ // 注意实际添加过程中线程的个数可能超出最大线程数，因此也要判断liveNum < pool->maxNum
				if (pool->threadIDs[i] == 0)
				{ // 这也是为什么循环从0到pool->maxNum的原因，为的是找出空闲的线程ID
					pthread_create(&pool->threadIDs[i], NULL, worker, pool);
					counter++;
					pool->liveNum++; // 注意每创建一个新的线程，活着的线程数就要+1
				}
			}
			pthread_mutex_unlock(&pool->mutexPool); // 解锁
		}

		// 销毁线程（根据业务逻辑修改销毁线程的策略，并没有一个统一的标准）
		if (busyNum * 2 < liveNum && liveNum > pool->minNum)
		{
			pthread_mutex_lock(&pool->mutexPool); // 注意加锁
			pool->exitNum = NUMBER;
			pthread_mutex_unlock(&pool->mutexPool); // 解锁
			// 让空闲的线程自杀
			// 引导它自杀（释放一个信号）
			for (int i = 0; i < NUMBER; ++i)
			{
				pthread_cond_broadcast(&pool->notEmpty);
			}
		}
	}
	return NULL;
}

// 此函数的作用是当线程退出时，将对应的threadIDs数组对应的位置重置为0，以便复用
void threadExit(ThreadPool *pool)
{
	pthread_t tid = pthread_self();		  // 获取当前线程ID
	pthread_mutex_lock(&pool->mutexPool); // 加锁，防止多线程并发改threadIDs
	for (int i = 0; i < pool->maxNum; ++i)
	{
		if (pool->threadIDs[i] == tid)
		{
			pool->threadIDs[i] = 0;
			printf("threadEixt() 被调用, %lu 存在！\n", tid);
			break;
		}
	}
	pthread_mutex_unlock(&pool->mutexPool); // 解锁
	pthread_exit(NULL);						// 让当前线程退出
}