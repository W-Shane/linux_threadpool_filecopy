关于开发板进度条 回退 / 负数 等一些问题

1.首先：任务总数统计时机错误
原来：边遍历投递拷贝任务，边累加总任务数
	前期已投递一部分文件开始拷贝，线程已经执行完成，分母（总文件数）还在持续上涨
	进度公式：进度 = 已完成任务数 / 总任务数 * 100％
	导致分子暂时不该变，分母变大，百分比数值回退，界面进度条往后回退。后期多线程并发计数争抢，计数器被脏数据覆盖就会变成负数。
改进：分段执行
	第一阶段：纯遍历源目录，只统计全部待拷贝文件总数，不投递任务，确定分母
	第二阶段：再次遍历目录投递拷贝任务，分母固定不变
Myhead.h文件
// 文件任务数量计数器（用于进度百分比计算）
extern int total_task_cnt;  // 第一轮遍历统计：全部待拷贝文件总数
extern int finish_task_cnt; // 线程每完成1个文件拷贝就+1

2.多线程未加互斥锁，共享计数变量数据错乱
原来：已完成文件数，总文件数是多个子线程共同读写的全局变量，没有pthread_mutex互斥锁保护
	多个线程同时改写同一个整型变量
改进：
copy_main.c
先：新增函数
// --------------------只统计总字节，不创建任务、不拷贝【新增函数】

然后：
void scan_and_dispatch(const char *src_dir, const char *dst_dir, const char *filter_suffix)
删掉以下
            // // 主线程阶段累加总字节数（后续进度百分比分母）
            // pthread_mutex_lock(&progress_mutex);
            // total_file_bytes += st.st_size;
            // pthread_mutex_unlock(&progress_mutex);
            // 堆上分配任务对象（禁止栈上传递，栈内存会失效）

接下来：
case 1:
…
        // 第一步：先完整遍历，一次性算出全部待拷贝总字节（分母固定不变）
        total_file_bytes = 0;
        scan_only_calc_size(argv[1], NULL);
        // 第二步：再遍历投递拷贝任务
        scan_and_dispatch(argv[1], argv[2], NULL); // NULL=不过滤后缀

case 2:
…
        // 第一步：先统计匹配后缀文件总大小，固定分母
        total_file_bytes = 0;
        scan_only_calc_size(argv[1], suffix);
        // 第二步：投递拷贝任务
        scan_and_dispatch(argv[1], argv[2], suffix);

3.LVGL界面刷线频率和线程完成节奏不同步
原来：子线程瞬间批量结束任务，短时间大量更新进度值，来不及渲染

4.边界未做数值钳位
代码没有限制进度区间0 ≤ progress ≤100
改进：
Lvgl_ui.c
        // 限制，防止负数、超过100
        if(percent < 0) percent = 0;
        if(percent > 100) percent = 100;
