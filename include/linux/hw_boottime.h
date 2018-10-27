#ifndef _HW_BOOTTIME_H_
#define _HW_BOOTTIME_H_
#ifdef CONFIG_HUAWEI_BOOT_TIME
#include <linux/init.h>
int __init_or_module do_boottime_initcall(initcall_t fn);
#endif
#endif