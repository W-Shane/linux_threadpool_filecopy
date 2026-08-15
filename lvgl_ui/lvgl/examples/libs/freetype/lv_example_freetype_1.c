#include "../../lv_examples.h"
#if LV_BUILD_EXAMPLES
#if LV_USE_FREETYPE

/**
 * LVGL官方写的例子程序，教你如何使用freetype显示中文汉字
 */
void lv_example_freetype_1(void)
{
    //定义结构体变量，结构体代表你要使用的中文字体
    static lv_ft_info_t info;
    //初始化结构体成员
    //这是开发板的路径
    info.name = "/font/simsun.ttc"; //我要使用自己拷贝过来的宋体中文字体
    info.weight = 22; //设置中文字体大小
    info.style = FT_FONT_STYLE_NORMAL;
    info.mem = NULL;
    if(!lv_ft_font_init(&info)) {
        LV_LOG_ERROR("create failed.");
    }

    //新建样式，让样式跟中文字体绑定
    //样式：跟计算机网页设计中的样式的概念类似，字体，前景色，背景色...都叫做样式
    static lv_style_t style;
    //初始化样式对象
    lv_style_init(&style); 
    //样式跟字体绑定在一起，等一会就能使用你指定的字体(宋体)
    lv_style_set_text_font(&style, info.font);
    //设置文字显示居中对齐
    lv_style_set_text_align(&style, LV_TEXT_ALIGN_CENTER);

    //创建一个标签对象
    lv_obj_t * label = lv_label_create(lv_scr_act());
    //让你这个标签使用刚才的样式
    lv_obj_add_style(label, &style, 0);
    //给标签设置文字
    lv_label_set_text(label, "我是秦始皇！");
    //设置标签居中显示
    lv_obj_center(label);
}
#else

void lv_example_freetype_1(void)
{
    /*TODO
     *fallback for online examples*/

    lv_obj_t * label = lv_label_create(lv_scr_act());
    lv_label_set_text(label, "FreeType is not installed");
    lv_obj_center(label);
}

#endif
#endif
