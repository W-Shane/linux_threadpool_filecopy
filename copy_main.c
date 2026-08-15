#include "myhead.h"
#include "threadpool_module/threadpool.h"

#include <unistd.h>

// LVGL界面线程入口
extern void *lvgl_thread_run(void *arg);
pthread_t lvgl_tid; // 存放LVGL线程ID

//-----------------全局共享变量（lvgl_ui只读、线程安全）
// 互斥锁：多线程修改进度时防止数据竞争
pthread_mutex_t progress_mutex = PTHREAD_MUTEX_INITIALIZER;
off_t total_file_bytes = 0;  // 所有待拷贝文件总大小（主线程扫描阶段统计）
off_t finished_bytes = 0;    // 已经拷贝完成的字节数（工作线程累加）
double copy_cost_time = 0.0; // 拷贝总耗时，拷贝结束后lvgl读取展示
int copy_finish_flag = 0; // 拷贝完成标志

// 外部函数声明：从另外三个模块引入接口
extern void print_tree(const char *path, int depth, bool *prefix); // tree_module

extern ThreadPool *threadPoolCreate(int min, int max, int queueSize);         // threadpool_module初始化线程池
extern void threadPoolAdd(ThreadPool *pool, void (*func)(void *), void *arg); // 投放拷贝任务
extern int threadPoolBusyNum(ThreadPool *pool);                               // 阻塞等待全部任务结束
extern int threadPoolDestroy(ThreadPool *pool);                               // 销毁线程池

// 全局线程池指针，供 scan_and_dispatch 内部使用
static ThreadPool *g_pool = NULL;

//-----------------拷贝任务结构体：投递进线程池队列
typedef struct
{
    char src_path[PATH_MAX];
    char dst_path[PATH_MAX];
} CopyTask;

//-------------------线程池工作线程执行的单文件拷贝函数
void *single_file_copy(void *arg)
{
    CopyTask *task = (CopyTask *)arg;
    int fd_src = open(task->src_path, O_RDONLY);
    if (fd_src < 0)
    {
        perror("源文件打开失败！");
        free(task);
        return NULL;
    }
    int fd_dst = open(task->dst_path, O_WRONLY | O_CREAT | O_TRUNC, 0664);
    if (fd_dst < 0)
    {
        perror("目标文件打开失败！");
        close(fd_src);
        free(task);
        return NULL;
    }

    char buf[4096];
    ssize_t read_len;
    while ((read_len = read(fd_src, buf, sizeof(buf))) > 0)
    {
        write(fd_dst, buf, read_len);
        // 加锁原子累加已完成字节数，lvgl安全读取
        pthread_mutex_lock(&progress_mutex);
        finished_bytes += read_len;
        pthread_mutex_unlock(&progress_mutex);
    }
    close(fd_src);
    close(fd_dst);
    free(task); // 堆内存释放，避免内存泄漏
    return NULL;
}

// --------------------只统计总字节，不创建任务、不拷贝
void scan_only_calc_size(const char *src_dir, const char *filter_suffix)
{
    DIR *dp = opendir(src_dir);
    if (!dp)
    {
        perror(src_dir);
        return;
    }
    struct dirent *entry;
    while ((entry = readdir(dp)) != NULL)
    {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        char src_full[PATH_MAX];
        snprintf(src_full, PATH_MAX, "%s/%s", src_dir, entry->d_name);
        struct stat st;
        lstat(src_full, &st);
        if (S_ISDIR(st.st_mode))
        {
            scan_only_calc_size(src_full, filter_suffix);
        }
        else if (S_ISREG(st.st_mode))
        {
            // 后缀过滤逻辑和scan_and_dispatch完全一致
            int need_copy = 1;
            if (filter_suffix != NULL)
            {
                int name_len = strlen(entry->d_name);
                int suf_len = strlen(filter_suffix);
                if (name_len < suf_len || strcmp(entry->d_name + name_len - suf_len, filter_suffix) != 0)
                {
                    need_copy = 0;
                }
            }
            if (!need_copy)
                continue;
            // 仅累加总字节，不投递任务
            pthread_mutex_lock(&progress_mutex);
            total_file_bytes += st.st_size;
            pthread_mutex_unlock(&progress_mutex);
        }
    }
    closedir(dp);
}

//------------------递归扫描目录：创建子目录、封装任务投放线程池
void scan_and_dispatch(const char *src_dir, const char *dst_dir, const char *filter_suffix)
{
    DIR *dp = opendir(src_dir);
    if (!dp)
    {
        perror(src_dir);
        return;
    }
    struct dirent *entry;
    while ((entry = readdir(dp)) != NULL)
    {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        char src_full[PATH_MAX], dst_full[PATH_MAX];
        snprintf(src_full, PATH_MAX, "%s/%s", src_dir, entry->d_name);
        snprintf(dst_full, PATH_MAX, "%s/%s", dst_dir, entry->d_name);

        struct stat st;
        lstat(src_full, &st);
        if (S_ISDIR(st.st_mode))
        {
            // 目录统一主线程创建：杜绝多线程并发mkdir报错
            if (mkdir(dst_full, 0755) == -1)
            {
                if (errno != EEXIST)
                {
                    perror("mkdir fail");
                    return;
                }
            }
            scan_and_dispatch(src_full, dst_full, filter_suffix);
        }
        else if (S_ISREG(st.st_mode))
        {
            // 指定后缀过滤逻辑
            int need_copy = 1;
            if (filter_suffix != NULL)
            {
                int name_len = strlen(entry->d_name);
                int suf_len = strlen(filter_suffix);
                if (name_len < suf_len || strcmp(entry->d_name + name_len - suf_len, filter_suffix) != 0)
                {
                    need_copy = 0;
                }
            }
            if (!need_copy)
                continue;

            // // 主线程阶段累加总字节数（后续进度百分比分母）
            // pthread_mutex_lock(&progress_mutex);
            // total_file_bytes += st.st_size;
            // pthread_mutex_unlock(&progress_mutex);
            // 堆上分配任务对象（禁止栈上传递，栈内存会失效）

            CopyTask *task = malloc(sizeof(CopyTask));
            if (task == NULL)
            {
                perror("malloc task fail");
                continue;
            }
            strcpy(task->src_path, src_full);
            strcpy(task->dst_path, dst_full);
            threadPoolAdd(g_pool, single_file_copy, task);
        }
    }
    closedir(dp);
}

// 等待全部任务完成的辅助函数
// 全部任务投递完成后立刻进入这里等待，等待结束直接修改copy_cost_time
static void wait_all_tasks_done(void)
{
    while (1) // 循环同时判断【忙线程 + 队列任务总数】，双条件等待
    {
        pthread_mutex_lock(&g_pool->mutexPool);
        int queue_task = g_pool->queueSize;
        pthread_mutex_unlock(&g_pool->mutexPool);

        int busy = threadPoolBusyNum(g_pool);
        if (busy == 0 && queue_task == 0)
            break;
        usleep(50000); // 50ms 轮询一次，CPU 占用极低
    }
}

int main(int argc, char *argv[])
{
    // 用户输入 ./程序名 目录路径 → 只打印目录树，不执行拷贝
    if (argc == 2)
    {
        return tree_entry(argc, argv);
    }
    //--------------- 题目强制参数格式校验：./程序 源目录 目标目录
    if (argc != 3)
    {
        printf("用法1（快捷树形打印）：%s 源目录路径\n用法2（拷贝功能）：%s 源目录路径 目标目录路径\n", argv[0], argv[0]);
        return -1;
    }
    struct stat src_st;
    if (stat(argv[1], &src_st) != 0 || !S_ISDIR(src_st.st_mode))
    {
        printf("第一个参数必须是合法源目录！\n");
        return -1;
    }

    //----------------- 三种功能切换菜单（题目硬性要求）
    int select;
    printf("=====功能选择菜单=====\n");
    printf("1、线程池全量拷贝目录（LVGL进度条+耗时统计）\n");
    printf("2、指定后缀类型文件线程池拷贝\n");
    printf("3、仅打印源目录彩色树形结构（不拷贝）\n");
    printf("请输入选项1/2/3：");
    scanf("%d", &select);

    bool prefix_arr[MAX_DEPTH] = {false};
    clock_t start_time, end_time;
    switch (select)
    {
    case 3:
    {
        // 仅调用tree模块，不启动线程池、不拷贝文件
        char *mock_argv[] = {argv[0], argv[1]};
        tree_entry(2, mock_argv);
    }
    break;
    case 1:
        // 使用真实接口创建线程池 min=4 max=8 queue=1024
        g_pool = threadPoolCreate(4, 8, 1024);
        if (!g_pool)
        {
            printf("线程池创建失败！\n");
            pthread_mutex_destroy(&progress_mutex); // 创建失败前先销毁锁再退出
            return -1;
        }

        // 启动LVGL界面线程
        pthread_create(&lvgl_tid, NULL, lvgl_thread_run, NULL);
        sleep(1); // 等待LVGL初始化完成再开始扫描拷贝

        start_time = clock();
        // 第一步：先完整遍历，一次性算出全部待拷贝总字节（分母固定不变）
        total_file_bytes = 0;
        scan_only_calc_size(argv[1], NULL);
        // 第二步：再遍历投递拷贝任务
        scan_and_dispatch(argv[1], argv[2], NULL); // NULL=不过滤后缀
        wait_all_tasks_done();
        copy_finish_flag = 1; //全部拷贝完成
        end_time = clock();

        sleep(1);

        copy_cost_time = (double)(end_time - start_time) / CLOCKS_PER_SEC;
        threadPoolDestroy(g_pool);
        g_pool = NULL;

        printf("拷贝完成，总耗时：%.2fs\n", copy_cost_time);
        // 拷贝结束打印目标目录树校验完整性
        printf("=====拷贝后目标目录结构=====\n");
        print_tree(argv[2], 1, prefix_arr);
        break;
    case 2:
    {
        char suffix[32];
        printf("请输入要拷贝的文件后缀（例：.c）：");
        scanf("%31s", suffix);

        g_pool = threadPoolCreate(4, 8, 1024);
        if (!g_pool)
        {
            printf("线程池创建失败!\n");
            pthread_mutex_destroy(&progress_mutex); // 创建失败前先销毁锁再退出
            return -1;
        }

        // 启动LVGL界面线程
        pthread_create(&lvgl_tid, NULL, lvgl_thread_run, NULL);
        sleep(1); // 等待LVGL初始化完成再开始扫描拷贝

        start_time = clock();
        // 第一步：先统计匹配后缀文件总大小，固定分母
        total_file_bytes = 0;
        scan_only_calc_size(argv[1], suffix);
        // 第二步：投递拷贝任务
        scan_and_dispatch(argv[1], argv[2], suffix);
        wait_all_tasks_done();
        copy_finish_flag = 1; //全部拷贝完成
        end_time = clock();// 拷贝真正结束立刻计时截止，延时不计入业务耗时

        sleep(5); // 延时，随后销毁线程池

        copy_cost_time = (double)(end_time - start_time) / CLOCKS_PER_SEC;
        threadPoolDestroy(g_pool);
        g_pool = NULL;

        printf("指定类型拷贝完成，总耗时：%.2fs\n", copy_cost_time);
        print_tree(argv[2], 1, prefix_arr);
        break;
    }
    default:
        printf("选项仅支持1/2/3！\n");
    }
    pthread_mutex_destroy(&progress_mutex);
    return 0;
}
