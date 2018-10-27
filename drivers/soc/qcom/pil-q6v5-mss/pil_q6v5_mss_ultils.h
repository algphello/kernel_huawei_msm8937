/*
 * Copyright (C) 2013 Huawei Device Co.Ltd
 * License terms: GNU General Public License (GPL) version 2
 *
 */

#ifndef PIL_Q6V5_MSS_ULTILS
#define PIL_Q6V5_MSS_ULTILS

#include<linux/kernel.h>
#include<linux/module.h>
#include<linux/types.h>
#include <linux/sched.h>

#define OEM_QMI "libqmi_oem_main"

extern int subsystem_restart_requested;

void restart_oem_qmi(void);

#endif
