#define _DEFAULT_SOURCE

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

//引入 Linux 特定的 limits 定义，确保 PATH_MAX 存在
#include <linux/limits.h>

#include <limits.h>    // 定义 PATH_MAX
// 兜底PATH_MAX，彻底解决未定义报错
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#include <dirent.h>    // 定义 DT_UNKNOWN、DT_DIR、DT_LNK、DT_REG

#include <sys/stat.h>  // lstat、结构体stat
#include <unistd.h>    // lstat、readlink

// MAX_DEPTH：限制递归最大层级，防止超深层目录栈溢出崩溃
#define MAX_DEPTH 64
// MAX_ENTRIES：单目录最大读取文件数，适配大容量项目文件夹
#define MAX_ENTRIES 4096 // 加大，配合快速排序qsort
// 改用快速排序
// 时间复杂度：冒泡O(n²)【数据量少】 --> 快速 O(nlogn)【数据量大】
// 额外空间：快速排序递归调用栈空间 O(logn)（平均），而冒泡排序额外空间为 O(1)

#include "myhead.h" 

// 颜色宏定义
#define COLOR_RESET "\033[0m"
#define COLOR_DIR "\033[1;34m"     // 加粗蓝色 --> 目录
#define COLOR_FILE "\033[0m"       // 默认色   --> 普通文件
#define COLOR_EXEC "\033[1;32m"    // 加粗绿色 --> 可执行文件
#define COLOR_LINK "\033[1;36m"    // 加粗青色 --> 软链接
#define COLOR_SPECIAL "\033[1;33m" // 黄色     --> 其他特殊文件

// 新增：全局统计计数器
// 遍历全程累加目录 --> 文件总数量 -->最后输出汇总统计行
static int g_dir_cnt = 0;
static int g_file_cnt = 0;

void print_tree(const char *path, int depth, bool *prefix);

// 目录条目（拷贝自 dirent，避免 readdir 复用缓冲区的坑）
// 原生readdir存在缓冲区复用问题：循环中p指针内容会被覆盖
// 解决方案：单独创建struct entry拷贝文件名+文件类型，不受缓冲区覆盖影响
struct entry
{
    char name[256];     // 存储文件/目录名
    unsigned char type; // 存储文件类型DT_DIR/DT_REG/DT_LNK
};

// // 用于 qsort（快速排序） 比较：目录优先，同类按字母序
// static int cmp_dirent(const void *a, const void *b)
// {
//     const struct dirent *const *da = (const struct dirent *const *)a;
//     const struct dirent *const *db = (const struct dirent *const *)b;
//     // 注意：这里只按名字排，目录/文件混排更自然；
//     //       若想"目录在前"，需要额外传入 type 信息，此处从简
//     return strcmp((*da)->d_name, (*db)->d_name);
// }
// 修改为：qsort 比较函数（按文件名升序，字母顺序）
static int cmp_entry(const void *a, const void *b)
{
    const struct entry *ea = (const struct entry *)a;
    const struct entry *eb = (const struct entry *)b;
    return strcmp(ea->name, eb->name);
}

/*
函数功能：递归遍历目录并打印完整树形结构
核心实现原理：
    1. 先一次性读取当前目录所有文件存入数组，区分最后一个节点，实现├─ / └─连接符
    2. prefix数组：prefix[depth]=true → 当前层级后续还有同级文件，需要持续打印竖线 │
    3. 递归深度depth控制缩进层级，配合prefix数组实现外层竖线连贯不中断（解决你之前树形断层bug）
入参：
    path: 当前遍历目录完整路径
    depth: 当前递归层级，用于缩进控制
    prefix: 布尔数组，记录每一层是否需要绘制向下竖线
            prefix[d]=true 表示第 d 层还需要继续画竖线 "│"

*/

void print_tree(const char *path, int depth, bool *prefix)
{
    // 递归深度超限保护，防止极端深层目录栈溢出
    if (depth >= MAX_DEPTH)
    {
        fprintf(stderr, "警告：目录层级超过最大限制（>%d），停止递归: %s\n", MAX_DEPTH, path);
        return;
    }

    DIR *dir = opendir(path);
    if (dir == NULL)
    {
        perror(path); // 打印无法打开的目录名称+系统错误
        return;
    }

    struct entry entries[MAX_ENTRIES];
    int cnt = 0;

    // 第一步：先把当前目录所有有效条目读入数组，过滤 . 和 .. 避免死递归
    struct dirent *p;
    while ((p = readdir(dir)) != NULL)
    {
        // 跳过当前目录、上级目录两个虚拟节点
        if (strcmp(p->d_name, ".") == 0 || strcmp(p->d_name, "..") == 0)
            continue;
        // 达到单目录最大存储条目则停止读取
        if (cnt >= MAX_ENTRIES)
            break;

        // 拷贝文件名，手动截断防溢出，保证字符串末尾带'\0'
        strncpy(entries[cnt].name, p->d_name, sizeof(entries[cnt].name) - 1);
        entries[cnt].name[sizeof(entries[cnt].name) - 1] = '\0';
        entries[cnt].type = p->d_type;
        cnt++;
    }
    closedir(dir); // 读取完毕关闭目录流，释放资源

    qsort(entries, cnt, sizeof(struct entry), cmp_entry);

    // 循环遍历当前目录下每一个文件/文件夹
    for (int i = 0; i < cnt; i++)
    {
        // 判断当前条目是否为本层最后一个：最后一个用 └──，其余用 ├──
        bool is_last = (i == cnt - 1);

        // 【树形竖线绘制】核心代码（解决外层竖线断裂核心逻辑）
        // 循环上层所有层级，根据prefix数组判断是否打印竖线│
        for (int d = 1; d < depth; d++)
        {
            if (prefix[d])
                printf("│   "); // 这个层后面还有同级文件，持续画竖线延伸
            else
                printf("    "); // 这个层无后续同级，仅空白缩进，不画竖线
        }
        // 当前条目前缀符号(当前节点连接符)
        if (is_last)
            printf("└── ");
        else
            printf("├── ");

        // 拼接完整路径，用于lstat获取详细文件属性、递归子目录
        char full_path[PATH_MAX];
        snprintf(full_path, sizeof(full_path), "%s/%s", path, entries[i].name);

        // 大优化：兼容特殊文件系统bug【优化点】
        // 用 lstat 拿真实类型（能识别软链接），d_type 在某些文件系统上是 DT_UNKNOWN，无法区分目录或链接
        // 补充lstat系统调用，精准获取文件真实类型
        struct stat st;
        unsigned char type = entries[i].type;
        if (type == DT_UNKNOWN && lstat(full_path, &st) == 0)
        {
            if (S_ISDIR(st.st_mode))
                type = DT_DIR;
            else if (S_ISLNK(st.st_mode))
                type = DT_LNK;
            else if (S_ISREG(st.st_mode))
                type = DT_REG;
        }

        // 打印文件名+颜色【按类型分颜色】
        switch (type)
        {
        case DT_DIR:
            // 目录打印蓝色，末尾加 / 标识文件夹
            printf(COLOR_DIR "%s/" COLOR_RESET "\n", entries[i].name);
            g_dir_cnt++;                              // 累计目录数
            prefix[depth] = !is_last;                 // 决定下一层是否画竖线（当前不是最后一个就保留竖线）
            print_tree(full_path, depth + 1, prefix); // 递归进入子目录，层级+1
            break;
        case DT_LNK:
            // 软链接青色打印，读取并展示链接指向的目标路径
            printf(COLOR_LINK "%s -> " COLOR_RESET, entries[i].name);
            // 打印链接目标
            {
                char target[PATH_MAX];
                ssize_t n = readlink(full_path, target, sizeof(target) - 1);
                if (n > 0)
                {
                    target[n] = '\0';
                    printf("%s\n", target);
                }
                else
                    printf("\n");
            }
            g_file_cnt++; // 软链接计入文件
            break;
        case DT_REG:
            // 判断是否可执行
            // 普通文件：判断是否拥有执行权限，绿色带*区分可执行程序
            if (lstat(full_path, &st) == 0 && (st.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH)))
                printf(COLOR_EXEC "%s*" COLOR_RESET "\n", entries[i].name);
            else
                printf(COLOR_FILE "%s" COLOR_RESET "\n", entries[i].name);
            g_file_cnt++; // 累计文件数
            break;
        default:
            // 设备文件、管道等特殊文件黄色打印
            printf(COLOR_SPECIAL "%s" COLOR_RESET "\n", entries[i].name);
            g_file_cnt++; // 特殊文件也计入
            break;
        }
    }
}

int tree_entry(int argc, char *argv[])
{
    // 命令行参数校验：仅允许传入1个目录路径参数
    if (argc != 2)
    {
        printf("用法：%s 目录路径\n示例：%s ./testThreadPool\n", argv[0], argv[0]);
        return -1;
    }
    // stat校验传入路径是否存在、是否为合法目录
    struct stat st;
    if (stat(argv[1], &st) != 0)
    {
        perror(argv[1]);
        return 1;
    }
    if (!S_ISDIR(st.st_mode))
    {
        fprintf(stderr, "错误：%s 不是一个目录\n", argv[1]);
        return 1;
    }

    // 先打印根目录名（蓝色高亮区分根节点）
    printf(COLOR_DIR "%s" COLOR_RESET "\n", argv[1]);

    // 初始化层级竖线标记数组，全部初始化为false
    bool prefix_arr[MAX_DEPTH] = {false};
    print_tree(argv[1], 1, prefix_arr);

    // 遍历完成，输出统计汇总行
    printf("\n%d director%s, %d file%s\n",
           g_dir_cnt, g_dir_cnt == 1 ? "y" : "ies",
           g_file_cnt, g_file_cnt == 1 ? "" : "s");

    return 0;
}