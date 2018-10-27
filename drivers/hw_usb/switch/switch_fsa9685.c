/************************************************************
*
* Copyright (C), 1988-1999, Huawei Tech. Co., Ltd.
* FileName: switch_fsa9685.c
* Author: lixiuna(00213837)       Version : 0.1      Date:  2013-11-06
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
*  Description:    .c file for switch chip
*  Version:
*  Function List:
*  History:
*  <author>  <time>   <version >   <desc>
***********************************************************/

#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/timer.h>
#include <linux/param.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/workqueue.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/of_irq.h>
#include <asm/irq.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/usb/switch_chip.h>
#ifdef CONFIG_HUAWEI_DEV_DETECT
#include <dev_detect/hw_dev_detect.h>
#endif
#include <linux/usb/switch_usb.h>
//#include <chipset_common/hwusb/hw_usb_rwswitch.h>
#ifdef CONFIG_HUAWEI_EXTERN_ID_DETECT
#include <linux/usb/msm_hsusb.h>
#endif
#include "switch_usb_class.h"

static int gpio = HW_USB_VALUE_INVALID;
static struct i2c_client *this_client = NULL;
static struct work_struct g_intb_work;
static struct delayed_work detach_delayed_work;
#ifdef CONFIG_FSA9685_DEBUG_FS
static int reg_locked = HW_USB_REG_LOCKED;
static char chip_regs[HW_FSA_CHIP_REG_NUM] = {0};
#endif

static struct atomic_notifier_head chg_type_notifier_head;
static spinlock_t chg_type_lock;
static atomic_t chg_det_done = ATOMIC_INIT(0);

extern char *saved_command_line;

int usb_switch_register_notifier(struct notifier_block *nb)
{
    unsigned long flags = 0;
    int ret = HW_USB_VALUE_INVALID;

    if (NULL == nb) {
        pr_err("[USB_DEBUG] %s: nb is NULL!\n", __func__);
        return HW_USB_VALUE_INVALID;
    }

    spin_lock_irqsave(&chg_type_lock, flags);
    ret = atomic_notifier_chain_register(&chg_type_notifier_head, nb);
    spin_unlock_irqrestore(&chg_type_lock, flags);

    return ret;
}
EXPORT_SYMBOL_GPL(usb_switch_register_notifier);

int usb_switch_unregister_notifier(struct notifier_block *nb)
{
    unsigned long flags = 0;
    int ret = HW_USB_VALUE_INVALID;

    if (NULL == nb) {
        pr_err("[USB_DEBUG] %s: nb is NULL!\n", __func__);
        return HW_USB_VALUE_INVALID;
    }

    spin_lock_irqsave(&chg_type_lock, flags);
    ret = atomic_notifier_chain_unregister(&chg_type_notifier_head, nb);
    spin_unlock_irqrestore(&chg_type_lock, flags);

    return ret;
}
EXPORT_SYMBOL_GPL(usb_switch_unregister_notifier);

int usb_switch_get_done_flag(void)
{
    int val = atomic_read(&chg_det_done);
    return val;
}
EXPORT_SYMBOL_GPL(usb_switch_get_done_flag);

static int usb_switch_notifier_call(int event)
{
    pr_err("[USB_DEBUG] %s: usb notifier with %d\n", __func__, event);
    if (USB_SWITCH_ATTACH == event) {
        atomic_set(&chg_det_done, HW_USB_ONE);
    } else {
        atomic_set(&chg_det_done, HW_USB_ZERO);
    }

    atomic_notifier_call_chain(&chg_type_notifier_head, event, NULL);

    return HW_USB_SUCCESS;
}

static int fsa9685_write_reg(int reg, int val)
{
    int ret = 0;

    if (NULL == this_client) {
        ret = -ERR_NO_DEV;
        pr_err("[USB_DEBUG] %s: this_client=NULL!!! ret=%d\n", __func__, ret);
        return ret;
    }
    ret = i2c_smbus_write_byte_data(this_client, reg, val);
    if (ret < 0) {
        pr_info("[USB_DEBUG] %s: i2c write error!!! ret=%d\n", __func__, ret);
    }

#ifdef CONFIG_FSA9685_DEBUG_FS
    chip_regs[reg] = val;
#endif
    return ret;
}

static int fsa9685_read_reg(int reg)
{
    int ret = 0;

    if (NULL == this_client) {
        ret = -ERR_NO_DEV;
        pr_err("[USB_DEBUG] %s: this_client=NULL!!! ret=%d\n", __func__, ret);
        return ret;
    }
    ret = i2c_smbus_read_byte_data(this_client, reg);
    if (ret < 0) {
        pr_info("[USB_DEBUG] %s: i2c read error!!! ret=%d\n", __func__, ret);
    }

#ifdef CONFIG_FSA9685_DEBUG_FS
    chip_regs[reg] = ret;
#endif
    return ret;
}

static int fsa9685_masked_write_reg(int reg, int mask, int val)
{
    int tmp_val = 0;

    tmp_val = fsa9685_read_reg(reg);
    if (tmp_val < 0) {
        pr_info("[USB_DEBUG] %s: i2c read error!!! ret=%d\n", __func__, tmp_val);
        return tmp_val;
    }

    tmp_val &= ~mask;
    tmp_val |= val & mask;
    return fsa9685_write_reg(reg, tmp_val);
}

int fsa9685_manual_sw(int input_select)
{
    int value = 0, ret = 0;

    if (NULL == this_client) {
        ret = -ERR_NO_DEV;
        pr_err("[USB_DEBUG] %s: this_client=NULL!!! ret=%d\n", __func__, ret);
        return ret;
    }

    pr_info("[USB_DEBUG] %s: input_select = %d", __func__, input_select);
    switch (input_select) {
    case FSA9685_USB1_ID_TO_IDBYPASS:
        value = REG_VAL_FSA9685_USB1_ID_TO_IDBYPASS;
        break;
    case FSA9685_USB2_ID_TO_IDBYPASS:
        value = REG_VAL_FSA9685_USB2_ID_TO_IDBYPASS;
        break;
    case FSA9685_UART_ID_TO_IDBYPASS:
        value = REG_VAL_FSA9685_UART_ID_TO_IDBYPASS;
        break;
    case FSA9685_MHL_ID_TO_CBUS:
        value = REG_VAL_FSA9685_MHL_ID_TO_CBUS;
        break;
    case FSA9685_USB1_ID_TO_VBAT:
        value = REG_VAL_FSA9685_USB1_ID_TO_VBAT;
        break;
    case FSA9685_OPEN:
    default:
        value = REG_VAL_FSA9685_OPEN;
        break;
    }

    ret = fsa9685_write_reg(FSA9685_REG_MANUAL_SW_1, value);
    if (ret < 0) {
        ret = -ERR_FSA9685_REG_MANUAL_SW_1;
        pr_err("[USB_DEBUG] %s: write reg FSA9685_REG_MANUAL_SW_1 error!!! ret=%d\n",
                __func__, ret);
        return ret;
    }

    value = fsa9685_read_reg(FSA9685_REG_CONTROL);
    if (value < 0) {
        ret = -ERR_FSA9685_READ_REG_CONTROL;
        pr_err("[USB_DEBUG] %s: read FSA9685_REG_CONTROL error!!! ret=%d\n",
                __func__, ret);
        return ret;
    }

    /* 0: manual switching */
    value &= (~FSA9685_MANUAL_SW);
    ret = fsa9685_write_reg(FSA9685_REG_CONTROL, value);
    if (ret < 0) {
        ret = -ERR_FSA9685_WRITE_REG_CONTROL;
        pr_err("[USB_DEBUG] %s: write FSA9685_REG_CONTROL error!!! ret=%d\n",
                __func__, ret);
        return ret;
    }

    return HW_USB_SUCCESS;

}
EXPORT_SYMBOL_GPL(fsa9685_manual_sw);

int fsa9685_manual_detach(void)
{
    int ret = 0;

    if (NULL == this_client) {
        ret = -ERR_NO_DEV;
        pr_err("[USB_DEBUG] %s: this_client=NULL!!! ret=%d\n", __func__, ret);
        return ret;
    }

    schedule_delayed_work(&detach_delayed_work, msecs_to_jiffies(0));
    pr_info("[USB_DEBUG] %s: end.\n", __func__);

    return ret;
}
static void fsa9685_detach_work(struct work_struct *work)
{
    int ret = 0;

    if (NULL ==  work) {
        /* Now becuase the work not used, so we just print log rather than return*/
        pr_info("[USB_DEBUG] %s: work is NULL!\n", __func__);
    }

    pr_info("[USB_DEBUG] %s: entry.\n", __func__);
    ret = fsa9685_read_reg(FSA9685_REG_DETACH_CONTROL);
    if (ret < 0) {
        pr_err("[USB_DUBUG] %s: read FSA9685_REG_DETACH_CONTROL error!!! ret=%d",
                __func__, ret);
        return;
    }

    ret = fsa9685_write_reg(FSA9685_REG_DETACH_CONTROL, HW_USB_ONE);
    if (ret < 0) {
        pr_err("[USB_DEBUG] %s: write FSA9685_REG_DETACH_CONTROL error!!! ret=%d",
                __func__, ret);
        return;
    }

    pr_info("[USB_DEBUG] %s: end.\n", __func__);
	return;
}
EXPORT_SYMBOL_GPL(fsa9685_manual_detach);

static bool switch_chip_iddetect_workround(int chg_det_finish)
{
    int ret = 0;
    if (strstr(saved_command_line, "androidboot.huawei_swtype=factory") != NULL)
        return false;
    if (FSA9682_I2C_ADDR == this_client->addr)
        return false;

    msleep(HW_USB_SLEEP_500);
    if (chg_det_finish == USB_SWITCH_ATTACH) {
        pr_info("[USB_DEBUG] %s: disable 0x30h Bit(3) ID detect\n", __func__);
        ret = fsa9685_masked_write_reg(FSA9685_REG_WD_CTRL,
                FSA9685_WD_CTRL_ID_DETECT_EN, HW_USB_ZERO);
        if (ret < 0) {
            pr_err("[USB_DEBUG] %s: write FSA9685_REG_WD_CTRL Bit(3) error!!!\n", __func__);
            return false;
        }
    } else if (chg_det_finish == USB_SWITCH_DETACH) {
        pr_info("[USB_DEBUG] %s: enable 0x30h Bit(3) ID detect\n", __func__);
        ret = fsa9685_masked_write_reg(FSA9685_REG_WD_CTRL,
                FSA9685_WD_CTRL_ID_DETECT_EN, HW_USB_ONE);
        if (ret < 0) {
            pr_err("[USB_DEBUG] %s: write FSA9685_REG_WD_CTRL Bit(3) error!!!\n", __func__);
            return false;
        }
    }
    return true;
}

static irqreturn_t fsa9685_irq_handler(int irq, void *dev_id)
{
    int gpio_value = 0;

    if (NULL ==  dev_id) {
        /* Now becuase the dev_id not used, so we just print log rather than return*/
        pr_info("[USB_DEBUG] %s: dev_id is NULL!\n", __func__);
    }

    gpio_value = gpio_get_value(gpio);
    if (HW_USB_ONE == gpio_value) {
        pr_err("[USB_DEBUG] %s: intb high when interrupt occurred!!!\n", __func__);
    }

    schedule_work(&g_intb_work);

    pr_info("[USB_DEBUG] %s: end. gpio_value=%d\n", __func__, gpio_value);
    return IRQ_HANDLED;
}

static void fsa9685_intb_work(struct work_struct *work)
{
    int reg_ctl = 0, reg_intrpt = 0, reg_adc = 0, vbus_status = 0;
    int reg_dev_type1 = 0, reg_dev_type2 = 0, reg_dev_type3 = 0;
    int ret = 0;
    int id_valid_status = ID_VALID;
    static int invalid_times = 0;
#ifdef CONFIG_HUAWEI_EXTERN_ID_DETECT
    static int otg_attach = HW_USB_ONE;
#else
    static int otg_attach = HW_USB_ZERO;
#endif
    static int pedestal_attach;
    int chg_det_finish = USB_SWITCH_DETACH;

    if (NULL ==  work) {
        /* Now becuase the work not used, so we just print log rather than return*/
        pr_info("[USB_DEBUG] %s: work is NULL!\n", __func__);
    }

    reg_intrpt = fsa9685_read_reg(FSA9685_REG_INTERRUPT);
    vbus_status = fsa9685_read_reg(FSA9685_REG_VBUS_STATUS);
    pr_info("[USB_DEBUG] %s: read FSA9685_REG_INTERRUPT. reg_intrpt=0x%x\n",
            __func__, reg_intrpt);

    if (unlikely(reg_intrpt < 0)) {
        pr_err("[USB_DEBUG] %s: read FSA9685_REG_INTERRUPT error!!!\n",
                __func__);
    } else if (unlikely(reg_intrpt == 0)) {
        pr_err("[USB_DEBUG] %s: read FSA9685_REG_INTERRUPT, and no intr!!!\n",
                __func__);
    } else {
        if (reg_intrpt & FSA9685_ATTACH) {
            pr_info("[USB_DEBUG] %s: FSA9685_ATTACH\n", __func__);
            reg_dev_type1 = fsa9685_read_reg(FSA9685_REG_DEVICE_TYPE_1);
            reg_dev_type2 = fsa9685_read_reg(FSA9685_REG_DEVICE_TYPE_2);
            reg_dev_type3 = fsa9685_read_reg(FSA9685_REG_DEVICE_TYPE_3);
            pr_info("[USB_DEBUG] %s: reg_dev_type1=0x%X, reg_dev_type2=0x%X, reg_dev_type3=0x%X\n",
                    __func__, reg_dev_type1, reg_dev_type2,
                    reg_dev_type3);
            if (reg_dev_type1 & FSA9685_FC_USB_DETECTED) {
                pr_info("[USB_DEBUG] %s: FSA9685_FC_USB_DETECTED\n", __func__);
            }
            if (reg_dev_type1 & FSA9685_USB_DETECTED) {
                pr_info("[USB_DEBUG] %s: FSA9685_USB_DETECTED\n", __func__);
                if (FSA9685_USB2_ID_TO_IDBYPASS == get_swstate_value()) {
                    switch_usb2_access_through_ap();
                    pr_info("[USB_DEBUG] %s: fsa9685 switch to USB2 by setvalue\n",
                            __func__);
                }
            }
            if (reg_dev_type1 & FSA9685_UART_DETECTED) {
                pr_info("[USB_DEBUG] %s: FSA9685_UART_DETECTED\n",
                        __func__);
            }
            if (reg_dev_type1 & FSA9685_MHL_DETECTED) {
                pr_info("[USB_DEBUG] %s: FSA9685_MHL_DETECTED\n", __func__);
            }
            if (reg_dev_type1 & FSA9685_CDP_DETECTED) {
                pr_info("[USB_DEBUG] %s: FSA9685_CDP_DETECTED\n", __func__);
            }
            if (reg_dev_type1 & FSA9685_DCP_DETECTED) {
                pr_info("[USB_DEBUG] %s: FSA9685_DCP_DETECTED\n", __func__);
            }
            if (reg_dev_type1 & FSA9685_USBOTG_DETECTED) {
                pr_info("[USB_DEBUG] %s: FSA9685_USBOTG_DETECTED\n", __func__);
                otg_attach = HW_USB_ONE;
#ifdef CONFIG_HUAWEI_EXTERN_ID_DETECT
                /* id is pulled up, otg is inserted, usb to host mode */
                msm_id_status_changed(USB_ID_STATUS_LOW);
#endif
            }
            if (reg_dev_type1 & FSA9685_DEVICE_TYPE1_UNAVAILABLE) {
                id_valid_status = ID_INVALID;
                pr_info("[USB_DEBUG] %s: FSA9685_DEVICE_TYPE1_UNAVAILABLE_DETECTED\n",
                        __func__);
            }
            if (reg_dev_type2 & FSA9685_JIG_UART) {
                pr_info("[USB_DEBUG] %s: FSA9685_JIG_UART\n", __func__);
            }
            if (reg_dev_type2 & FSA9685_DEVICE_TYPE2_UNAVAILABLE) {
                id_valid_status = ID_INVALID;
                pr_info("[USB_DEBUG] %s: FSA9685_DEVICE_TYPE2_UNAVAILABLE_DETECTED\n",
                        __func__);
            }
            if (reg_dev_type3 & FSA9685_CUSTOMER_ACCESSORY7) {
                fsa9685_manual_sw(FSA9685_USB1_ID_TO_IDBYPASS);
                //ret = hw_usb_port_switch_request(INDEX_USB_REWORK_SN);
                pr_info("[USB_DEBUG] %s: FSA9685_CUSTOMER_ACCESSORY7 USB_REWORK_SN ret %d\n",
                        __func__, ret);
            }
            if (reg_dev_type3 & FSA9685_CUSTOMER_ACCESSORY5) {
                pr_info("[USB_DEBUG] %s: FSA9685_CUSTOMER_ACCESSORY5, 365K\n",
                        __func__);
                fsa9685_manual_sw(FSA9685_USB1_ID_TO_IDBYPASS);
                pedestal_attach = HW_USB_ONE;
                usb_custom_acc5_event(pedestal_attach);
            }
            if (reg_dev_type3 & FSA9685_FM8_ACCESSORY) {
                pr_info("[USB_DEBUG] %s: FSA9685_FM8_DETECTED\n", __func__);
                fsa9685_manual_sw(FSA9685_USB1_ID_TO_IDBYPASS);
            }
            if (reg_dev_type3 & FSA9685_DEVICE_TYPE3_UNAVAILABLE) {
                id_valid_status = ID_INVALID;
                if (reg_intrpt & FSA9685_VBUS_CHANGE) {
                    fsa9685_manual_sw(FSA9685_USB1_ID_TO_IDBYPASS);
                }
                pr_info("[USB_DEBUG] %s: FSA9685_DEVICE_TYPE3_UNAVAILABLE_DETECTED\n",
                        __func__);
            }
            chg_det_finish = USB_SWITCH_ATTACH;
        }

        if (reg_intrpt & FSA9685_RESERVED_ATTACH) {
            id_valid_status = ID_INVALID;
            if (reg_intrpt & FSA9685_VBUS_CHANGE) {
                fsa9685_manual_sw(FSA9685_USB1_ID_TO_IDBYPASS);
            }
            pr_info("[USB_DEBUG] %s: FSA9685_RESERVED_ATTACH\n", __func__);
        }

        if (reg_intrpt & FSA9685_DETACH) {
            pr_info("[USB_DEBUG] %s: FSA9685_DETACH\n", __func__);
            /*
             * check control register, if manual switch,
             * reset to auto switch
             */
            reg_ctl = fsa9685_read_reg(FSA9685_REG_CONTROL);
            reg_dev_type2 = fsa9685_read_reg(FSA9685_REG_DEVICE_TYPE_2);
            pr_info("[USB_DEBUG] %s: reg_ctl=0x%x\n", __func__, reg_ctl);
            if (reg_ctl < 0) {
                pr_err("[USB_DEBUG] %s: read FSA9685_REG_CONTROL error!!! reg_ctl=%d\n",
                        __func__, reg_ctl);
                goto OUT;
            }
            if (0 == (reg_ctl & FSA9685_MANUAL_SW)) {
                reg_ctl |= FSA9685_MANUAL_SW;
                ret = fsa9685_write_reg(FSA9685_REG_CONTROL, reg_ctl);
                if (ret < 0) {
                    pr_err("[USB_DEBUG] %s: write FSA9685_REG_CONTROL error!!!\n", __func__);
                    goto OUT;
                }
            }
            if (HW_USB_ONE == otg_attach) {
                pr_info("[USB_DEBUG] %s: FSA9685_USBOTG_DETACH\n", __func__);
                otg_attach = HW_USB_ZERO;
#ifdef CONFIG_HUAWEI_EXTERN_ID_DETECT
                /*
                 * id is pulled up, to change usb to
                 * peripheral mode
                 */
                msm_id_status_changed(USB_ID_STATUS_HIGH);
#endif
            }
            if (HW_USB_ONE == pedestal_attach) {
                pr_info("[USB_DEBUG] %s: FSA9685_CUSTOMER_ACCESSORY5_DETACH\n", __func__);
                pedestal_attach = HW_USB_ZERO;
                usb_custom_acc5_event(pedestal_attach);
            }
            if (reg_dev_type2 & FSA9685_JIG_UART) {
                pr_info("[USB_DEBUG] %s: FSA9685_JIG_UART\n", __func__);
            }
            chg_det_finish = USB_SWITCH_DETACH;
        }
        if (reg_intrpt & FSA9685_VBUS_CHANGE) {
            pr_info("[USB_DEBUG] %s: FSA9685_VBUS_CHANGE\n", __func__);
        }
        if (reg_intrpt & FSA9685_ADC_CHANGE) {
            reg_adc = fsa9685_read_reg(FSA9685_REG_ADC);
            pr_info("[USB_DEBUG] %s: FSA9685_ADC_CHANGE. reg_adc=%d\n",
                    __func__, reg_adc);
            if (reg_adc < HW_USB_ZERO) {
                pr_err("[USB_DEBUG] %s: read FSA9685_ADC_CHANGE error!!! reg_adc=%d\n",
                        __func__, reg_adc);
            }
            /* do user specific handle */
        }
    }

    if ((ID_INVALID == id_valid_status)
        && (reg_intrpt & (FSA9685_ATTACH | FSA9685_RESERVED_ATTACH))) {
        invalid_times++;
        pr_info("[USB_DEBUG] %s: invalid time:%d reset fsa9685 work\n",
                __func__, invalid_times);
        if (invalid_times < MAX_DETECTION_TIMES) {
            pr_info("[USB_DEBUG] %s: start schedule delayed work\n", __func__);
            schedule_delayed_work(&detach_delayed_work, msecs_to_jiffies(HW_USB_ZERO));
        } else {
            invalid_times = HW_USB_ZERO;
        }
    } else if ((ID_VALID == id_valid_status)
            && (reg_intrpt & (FSA9685_ATTACH | FSA9685_RESERVED_ATTACH))) {
        invalid_times = HW_USB_ZERO;
    }

    usb_switch_notifier_call(chg_det_finish);

    /* disable 0x30h Bit(3) ID detect when plug in usb, avoid fasle status to cut off battery */
    switch_chip_iddetect_workround(chg_det_finish);

OUT:
    pr_info("[USB_DEBUG] %s: end.\n", __func__);
    return;
}

#ifdef CONFIG_FSA9685_DEBUG_FS
static ssize_t dump_regs_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    const int regaddrs[] = {0x01, 0x02, 0x03, 0x04, 0x5, 0x06, 0x07,
                            0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
                            0x18, 0x19, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35};
    const char str[] = "0123456789abcdef";
    int i = 0, index = 0;
    char val = 0;

    if (NULL ==  buf) {
        /* Now becuase the work not used, so we just print log rather than return*/
        pr_info("[USB_DEBUG] %s: buf is NULL!\n", __func__);
        return HW_USB_FAIL;
    }

    if ((NULL ==  dev) || (NULL == attr)) {
        /* Now becuase the work not used, so we just print log rather than return*/
        pr_info("[USB_DEBUG] %s: dev or attr is NULL!\n", __func__);
    }

    for (i = 0; i < HW_USB_FSA_0X60; i++) {
		if ((i % HW_USB_FSA_3) == HW_USB_FSA_2)
			buf[i] = ' ';
		else
			buf[i] = 'x';
	}
	buf[HW_USB_FSA_0X5D] = '\n';
	buf[HW_USB_FSA_0X5E] = 0;
	buf[HW_USB_FSA_0X5F] = 0;
	if (reg_locked != 0) {
		for (i = 0; i < ARRAY_SIZE(regaddrs); i++) {
			if (regaddrs[i] != HW_FSA_INTERRUPT_REG) {
				val = fsa9685_read_reg(regaddrs[i]);
				chip_regs[i + HW_USB_FSA_1] = val;
			}
		}
	}

	for (i = 0; i < ARRAY_SIZE(regaddrs); i++) {
		index = i + HW_USB_FSA_1;
		val = chip_regs[index];
			buf[HW_USB_FSA_3*index] = str[(val&HW_USB_FSA_0XF0) >> HW_USB_SHIFT_4];
		buf[HW_USB_FSA_3*index+HW_USB_FSA_1] = str[val&HW_USB_FSA_0X0F];
		buf[HW_USB_FSA_3*index+HW_USB_FSA_2] = ' ';
	}

	return HW_USB_FSA_0X60;
}
static DEVICE_ATTR(dump_regs, S_IRUGO, dump_regs_show, NULL);
#endif

static ssize_t jigpin_ctrl_store(struct device *dev,
                                 struct device_attribute *attr,
                                 const char *buf, size_t size)
{
    int jig_val = JIG_PULL_DEFAULT_DOWN;
    int ret = 0;

    if ((NULL == this_client) || (NULL == buf)) {
        pr_err("[USB_DEBUG] %s: this_client or buf is NULL!\n", __func__);
        return -ERR_NO_DEV;
    }

    if ((NULL == dev) || (NULL == attr)) {
        /* Now because the dev and attr not used, so we just print logs */
        pr_err("[USB_DEBUG] %s: dev or attr is NULL!\n", __func__);
    }

    if (sscanf(buf, "%d", &jig_val) != HW_USB_ONE) {
        pr_err("[USB_DEBUG] %s:write regs failed, invalid input!\n", __func__);
        return HW_USB_FAIL;
    }
    switch (jig_val) {
    case JIG_PULL_DEFAULT_DOWN:
        pr_info("[USB_DEBUG] %s:pull down jig pin to default state\n", __func__);
        if (FSA9683_I2C_ADDR == this_client->addr) {
            ret = fsa9685_masked_write_reg(FSA9685_REG_MANUAL_SW_2,
                        REG_JIG_MASK, REG_JIG_UP);
            if (ret < 0) {
                pr_err("[USB_DEBUG] %s:write FSA9685_REG_MANUAL_SW_2 error!!!\n", __func__);
            }
        } else {
            ret = fsa9685_masked_write_reg(FSA9685_REG_MANUAL_SW_2,
                        REG_JIG_MASK, REG_JIG_DEFAULT_DOWN);
            if (ret < 0) {
                pr_err("[USB_DEBUG] %s:write FSA9685_REG_MANUAL_SW_2 error!!!\n",
                        __func__);
            }
        }
        break;
    case JIG_PULL_UP:
        pr_info("[USB_DEBUG] %s:pull up jig pin to cut battery\n", __func__);
        if (FSA9682_I2C_ADDR != this_client->addr) {
            ret = fsa9685_masked_write_reg(FSA9685_REG_WD_CTRL,
                    FSA9685_WD_CTRL_JIG_MANUAL_EN,
                    FSA9685_WD_CTRL_JIG_MANUAL_EN);
            if (ret < 0) {
                pr_err("[USB_DEBUG] %s:write FSA9685_REG_WD_CTRL error!!!\n", __func__);
            }
        }
        ret = fsa9685_masked_write_reg(FSA9685_REG_MANUAL_SW_1,
                REG_VAL_FSA9685_USB1_BYPASS, REG_VAL_FSA9685_USB1_BYPASS);
        if (ret < 0) {
            pr_err("[USB_DEBUG] %s:write FSA9685_REG_MANUAL_SW_1 error!!!\n",__func__);
        }
        ret = fsa9685_masked_write_reg(FSA9685_REG_CONTROL, FSA9685_MANUAL_SW, HW_USB_ZERO);
        if (ret < 0) {
            pr_err("[USB_DEBUG] %s:write FSA9685_REG_CONTROL error!!!\n",__func__);
        }
        if (FSA9683_I2C_ADDR == this_client->addr) {
            ret = fsa9685_masked_write_reg(FSA9685_REG_MANUAL_SW_2,
                        REG_JIG_MASK, REG_JIG_DEFAULT_DOWN);
            if (ret < 0) {
                pr_err("[USB_DEBUG] %s:write FSA9685_REG_MANUAL_SW_2 error!!!\n",
                        __func__);
            }
        } else {
            ret = fsa9685_masked_write_reg(FSA9685_REG_MANUAL_SW_2,
                            REG_JIG_MASK, REG_JIG_UP);
            if (ret < 0) {
                pr_err("[USB_DEBUG] %s:write FSA9685_REG_MANUAL_SW_2 error!!!\n",
                        __func__);
            }
        }
        break;
    default:
        pr_err("[USB_DEBUG] %s:Wrong input action!\n", __func__);
        return HW_USB_FAIL;
    }
    return HW_USB_FSA_0X60;
}

static ssize_t jigpin_ctrl_show(struct device *dev,
                struct device_attribute *attr, char *buf)
{
    int manual_sw2_val = 0;

    if (NULL == buf) {
        pr_err("[USB_DEBUG] %s: buf is NULL!\n", __func__);
        return -ERR_NO_DEV;
    }

    if ((NULL == dev) || (NULL == attr)) {
        /* Now because the dev and attr not used, so we just print logs */
        pr_err("[USB_DEBUG] %s: dev or attr is NULL!\n", __func__);
    }

    manual_sw2_val = fsa9685_read_reg(FSA9685_REG_MANUAL_SW_2);
    if (manual_sw2_val < 0) {
        pr_err("[USB_DEBUG] %s: read FSA9685_REG_MANUAL_SW_2 error!!! reg=%d.\n",
                __func__, manual_sw2_val);
    }

    return snprintf(buf, PAGE_SIZE, "%02x\n", manual_sw2_val);
}
static DEVICE_ATTR(jigpin_ctrl, S_IRUGO | S_IWUSR, jigpin_ctrl_show,
           jigpin_ctrl_store);

static ssize_t switchctrl_store(struct device *dev,
                struct device_attribute *attr,
                const char *buf, size_t size)
{
    int action = 0;

    if (NULL == buf) {
        pr_err("[USB_DEBUG] %s: buf is NULL!\n", __func__);
        return -ERR_NO_DEV;
    }

    if ((NULL == dev) || (NULL == attr)) {
        /* Now because the dev and attr not used, so we just print logs */
        pr_err("[USB_DEBUG] %s: dev or attr is NULL!\n", __func__);
    }

    if (sscanf(buf, "%d", &action) != HW_USB_ONE) {
        pr_err("[USB_DEBUG] %s:write regs failed, invalid input!\n", __func__);
        return HW_USB_FAIL;
    }
    switch (action) {
    case MANUAL_DETACH:
        pr_info("[USB_DEBUG] %s:manual_detach\n", __func__);
        fsa9685_manual_detach();
        break;
    case MANUAL_SWITCH:
        pr_info("[USB_DEBUG] %s:manual_switch\n", __func__);
        fsa9685_manual_sw(FSA9685_USB1_ID_TO_VBAT);
        break;
    default:
        pr_err("[USB_DEBUG] %s:Wrong input action!\n", __func__);
        return HW_USB_FAIL;
    }
    return HW_USB_FSA_0X60;
}

static ssize_t switchctrl_show(struct device *dev,
                struct device_attribute *attr, char *buf)
{
    int device_type1 = 0, device_type2 = 0, device_type3 = 0, mode = HW_USB_VALUE_INVALID;
    int tmp = 0;

    if (NULL == buf) {
        pr_err("[USB_DEBUG] %s: buf is NULL!\n", __func__);
        return -ERR_NO_DEV;
    }

    if ((NULL == dev) || (NULL == attr)) {
        /* Now because the dev and attr not used, so we just print logs */
        pr_err("[USB_DEBUG] %s: dev or attr is NULL!\n", __func__);
    }

    device_type1 = fsa9685_read_reg(FSA9685_REG_DEVICE_TYPE_1);
    if (device_type1 < 0) {
        pr_err("[USB_DEBUG] %s: read FSA9685_REG_DEVICE_TYPE_1 error!!! reg=%d.\n",
                __func__, device_type1);
        goto read_reg_failed;
    }
    device_type2 = fsa9685_read_reg(FSA9685_REG_DEVICE_TYPE_2);
    if (device_type2 < 0) {
        pr_err("[USB_DEBUG] %s: read FSA9685_REG_DEVICE_TYPE_2 error!!! reg=%d.\n",
                __func__, device_type2);
        goto read_reg_failed;
    }
    device_type3 = fsa9685_read_reg(FSA9685_REG_DEVICE_TYPE_3);
    if (device_type3 < 0) {
        pr_err("[USB_DEBUG] %s: read FSA9685_REG_DEVICE_TYPE_3 error!!! reg=%d.\n",
                __func__, device_type3);
        goto read_reg_failed;
    }
    tmp = device_type3 << HW_USB_SHIFT_16 | device_type2 << HW_USB_SHIFT_8 | device_type1;
    mode = 0;
    while (tmp >> mode)
        mode++;

read_reg_failed:
    return sprintf(buf, "%d", mode);
}

static DEVICE_ATTR(switchctrl, S_IRUGO | S_IWUSR, switchctrl_show,
                   switchctrl_store);

#ifdef CONFIG_OF
static const struct of_device_id switch_fsa9685_ids[] = {
    { .compatible = "huawei,fairchild_fsa9683" },
    { .compatible = "huawei,fairchild_fsa9682" },
    { .compatible = "huawei,nxp_cbtl9689" },
    {},
};
MODULE_DEVICE_TABLE(of, switch_fsa9685_ids);
#endif

static int fsa9685_probe(struct i2c_client *client,
            const struct i2c_device_id *id)
{
    int ret = 0, reg_ctl = 0, gpio_value = 0, reg_vendor = 0;
    struct device_node *node = NULL;
    struct class *switch_class = NULL;
    struct device *new_dev = NULL;

    if (NULL == client) {
        pr_info("[USB_DEBUG] %s: client or dev is NULL!\n", __func__);
        return -ERR_NO_DEV;
    }

    if (NULL == id) {
        /* Now because is not used, so we just print logs rather than return */
        pr_info("[USB_DEBUG] %s: id is NULL!\n", __func__);
    }

    node = client->dev.of_node;
    pr_info("[USB_DEBUG] %s: entry.\n", __func__);

    if (!i2c_check_functionality(client->adapter,
                    I2C_FUNC_SMBUS_BYTE_DATA)) {
        pr_err("[USB_DEBUG] %s: i2c_check_functionality error!!!\n", __func__);
        ret = -ERR_NO_DEV;
        this_client = NULL;
        goto err_i2c_check_functionality;
    }
    if (this_client) {
        pr_info("[USB_DEBUG] %s:chip is already detected\n", __func__);
        return HW_USB_SUCCESS;
	} else {
        this_client = client;
    }

    /* distingush the chip whith different address */
    reg_vendor = fsa9685_read_reg(FSA9685_REG_DEVICE_ID);
    if (reg_vendor < 0) {
        pr_err("[USB_DBUG] %s: read FSA9685_REG_DEVICE_ID error!!! reg_vendor=%d.\n",
                __func__, reg_vendor);
        goto err_i2c_check_functionality;
    }

#ifdef CONFIG_FSA9685_DEBUG_FS
    ret = device_create_file(&client->dev, &dev_attr_dump_regs);
    if (ret < 0) {
        pr_err("[USB_DEBUG] %s: device_create_file error!!! ret=%d.\n",
                __func__, ret);
        ret = -ERR_SWITCH_USB_DEV_REGISTER;
        goto err_i2c_check_functionality;
    }
#endif

    /*create a node for phone-off current drain test*/
    ret = device_create_file(&client->dev, &dev_attr_switchctrl);
    if (ret < 0) {
        pr_err("[USB_DEBUG] %s: device_create_file error!!! ret=%d.\n",
                __func__, ret);
        ret = -ERR_SWITCH_USB_DEV_REGISTER;
        goto err_get_named_gpio;
    }
    ret = device_create_file(&client->dev, &dev_attr_jigpin_ctrl);
    if (ret < 0) {
        pr_err("[USB_DEBUG] %s: device_create_file error!!! ret=%d.\n",
                __func__, ret);
        ret = -ERR_SWITCH_USB_DEV_REGISTER;
        goto err_create_jigpin_ctrl_failed;
    }
    switch_class = class_create(THIS_MODULE, "usb_switch");
    if (IS_ERR(switch_class)) {
        pr_err("[USB_DEBUG] %s:create switch class failed!\n", __func__);
        goto err_create_link_failed;
    }
    new_dev = device_create(switch_class, NULL, HW_USB_ZERO, NULL, "switch_ctrl");
    if (NULL == new_dev) {
        pr_err("[USB_DEBUG] %s:device create failed!\n", __func__);
        goto err_create_link_failed;
    }
    ret = sysfs_create_link(&new_dev->kobj, &client->dev.kobj, "manual_ctrl");
    if (ret < 0) {
        pr_err("[USB_DEBUG] %s:create link to switch failed!\n", __func__);
        goto err_create_link_failed;
    }
    /*create link end*/
    gpio = of_get_named_gpio(node, "fairchild_fsa9685,gpio-intb", HW_USB_ZERO);
    if (gpio < 0) {
        pr_err("[USB_DEBUG] %s: of_get_named_gpio error!!! ret=%d, gpio=%d.\n",
                __func__, ret, gpio);
        ret = -ERR_OF_GET_NAME_GPIO;
        goto err_get_named_gpio;
    }

    client->irq = gpio_to_irq(gpio);

    if (client->irq < 0) {
        pr_err("[USB_DEBUG] %s: gpio_to_irq error!!! ret=%d, gpio=%d, client->irq=%d.\n",
                __func__, ret, gpio, client->irq);
        ret = -ERR_GPIO_TO_IRQ;
        goto err_get_named_gpio;
    }

    ret = gpio_request(gpio, "fsa9685_int");
    if (ret < 0) {
        pr_err("[USB_DEBUG] %s: gpio_request error!!! ret=%d. gpio=%d.\n",
                __func__, ret, gpio);
        ret = -ERR_GPIO_REQUEST;
        goto err_get_named_gpio;
    }

    ret = gpio_direction_input(gpio);
    if (ret < 0) {
        pr_err("[USB_DEBUG] %s: gpio_direction_input error!!! ret=%d. gpio=%d.\n",
                __func__, ret, gpio);
        ret = -ERR_GPIO_DIRECTION_INPUT;
        goto err_gpio_direction_input;
    }

    INIT_DELAYED_WORK(&detach_delayed_work, fsa9685_detach_work);

    /* interrupt register */
    INIT_WORK(&g_intb_work, fsa9685_intb_work);

    ret = request_irq(client->irq, fsa9685_irq_handler,
                      IRQF_TRIGGER_FALLING,
                      "fsa9685_int", client);
    if (ret < 0) {
        pr_err("[USB_DEBUG] %s: request_irq error!!! ret=%d.\n", __func__, ret);
        ret = -ERR_REQUEST_THREADED_IRQ;
        goto err_gpio_direction_input;
    }
    enable_irq_wake(client->irq);
    /* clear INT MASK */
    reg_ctl = fsa9685_read_reg(FSA9685_REG_CONTROL);
    if (reg_ctl < 0) {
        pr_err("[USB_DEBUG] %s: read FSA9685_REG_CONTROL error!!! reg_ctl=%d.\n",
                __func__, reg_ctl);
        goto err_gpio_direction_input;
    }
    pr_info("[USB_DEBUG] %s: read FSA9685_REG_CONTROL. reg_ctl=0x%x.\n",
            __func__, reg_ctl);

    reg_ctl &= (~FSA9685_INT_MASK);
    ret = fsa9685_write_reg(FSA9685_REG_CONTROL, reg_ctl);
    if (ret < 0) {
        pr_err("[USB_DEBUG] %s: write FSA9685_REG_CONTROL error!!! reg_ctl=%d.\n",
                __func__, reg_ctl);
        goto err_gpio_direction_input;
    }
    pr_info("[USB_DEBUG] %s: write FSA9685_REG_CONTROL. reg_ctl=0x%x.\n",
            __func__, reg_ctl);

    ret = fsa9685_write_reg(FSA9685_REG_DCD, 0x0c);
    if (ret < 0) {
        pr_err("[USB_DEBUG] %s: write FSA9685_REG_DCD error!!! reg_DCD=0x%x.\n",
                __func__, 0x08);
        goto err_gpio_direction_input;
    }
    pr_info("[USB_DEBUG] %s: write FSA9685_REG_DCD. reg_DCD=0x%x.\n",
            __func__, 0x0c);
    /* Enable DCD timeout */
    reg_ctl = fsa9685_read_reg(FSA9685_REG_CONTROL_2);
    if (reg_ctl < 0) {
        pr_err("[USB_DEBUG] %s: read FSA9685_REG_CONTROL_2 error!!! reg_ctl=%d.\n",
                __func__, reg_ctl);
        goto err_gpio_direction_input;
    }
    pr_info("[USB_DEBUG] %s: read FSA9685_REG_CONTROL_2. reg_ctl=0x%x.\n",
            __func__, reg_ctl);

    reg_ctl |= (FSA9685_DCD_TIMEOUT | FSA9685_FM6_OPTION_ENABLE
                | FSA9685_FM8_OPTION_ENABLE);
    reg_ctl &= (~FSA9685_FM1_ENABLE);
    ret = fsa9685_write_reg(FSA9685_REG_CONTROL_2, reg_ctl);
    if (ret < 0) {
        pr_err("[USB_DEBUG] %s: write FSA9685_REG_CONTROL_2 error!!! reg_ctl=%d.\n",
                __func__, reg_ctl);
        goto err_gpio_direction_input;
    }
    pr_info("[USB_DEBUG] %s: write FSA9685_REG_CONTROL_2. reg_ctl=0x%x.\n",
            __func__, reg_ctl);

    gpio_value = gpio_get_value(gpio);
    pr_info("[USB_DEBUG] %s: intb=%d after clear MASK.\n", __func__, gpio_value);

    if (HW_USB_ZERO == gpio_value) {
        schedule_work(&g_intb_work);
    }

#ifdef CONFIG_HUAWEI_DEV_DETECT
    /* detect current device successful, set the flag as present */
    ret = set_hw_dev_detect_result(DEV_DETECT_USB_SWITCH);
    if (!ret)
    {
        pr_info("[USB_DEBUG] %s: ret = %d.\n", __func__, ret);
    }
#endif
    pr_info("[USB_DEBUG] %s: end. ret = %d.\n", __func__, ret);
    return ret;

err_gpio_direction_input:
    gpio_free(gpio);
err_create_link_failed:
    device_remove_file(&client->dev, &dev_attr_jigpin_ctrl);
err_create_jigpin_ctrl_failed:
    device_remove_file(&client->dev, &dev_attr_switchctrl);
err_get_named_gpio:
#ifdef CONFIG_FSA9685_DEBUG_FS
    device_remove_file(&client->dev, &dev_attr_dump_regs);
#endif
err_i2c_check_functionality:
    this_client = NULL;

    pr_err("[USB_DEBUG] %s: FAIL!!! end. ret = %d.\n", __func__, ret);
    return ret;
}

static int fsa9685_remove(struct i2c_client *client)
{
    if (NULL == client) {
        pr_info("[USB_DEBUG] %s: client is NULL!\n", __func__);
        return HW_USB_FAIL;
    }
#ifdef CONFIG_FSA9685_DEBUG_FS
    device_remove_file(&client->dev, &dev_attr_dump_regs);
#endif
    device_remove_file(&client->dev, &dev_attr_switchctrl);
    device_remove_file(&client->dev, &dev_attr_jigpin_ctrl);
    free_irq(client->irq, client);
    gpio_free(gpio);

    return HW_USB_SUCCESS;
}

static const struct i2c_device_id fsa9685_i2c_id[] = {
    { "fsa9685", HW_USB_ZERO },
    {},
};

static struct i2c_driver fsa9685_i2c_driver = {
    .driver = {
        .name = "fsa9685",
        .owner = THIS_MODULE,
        .of_match_table = of_match_ptr(switch_fsa9685_ids),
    },
    .probe	= fsa9685_probe,
    .remove   = fsa9685_remove,
    .id_table = fsa9685_i2c_id,
};

static __init int fsa9685_i2c_init(void)
{
    int ret = 0;
    pr_info("[USB_DEBUG] %s: entry.\n", __func__);
    ret = i2c_add_driver(&fsa9685_i2c_driver);
    if (ret) {
        pr_err("[USB_DEBUG] %s: i2c_add_driver error!!!\n", __func__);
    }

    pr_info("[USB_DEBUG] %s: end.\n", __func__);
    return ret;
}

static __exit void fsa9685_i2c_exit(void)
{
    i2c_del_driver(&fsa9685_i2c_driver);
}

module_init(fsa9685_i2c_init);
module_exit(fsa9685_i2c_exit);

MODULE_AUTHOR("Lixiuna<lixiuna@huawei.com>");
MODULE_DESCRIPTION("I2C bus driver for FSA9685 USB Accesory Detection Switch");
MODULE_LICENSE("GPL v2");
