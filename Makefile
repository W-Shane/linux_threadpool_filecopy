# 编译器：本地Ubuntu编译用gcc；ARM开发板编译换回arm-linux-gcc
CC = arm-linux-gcc

# # 显式开启驱动编译
# USE_FBDEV = 1
# USE_EVDEV = 1

# # LVGL根目录（lvgl、lv_drivers文件夹都在项目根目录）
# LVGL_DIR 必须指向lvgl文件夹的上一级：lvgl_ui/lvgl的父级就是lvgl_ui
LVGL_DIR := $(realpath $(shell pwd)/lvgl_ui/lvgl)

# 强制加入屏幕和触摸驱动源码
CSRCS += $(realpath $(shell pwd)/lvgl_ui/lv_drivers)/display/fbdev.c
CSRCS += $(realpath $(shell pwd)/lvgl_ui/lv_drivers)/indev/evdev.c

# # 强制指定驱动文件路径
# # 假设 lv_drivers 文件夹与 lvgl 文件夹同在 lvgl_ui 目录下
# DRIVERS_DIR := $(realpath $(shell pwd)/lvgl_ui/lv_drivers)
# CSRCS += $(DRIVERS_DIR)/display/fbdev.c
# CSRCS += $(DRIVERS_DIR)/indev/evdev.c

# 编译参数：保留官方LVGL编译标准参数，保留-pthread线程库
# CFLAGS ?= -O3 -g0 -I$(LVGL_DIR)/ -I$(LVGL_DIR)/include/freetype2 \

# 编译参数
CFLAGS ?= -O3 -g0 \
    -I$(LVGL_DIR) \
    -I$(LVGL_DIR)/.. \
    -Wall -Wshadow -Wundef -Wmissing-prototypes -Wno-discarded-qualifiers \
    -Wextra -Wno-unused-function -Wno-error=strict-prototypes \
    -Wpointer-arith -fno-strict-aliasing -Wno-error=cpp -Wuninitialized \
    -Wmaybe-uninitialized -Wno-unused-parameter -Wno-missing-field-initializers \
    -Wtype-limits -Wsizeof-pointer-memaccess -Wno-format-nonliteral \
    -Wno-cast-qual -Wunreachable-code -Wno-switch-default -Wreturn-type \
    -Wmultichar -Wformat-security -Wno-ignored-qualifiers -Wno-sign-compare \
    -Wno-error=missing-prototypes -Wdouble-promotion -Wclobbered -Wdeprecated \
    -Wempty-body -Wstack-usage=2048 -Wno-unused-value -std=gnu99 -pthread

# 链接库：数学库、线程库
LDFLAGS ?= -lm -lpthread

# 最终生成可执行程序名
BIN = copy_tool

# 唯一主程序（只有这里有main）
MAINSRC = ./copy_main.c

# $(info 驱动目录路径=$(realpath $(shell pwd)/lvgl_ui/lv_drivers))

# 自定义模块：目录树、线程池、LVGL界面
USER_CSRCS += ./tree_module/tree.c
USER_CSRCS += ./threadpool_module/threadpool.c
USER_CSRCS += ./lvgl_ui/lvgl_ui.c

# LVGL_DIR现在已经是 lvgl_ui/lvgl，直接引入同目录lvgl.mk
include $(LVGL_DIR)/lvgl.mk
# lv_drivers回到上层lvgl_ui目录去找
include $(realpath $(shell pwd)/lvgl_ui/lv_drivers)/lv_drivers.mk


# 合并所有C文件：官方LVGL源码 + 你的自定义模块源码
CSRCS += $(USER_CSRCS)

# 后缀规则
OBJEXT ?= .o
AOBJS = $(ASRCS:.S=$(OBJEXT))
COBJS = $(CSRCS:.c=$(OBJEXT))
MAINOBJ = $(MAINSRC:.c=$(OBJEXT))

SRCS = $(ASRCS) $(CSRCS) $(MAINSRC)
OBJS = $(AOBJS) $(COBJS)

# 编译规则
all: default

%.o: %.c
	@$(CC)  $(CFLAGS) -c $< -o $@
	@echo "CC $<"

# # 链接所有目标文件生成程序
# default: $(AOBJS) $(COBJS) $(MAINOBJ)
# 	$(CC) -o $(BIN) $(MAINOBJ) $(AOBJS) $(COBJS) $(LDFLAGS)

# 简化链接命令，避免漏写.o
default: $(MAINOBJ) $(AOBJS) $(COBJS)
	$(CC) -o $(BIN) $^ $(LDFLAGS)

# 清理：删除可执行文件+所有.o中间文件
clean: 
	rm -f $(BIN) $(AOBJS) $(COBJS) $(MAINOBJ)

# 安装/卸载规则保留（答辩拓展用，平时不用）
prefix ?= /usr
bindir ?= $(prefix)/bin

install:
	install -d $(DESTDIR)$(bindir)
	install $(BIN) $(DESTDIR)$(bindir)

uninstall:
	$(RM) -r $(addprefix $(DESTDIR)$(bindir)/,$(BIN))

# 文件末尾增加伪目标
.PHONY: all clean install uninstall