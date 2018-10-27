/************************************************************
*
* Copyright (C), 1988-1999, Huawei Tech. Co., Ltd.
* FileName: switch_usb_class.h
* Author: lixiuna(00213837)       Version : 0.1      Date:  2013-11-07
*
* This software is licensed under the terms of the GNU General Public
* License version 2, as published by the Free Software Foundation, and
* may be copied, distributed, and modified under those terms.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
*  Description:    .h file for switch chip
*  Version:
*  Function List:
*  History:
*  <author>  <time>   <version >   <desc>
***********************************************************/

#define FSA9685_OPEN             0
#define FSA9685_USB1             1
#define FSA9685_USB2             2
#define FSA9685_MHL              4
#define HW_FSA_CHIP_REG_NUM      0xff
#define HW_USB_VALUE_INVALID     (-1)
#define HW_USB_REG_LOCKED        1
#define HW_USB_SUCCESS           0
#define HW_USB_FAIL              (-1)
#define HW_USB_ONE               1
#define HW_USB_ZERO              0
#define HW_USB_FSA_1             1
#define HW_USB_FSA_2             2
#define HW_USB_FSA_3             3
#define HW_USB_SHIFT_4           4
#define HW_USB_SHIFT_8           8
#define HW_USB_SHIFT_16          16
#define HW_USB_SLEEP_500         500
#define HW_USB_SLEEP_1000        1000

#define HW_FSA_INTERRUPT_REG     0x03
#define HW_USB_FSA_0X0F          0x0f
#define HW_USB_FSA_0X5D          0x5d
#define HW_USB_FSA_0X5E          0x5e
#define HW_USB_FSA_0X5F          0x5f
#define HW_USB_FSA_0X60          0x60
#define HW_USB_FSA_0XF0          0xf0

#define HW_USB_EVENT_BUF_SIZE    32

struct switch_usb_info {
    struct atomic_notifier_head charger_type_notifier_head;
    spinlock_t reg_flag_lock;
};

extern int fsa9685_manual_sw(int input_select);
extern int fsa9685_manual_detach(void);
