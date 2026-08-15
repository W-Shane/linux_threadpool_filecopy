#ifndef _MYHEAD_H
#define _MYHEAD_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>       //open的头文件
#include <unistd.h>      //read的头文件
#include <errno.h>       //perror的头文件
#include <linux/input.h> //输入子系统模型有关的头文件
#include <sys/ioctl.h>   //ioctl的头文件
#include <sys/mman.h>    //mmap内存映射
#include <dirent.h>      //目录操作
#include <sys/wait.h>    //wait的头文件
#include <signal.h>      //信号设置阻塞
#include <pthread.h>     //线程有关
#include <sys/socket.h>  //网络编程有关
#include <stdbool.h>     //bool类型

#include <ctype.h>     //验证用户输入、进行文本处理以及字符大小写转换
#include <arpa/inet.h> //地址族（IPv4和IPv6）的地址转换、字节序转换

#include <time.h>
#include <limits.h>

extern void print_tree(const char *path, int depth, bool *prefix); // 树形打印外部声明
extern int tree_entry(int argc, char *argv[]);

// 先放宏定义
//  MAX_DEPTH 宏（树形代码依赖，现在myhead无定义）
#define MAX_DEPTH 16                      // 最大层数
#define MAX_ENTRIES 64                 // 最大条目数
#include "threadpool_module/threadpool.h" // 再引入线程池结构体定义

// 全局进度变量，供copy_main + lvgl_ui访问
extern pthread_mutex_t progress_mutex;

extern off_t total_file_bytes;
extern off_t finished_bytes;
extern double copy_cost_time;

extern int copy_finish_flag; // 新增

// 文件任务数量计数器（用于进度百分比计算）
extern int total_task_cnt;  // 第一轮遍历统计：全部待拷贝文件总数
extern int finish_task_cnt; // 线程每完成1个文件拷贝就+1

// lvgl_ui线程函数声明
extern void *lvgl_thread_run(void *arg);

#endif