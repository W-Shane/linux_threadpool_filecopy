#include "../myhead.h"
// lvgl_ui子文件夹向上一层回到根目录读取myhead.h

#include "lvgl/lvgl.h"
// #include "lvgl/src/extra/libs/freetype/lv_freetype.h" // 新增freetype接口

#include "lvgl/demos/lv_demos.h"
#include "lv_drivers/display/fbdev.h"
#include "lv_drivers/indev/evdev.h"
// #include "myhead.h" // 引入全局进度变量、互斥锁
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <sys/time.h>
#include <stdio.h>

#define DISP_BUF_SIZE (1024 * 900)

// 全局freetype字体信息
// static lv_ft_info_t chinese_font;
static lv_style_t text_style; // 统一文字样式，绑定中文字体

// 全局UI控件：定时器回调需要访问
lv_obj_t * bar1;       // 进度条(进度条对象的地址)
lv_obj_t * label_info; // 文字标签：百分比+耗时

// 全局UI控件：定时器回调需要访问
lv_obj_t * bar1;       // 进度条
lv_obj_t * label_info; // 文字标签：百分比+耗时

// LVGL定时器回调：每100ms自动刷新进度
static void progress_timer_cb(lv_timer_t * timer)
{
    (void)timer;
    // 加锁读取全局进度（多线程安全）
    pthread_mutex_lock(&progress_mutex);
    off_t total = total_file_bytes;
    off_t done  = finished_bytes;
    double cost = copy_cost_time;
    pthread_mutex_unlock(&progress_mutex);

    // 无文件时不刷新
    if(total <= 0) return;

    // 计算拷贝百分比
    int percent;
    if(copy_finish_flag == 1) {
        // 拷贝全部结束，强制固定100%
        percent = 100;
    } else {
        // 运行中正常计算进度
        percent = (done * 100) / total;
        // 限制，防止负数、超过100
        if(percent < 0) percent = 0;
        if(percent > 100) percent = 100;
    }
    lv_bar_set_value(bar1, percent, LV_ANIM_ON);

    // 拼接文字：当前进度 + 总耗时
    char info_buf[128];
    snprintf(info_buf, sizeof(info_buf), "loading:%d %%  duration:%.2f s", percent, cost);
    lv_label_set_text(label_info, info_buf);
}

/* LVGL运行线程函数，由copy_main创建子线程执行 */
void * lvgl_thread_run(void * arg)
{
    (void)arg;

    // 1.LVGL基础初始化
    lv_init();
    // 显示驱动（fb0固定不变）
    fbdev_init();
    // 触摸事件节点
    evdev_init();
    evdev_set_file("/dev/input/event0");

    static lv_color_t buf[DISP_BUF_SIZE];
    static lv_disp_draw_buf_t disp_buf;
    lv_disp_draw_buf_init(&disp_buf, buf, NULL, DISP_BUF_SIZE);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.draw_buf = &disp_buf;
    disp_drv.flush_cb = fbdev_flush;
    disp_drv.hor_res  = 800;
    disp_drv.ver_res  = 480;
    lv_disp_drv_register(&disp_drv);

    // 触摸屏初始化
    evdev_init();
    static lv_indev_drv_t indev_drv_1;
    lv_indev_drv_init(&indev_drv_1);
    indev_drv_1.type         = LV_INDEV_TYPE_POINTER;
    indev_drv_1.read_cb      = evdev_read;
    lv_indev_t * mouse_indev = lv_indev_drv_register(&indev_drv_1);

    // // 中文字体
    // // 初始化freetype缓存
    // lv_freetype_init(64, 2, 0);

    // // 配置字体路径、字号、样式
    // chinese_font.name   = "/IOT/Fonts/STXINGKA.TTF"; // 开发板绝对路径
    // chinese_font.weight = 28;                        // 文字大小，可自行修改16/24/32
    // // chinese_font.style  = LV_FREETYPE_FONT_STYLE_NORMAL;
    // chinese_font.style = 0; // 0 代表普通样式 (Normal)

    // lv_ft_font_init(&chinese_font); // 加载字体文件

    // 创建样式，绑定中文字体，所有label自动生效
    lv_style_init(&text_style);
    lv_style_set_text_font(&text_style, &lv_font_montserrat_24);
    // lv_style_set_text_font(&text_style, chinese_font.font);
    // lv_style_set_text_color(&text_style, lv_color_white());

    // 2.创建进度条UI
    bar1 = lv_bar_create(lv_scr_act());
    lv_obj_set_size(bar1, 600, 60);
    lv_obj_align(bar1, LV_ALIGN_CENTER, 0, -40); // 居中偏上
    lv_bar_set_range(bar1, 0, 100);
    lv_obj_set_style_anim_time(bar1, 300, LV_STATE_DEFAULT);

    // 3.创建文字标签，放在进度条下方
    label_info = lv_label_create(lv_scr_act());
    lv_obj_align(label_info, LV_ALIGN_CENTER, 0, 40);
    lv_label_set_text(label_info, "Waiting for the copy task to start...");
    lv_obj_add_style(label_info, &text_style, LV_STATE_DEFAULT); // 绑定中文字体

    // 4.创建定时器：100ms执行一次刷新进度
    lv_timer_create(progress_timer_cb, 100, NULL);

    // 5.LVGL主循环（持续刷新界面）
    while(1) {
        lv_timer_handler();
        usleep(5000);
    }

    return NULL;
}

// int main(void)
// {
//     // LVGL的初始化
//     lv_init(); // 分配了内存，分配了定时器(刷新界面)

//     // 液晶屏设备的初始化
//     fbdev_init(); // open打开/dev/fb0,然后ioctl发送命令获取到液晶屏的宽，高，色深，mmap映射得到液晶屏的首地址

//     // 定义数组：存放液晶屏需要显示的画面数据
//     static lv_color_t buf[DISP_BUF_SIZE];

//     // 定义结构体变量
//     static lv_disp_draw_buf_t disp_buf;
//     // 初始化结构体和刚才定义的那个数组
//     lv_disp_draw_buf_init(&disp_buf, buf, NULL, DISP_BUF_SIZE);

//     // 定义结构体变量
//     static lv_disp_drv_t disp_drv;
//     // 初始化结构体变量
//     lv_disp_drv_init(&disp_drv);
//     disp_drv.draw_buf = &disp_buf;   // 指定图像界面显示需要用到的缓冲区
//     disp_drv.flush_cb = fbdev_flush; // 重点：指定LVGL显示界面，需要用到的画点/画矩形区域，给矩形区域填充颜色函数
//     disp_drv.hor_res  = 800;         // 液晶屏的宽
//     disp_drv.ver_res  = 480;         // 液晶屏的高
//     // 注册液晶屏
//     lv_disp_drv_register(&disp_drv);

//     // 触摸屏的初始化
//     evdev_init(); // 打开触摸屏的驱动，但是/dev/input/event10,跟我们6818不符合，我修改成了/dev/input/event0
//     // 定义结构体变量
//     static lv_indev_drv_t indev_drv_1;
//     // 初始化结构体变量
//     lv_indev_drv_init(&indev_drv_1);
//     indev_drv_1.type = LV_INDEV_TYPE_POINTER; // 输入设备类型，这个枚举值就表示触摸设备

//     // 指定触摸屏读取坐标的函数
//     indev_drv_1.read_cb = evdev_read; // 读取触摸屏坐标的函数
//     // 注册触摸屏
//     lv_indev_t * mouse_indev = lv_indev_drv_register(&indev_drv_1);

//     //创建一个进度条
//     bar1=lv_bar_create(lv_scr_act());

//     //设置位置，大小
//     lv_obj_center(bar1); //屏幕居中显示
//     lv_obj_set_size(bar1,200,60);

//     //设置进度值范围
//     lv_bar_set_range(bar1,0,100);

//     //设置进度条显示耗时--》缓慢的动画效果
//     lv_obj_set_style_anim_time(bar1,5000, LV_STATE_DEFAULT); //5000ms也就是5秒钟

//     //设置进度条显示到80的位置  LV_ANIM_ON表示开启动画显示效果
//     lv_bar_set_value(bar1, 80, LV_ANIM_ON);

//     // 一定要保留，不可以删除
//     while(1) {
//         lv_timer_handler(); // 把你刷新ui界面，监测LVGL程序是否发生事件，响应事件
//         usleep(5000);
//     }

//     return 0;
// }

// LVGL系统时钟（lv_conf.h配置对应宏）
/*Set in lv_conf.h as `LV_TICK_CUSTOM_SYS_TIME_EXPR`*/
uint32_t custom_tick_get(void)
{
    static uint64_t start_ms = 0;
    if(start_ms == 0) {
        struct timeval tv_start;
        gettimeofday(&tv_start, NULL);
        start_ms = (tv_start.tv_sec * 1000000 + tv_start.tv_usec) / 1000;
    }

    struct timeval tv_now;
    gettimeofday(&tv_now, NULL);
    uint64_t now_ms;
    now_ms = (tv_now.tv_sec * 1000000 + tv_now.tv_usec) / 1000;

    uint32_t time_ms = now_ms - start_ms;
    return time_ms;
}
