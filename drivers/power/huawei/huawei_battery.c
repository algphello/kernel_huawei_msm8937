/*
 * drivers/power/huawei/huawei_battery.c
 *
 * huawei charger driver
 *
 * Copyright (C) 2012-2017 HUAWEI, Inc.
 * Author: HUAWEI, Inc.
 *
 * This package is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/jiffies.h>
#include <linux/wakelock.h>
#include <linux/usb/otg.h>
#include <linux/io.h>
#include <linux/gpio.h>
#include <linux/power_supply.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/notifier.h>
#include <linux/mutex.h>
#include <linux/spmi.h>
#include <linux/sysfs.h>
#include <linux/power/huawei_charger.h>
#include <linux/msm_spmi.h>
#include <linux/i2c.h>
#include <linux/version.h>
#include <linux/pmic-voter.h>

#define BUFFER_SIZE                     30
#define CHG_OVP_REG_NUM                 3
#define CHG_DIS_REG_NUM                 3
#define DEFAULT_FASTCHG_MAX_CURRENT     1000
#define DEFAULT_FASTCHG_MAX_VOLTAGE     4400
#define DEFAULT_FASTCHG_WARM_CURRENT    900
#define DEFAULT_FASTCHG_WARM_VOLTAGE    4100
#define DEFAULT_FASTCHG_COOL_CURRENT    1100
#define DEFAULT_FASTCHG_COOL_VOLTAGE    4400
#define DEFAULT_USB_ICL_CURRENT         2000
#define DEFAULT_CUSTOM_COOL_LOW_TH      0
#define DEFAULT_CUSTOM_COOL_UP_TH       50
#define DEFAULT_CUSTOM_COOL_CURRENT     300
/* 1A/2A charger adaptive algo */
#define BATTERY_IBUS_MARGIN_MA          200
#define BATTERY_IBUS_CONFIRM_COUNT      6
#define BATTERY_IBUS_WORK_DELAY_TIME    1000
#define BATTERY_IBUS_WORK_START_TIME    3000
#define IINLIM_1000                     1000
#define IINLIM_1500                     1500
#define IINLIM_2000                     2000
#define BATT_HEALTH_TEMP_HOT            600
#define BATT_HEALTH_TEMP_WARM           480
#define BATT_HEALTH_TEMP_COOL           100
#define BATT_HEALTH_TEMP_COLD           -200
#define EMPTY_SOC                       0
#define FACTORY_SOC_LOW                 1
#define FULL_SOC                        100
#define DEFAULT_BATT_TEMP               250

#define SPMI_ADDR_MASK                  0xffff
#define SPMI_SID_OFFSET                 16
#define SPMI_SID_MASK                   0xf

#define MA_TO_UA                        1000
#define SPMI_BUS                        1
#define I2C_BUS                         2

struct spmi_bus_info {
    struct spmi_controller *spmi_ctrl;
};

struct i2c_bus_info {
    struct i2c_adapter *i2c_adapter;
    unsigned short i2c_addr;
    unsigned short i2c_flags;
};

struct ops_param {
    u8 sid;
    u16 addr;
    int count;
    u8 *val;
};

struct huawei_battery_info;
typedef int (*reg_ops)(struct huawei_battery_info *, struct ops_param *, bool);

struct huawei_battery_info {
    struct device *dev;
    struct power_supply batt_psy;
    enum power_supply_property *batt_props;
    int num_props;

    struct power_supply *bk_batt_psy;
    struct power_supply *bms_psy;
    struct power_supply *usb_psy;

    int bus_type;
    bool support_parallel_charger;
    struct device *bk_battery_dev;
    struct spmi_bus_info spmi_info;
    struct i2c_bus_info i2c_info;
    struct i2c_bus_info second_i2c_info;

    /* register operation */
    reg_ops battery_reg_read;
    reg_ops battery_reg_write;
    reg_ops battery_second_reg_read;
    reg_ops battery_second_reg_write;

    /* chargelog dump regs */
    u32 *dump_regs;
    int dump_regs_num;
    u32 *parallel_dump_regs;
    int parallel_regs_num;

    /* voteable */
    struct votable *usb_icl_votable;
    struct votable *fcc_votable;
    struct votable *vbat_votable;
    struct votable *bch_votable;

    int fastchg_max_current;
    int fastchg_max_voltage;
    int fastchg_warm_current;
    int fastchg_warm_voltage;
    int fastchg_cool_current;
    int fastchg_cool_voltage;
    int usb_icl_max;
    int thermal_limit_current;
    int current_adjusted_ma;
    bool ibus_detect_disable;
    int ibus_detecting;
    int customize_cool_lower_limit;
    int customize_cool_upper_limit;
    int fastchg_current_customize_cool;
    int charger_batt_capacity_mah;

    /* delayed work */
    struct delayed_work ibus_detect_work;
    struct delayed_work jeita_work;
    struct delayed_work monitor_charging_work;

    /* jeita solution */
    bool jeita_use_interrupt;
    bool jeita_hw_curr_limit;
    bool jeita_hw_volt_limit;
    bool jeita_hw_chg_dis;
    int jeita_chg_dis_reg;
    int jeita_chg_dis_mask;
    int jeita_chg_dis_value;
    /* fake soc */
    int fake_soc;
};

#define IN_THERMAL_VOTER "IN_THERMAL_VOTER"
#define IBUS_DETECT_VOTER "IBUS_DETECT_VOTER"
#define ICL_USER_VOTER "ICL_USER_VOLTER"
#define FCC_JEITA_VOTER "FCC_JEITA_VOTER"
#define FCC_USER_VOTER "FCC_USER_VOTER"
#define FIVE_DEGREE_VOTER "FIVE_DEGREE_VOTER"
#define VBAT_JEITA_VOTER "VBAT_JEITA_VOTER"
#define VBAT_USER_VOTER "VBAT_USER_VOTER"
#define VBAT_BASP_VOTER "VBAT_BASP_VOTER"
#define BCH_JEITA_VOTER "BCH_JEITA_VOTER"
#define BCH_WARM_WR_VOTER   "BCH_WARM_WR_VOTER"
#define BCH_USER_VOTER  "BCH_USER_VOTER"

static int set_prop_safety_timer_enable(struct huawei_battery_info *info, int use_timer);
static int set_prop_to_psy(struct power_supply *psy, enum power_supply_property prop,
                        const union power_supply_propval *val);
static int battery_jeita_solution(struct huawei_battery_info *info);

static bool factory_mode = false;
static int __init early_parse_factory_mode(char *cmdline)
{
    if (!cmdline) {
        return 0;
    }

    if ((cmdline) && !strncmp(cmdline, "factory", strlen("factory"))) {
        factory_mode = true;
    }

    return 0;
}
early_param("androidboot.huawei_swtype", early_parse_factory_mode);

/***************************************************
 Function: dump_reg_read
 Description: dummy function for register read
 Parameters:
    info            huawei_battery_info
    param           operation parameter
    prime           prime charger or parallel charger
 Return:
    0 means read successful
***************************************************/
static int dummy_reg_read(struct huawei_battery_info *info,
                            struct ops_param *param, bool prime)
{
    if (!info || !param) {
        dev_err(info->dev, "Invalid param, just warning\n");
        return 0;
    }

    dev_dbg(info->dev, "Dummy reg read\n");
    return 0;
}

/***************************************************
 Function: dump_reg_write
 Description: dummy function for register write
 Parameters:
    info            huawei_battery_info
    param           operation parameter
    prime           prime charger or parallel charger
 Return:
    0 means read successful
***************************************************/
static int dummy_reg_write(struct huawei_battery_info *info,
                            struct ops_param *param, bool prime)
{
    if (!info || !param) {
        dev_err(info->dev, "Invalid param, just warning\n");
        return 0;
    }

    dev_dbg(info->dev, "Dummy reg write\n");
    return 0;
}

/***************************************************
 Function: spmi_reg_read
 Description: register read for spmi bus
 Parameters:
    info            huawei_battery_info
    param           operation parameter
    prime           prime charger or parallel charger
 Return:
    0 means read successful
    negative mean fail
***************************************************/
static int spmi_reg_read(struct huawei_battery_info *info,
                            struct ops_param *param, bool prime)
{
    int rc = 0;
    struct spmi_bus_info *spmi_info = NULL;

    if (!info || !param) {
        pr_err("%s: Invalid param, fatal error\n", __func__);
        return -EINVAL;
    }

    spmi_info = &info->spmi_info;
    if (!spmi_info) {
        dev_err(info->dev, "%s: Invalid spmi info, fatal error\n", __func__);
        return -EINVAL;
    }

    rc = spmi_ext_register_readl(spmi_info->spmi_ctrl, param->sid,
                        param->addr, param->val, param->count);
    if (rc < 0) {
        dev_err(info->dev, "Read form reg 0x%x fail, rc = %d\n", param->addr, rc);
        return rc;
    }

    return 0;
}

/***************************************************
 Function: spmi_reg_write
 Description: register write for spmi bus
 Parameters:
    info            huawei_battery_info
    param           operation parameter
    prime           prime charger or parallel charger
 Return:
    0 means read successful
    negative mean fail
***************************************************/
#define SPMI_SECURE_MASK    (0xFF00)
#define SPMI_SECURE_REG     (0xD0)
#define SPMI_SECURE_VAL     (0xA5)
static int spmi_reg_write(struct huawei_battery_info *info,
                            struct ops_param *param, bool prime)
{
    int rc = 0;
    u8 data = SPMI_SECURE_VAL;
    struct spmi_bus_info *spmi_info = NULL;

    if (!info || !param) {
        pr_err("%s: Invalid param, fatal error\n", __func__);
        return -EINVAL;
    }

    spmi_info = &info->spmi_info;
    if (!spmi_info) {
        dev_err(info->dev, "%s: Invalid spmi info, fatal error\n", __func__);
        return -EINVAL;
    }

    rc = spmi_ext_register_writel(spmi_info->spmi_ctrl, param->sid,
                ((param->addr & SPMI_SECURE_MASK) | SPMI_SECURE_REG), &data, 1);
    if (rc < 0) {
        dev_err(info->dev, "Unlock periph 0x%x fail, rc = %d\n",
                                        (param->addr & 0xff00), rc);
        return rc;
    }
    rc = spmi_ext_register_writel(spmi_info->spmi_ctrl, param->sid,
                        param->addr, param->val, param->count);
    if (rc < 0) {
        dev_err(info->dev, "Write 0x%x to reg 0x%x fail, rc = %d\n",
                                        *param->val, param->addr, rc);
        return rc;
    }
    return 0;
}

/***************************************************
 Function: i2c_reg_read
 Description: register read for i2c bus
 Parameters:
    info            huawei_battery_info
    param           operation parameter
    prime           prime charger or parallel charger
 Return:
    0 means read successful
    negative mean fail
***************************************************/
static int i2c_reg_read(struct huawei_battery_info *info,
                            struct ops_param *param, bool prime)
{
    int status = 0, i = 0;
    union i2c_smbus_data data;
    struct i2c_bus_info *i2c_info = NULL;

    if (!info || !param) {
        pr_err("%s: Invalid param, fatal error\n", __func__);
        return -EINVAL;
    }

    memset(&data, 0, sizeof(data));
    if (prime) {
        i2c_info = &info->i2c_info;
    } else {
        i2c_info = &info->second_i2c_info;
    }

    if (!i2c_info) {
        dev_err(info->dev, "%s: Invalid i2c info, fatal error\n", __func__);
        return -EINVAL;
    }

    for (i = 0; i < param->count; i++) {
        status = i2c_smbus_xfer(i2c_info->i2c_adapter,
                        i2c_info->i2c_addr, i2c_info->i2c_flags,
                        I2C_SMBUS_READ, param->addr + i,
                        I2C_SMBUS_BYTE_DATA, &data);
        if (status < 0) {
            pr_err("Read register 0x%x fail rc = %d\n", param->addr + i, status);
            break;
        }
        param->val[i] = data.byte;
    }
    return status;
}

/***************************************************
 Function: i2c_reg_write
 Description: register write for i2c bus
 Parameters:
    info            huawei_battery_info
    param           operation parameter
    prime           prime charger or parallel charger
 Return:
    0 means read successful
    negative mean fail
***************************************************/
static int i2c_reg_write(struct huawei_battery_info *info,
                            struct ops_param *param, bool prime)
{
    int status = 0, i = 0;
    union i2c_smbus_data data;
    struct i2c_bus_info *i2c_info = NULL;

    if (!info || !param) {
        pr_err("%s: Invalid param, fatal error\n", __func__);
        return -EINVAL;
    }

    memset(&data, 0, sizeof(data));
    if (prime) {
        i2c_info = &info->i2c_info;
    } else {
        i2c_info = &info->second_i2c_info;
    }

    if (!i2c_info) {
        dev_err(info->dev, "%s: Invalid i2c info, fatal error\n", __func__);
        return -EINVAL;
    }

    for (i = 0; i < param->count; i++) {
        data.byte = param->val[i];
        status = i2c_smbus_xfer(i2c_info->i2c_adapter,
                        i2c_info->i2c_addr, i2c_info->i2c_flags,
                        I2C_SMBUS_WRITE, param->addr,
                        I2C_SMBUS_BYTE_DATA, &data);
        if (status < 0) {
            pr_err("Write register 0x%x fail rc = %d\n", param->addr + i , status);
            break;
        }
    }

    return status;
}

/***************************************************
 Function: battery_reg_read
 Description: battery register read interface
 Parameters:
    info            huawei_battery_info
    addr            register addr
    val             the place to hold values
    prime           prime charger or parallel charger
 Return:
    0 means read successful
    negative mean fail
***************************************************/
static int battery_reg_read(struct huawei_battery_info *info,
                                u32 addr, u8 *val, bool prime)
{
    int rc = 0;
    struct ops_param param;
    reg_ops reg_read = NULL;

    if (!info || !val) {
        pr_err("%s: Invalid param, fatal error\n", __func__);
        return -EINVAL;
    }

    memset(&param, 0, sizeof(param));
    if (prime) {
        reg_read = info->battery_reg_read;
    } else if (info->support_parallel_charger) {
        reg_read = info->battery_second_reg_read;
    } else {
        dev_dbg(info->dev, "Cannot find the reg ops\n");
        return -EINVAL;
    }

    param.sid = (addr >> SPMI_SID_OFFSET) & SPMI_SID_MASK;
    param.addr = addr & SPMI_ADDR_MASK;
    param.count = 1; /* only read 1 byte */
    param.val = val;
    rc = reg_read(info, &param, prime);
    if (rc) {
        dev_err(info->dev, "read reg 0x%x fail, rc = %d\n", addr, rc);
    }
    return rc;
}

/***************************************************
 Function: battery_reg_write
 Description: battery register write interface
 Parameters:
    info            huawei_battery_info
    addr            register addr
    val             the place to hold values
    prime           prime charger or parallel charger
 Return:
    0 means read successful
    negative mean fail
***************************************************/
static int battery_reg_write(struct huawei_battery_info *info,
                                u32 addr, u8 val, bool prime)
{
    int rc = 0;
    struct ops_param param;
    reg_ops reg_write = NULL;

    if (!info) {
        pr_err("%s: Invalid param, fatal error\n", __func__);
        return -EINVAL;
    }

    memset(&param, 0, sizeof(param));
    if (prime) {
        reg_write = info->battery_reg_write;
    } else if (info->support_parallel_charger) {
        reg_write = info->battery_second_reg_write;
    } else {
        dev_dbg(info->dev, "Cannot find the reg ops\n");
        return -EINVAL;
    }

    param.sid = (addr >> SPMI_SID_OFFSET) & SPMI_SID_MASK;
    param.addr = addr & SPMI_ADDR_MASK;
    param.count = 1; /* ony write 1 byte */
    param.val = &val;
    rc = reg_write(info, &param, prime);
    if (rc) {
        dev_err(info->dev, "write reg 0x%x fail, rc = %d\n", addr, rc);
    }
    return rc;
}

/***************************************************
 Function: batt_disable_temp_limit
 Description: disable jeita solution for heat case
 Parameters:
    info            huawei_battery_info
 Return:
    0 means unregister success
    negative means fail
***************************************************/
static int chg_no_temp_limit = 0;
static int batt_disable_temp_limit(struct huawei_battery_info *info)
{
    int rc = 0;
    u8 data = 0;

    if (!info) {
        pr_err("%s: Invalid param, fatal error\n", __func__);
        return -EINVAL;
    }

    if (info->jeita_hw_chg_dis) {
        rc = battery_reg_read(info, info->jeita_chg_dis_reg, &data, true);
        if (rc)
            goto OUT;
        data &= ~info->jeita_chg_dis_mask;
        if (chg_no_temp_limit)
            data |= info->jeita_chg_dis_value;
        rc = battery_reg_write(info, info->jeita_chg_dis_reg, data, true);
    } else {
        rc = battery_jeita_solution(info);
        if (rc < 0) {
            dev_err(info->dev, "%s temp limit fail, rc = %d\n",
                chg_no_temp_limit > 0 ? "disable" : "enable", rc);
        }
    }

OUT:
    if (rc) {
        dev_err(info->dev, "Set chg no temp limit fail, rc = %d\n", rc);
    }
    return rc;
}

/***************************************************
 Function: chg_no_temp_limit_set
 Description: file node set interface
 Parameters:
    val         the value need to set
    kp          kernel_param
 Return:
    0 means unregister success
    negative means fail
***************************************************/
static int chg_no_temp_limit_set(const char *val, const struct kernel_param *kp)
{
    int rc = 0;
    struct power_supply *batt_psy = NULL;
    struct huawei_battery_info *info = NULL;

    if (!val || !kp) {
        pr_err("%s: Invalid param, fatal error\n", __func__);
        return -EINVAL;
    }

    rc = param_set_int(val, kp);
    if (rc) {
        pr_err("Unable to set chg_no_temp_limit: %d\n", rc);
        return rc;
    }
    batt_psy = power_supply_get_by_name("battery");
    if (!batt_psy) {
        pr_err("batt psy not found\n");
        return 0;
    }
    info = container_of(batt_psy, struct huawei_battery_info, batt_psy);
    return batt_disable_temp_limit(info);
}

static struct kernel_param_ops chg_no_temp_limit_ops = {
    .set = chg_no_temp_limit_set,
    .get = param_get_int,
};
module_param_cb(chg_no_temp_limit, &chg_no_temp_limit_ops, &chg_no_temp_limit, 0644);

/***************************************************
 Function: chg_no_timer_set
 Description: disable safety timer for charging
 Parameters:
    val         the value set by userspace
    kp          the kernel param related with
                    this chg_no_timer property
 Return:
    0 means read successful
    negative mean fail
***************************************************/
static int chg_no_timer = 0;
static int chg_no_timer_set(const char *val, const struct kernel_param *kp)
{
    int rc = 0;
    struct power_supply *batt_psy = NULL;
    struct huawei_battery_info *info = NULL;

    if (!val || !kp) {
        pr_err("%s: Invalid param, fatal error\n", __func__);
        return -EINVAL;
    }

    rc = param_set_int(val, kp);
    if (rc) {
        pr_err("Unable to set chg_no_timer: %d\n", rc);
        return rc;
    }
    batt_psy = power_supply_get_by_name("battery");
    if (!batt_psy) {
        pr_err("batt psy not foudn\n");
        return 0;
    }
    info = container_of(batt_psy, struct huawei_battery_info, batt_psy);
    rc = set_prop_safety_timer_enable(info, !chg_no_timer);
    return rc;
}
static struct kernel_param_ops chg_no_timer_ops = {
    .set = chg_no_timer_set,
    .get = param_get_int,
};
module_param_cb(chg_no_timer, &chg_no_timer_ops, &chg_no_timer, 0644);

/***************************************************
 Function: batt_set_usb_current
 Description: set input current for usb
 Parameters:
    info                huawei_battery_info
 Return:
    0 means read successful
    negative mean fail
***************************************************/
#define DEFAULT_USB_CURRENT 500000
static int chg_usb_current = 0;
static int batt_set_usb_current(struct huawei_battery_info *info)
{
    int rc = 0;
    union power_supply_propval prop = {0, };

    if (!info) {
        pr_err("%s: Invalid param\n", __func__);
        return -EINVAL;
    }

    if (chg_usb_current > 0) {
        prop.intval = chg_usb_current;
        rc = set_prop_to_psy(info->usb_psy, POWER_SUPPLY_PROP_CURRENT_MAX, &prop);
        if (rc) {
            dev_err(info->dev, "Set bk battery input current fail, rc = %d\n", rc);
        }
    } else {
        prop.intval = DEFAULT_USB_CURRENT;
        rc = set_prop_to_psy(info->usb_psy, POWER_SUPPLY_PROP_CURRENT_MAX, &prop);
        if (rc) {
            dev_err(info->dev, "Set effective battery input current fail, rc = %d\n", rc);
        }
    }
    return rc;
}

/***************************************************
 Function: chg_usb_current_set
 Description: set input current for usb
 Parameters:
    val         the value set by userspace
    kp          the kernel param related with
                    this chg_usb_current property
 Return:
    0 means read successful
    negative mean fail
***************************************************/
static int chg_usb_current_set(const char *val,const struct kernel_param *kp)
{
    int rc = 0;
    struct power_supply *batt_psy = NULL;
    struct huawei_battery_info *info = NULL;

    if (!val || !kp) {
        pr_err("%s: Invalid param, fatal error\n", __func__);
        return -EINVAL;
    }

    rc = param_set_int(val, kp);
    if (rc) {
        pr_err("Unable to set chg_usb_current; %d\n", rc);
        return rc;
    }
    chg_usb_current *= MA_TO_UA;
    batt_psy = power_supply_get_by_name("battery");
    if (!batt_psy) {
        pr_err("batt psy not found\n");
        return 0;
    }
    info = container_of(batt_psy, struct huawei_battery_info, batt_psy);
    return batt_set_usb_current(info);
}
static struct kernel_param_ops chg_usb_current_ops = {
    .set = chg_usb_current_set,
    .get = param_get_int,
};
module_param_cb(chg_usb_current, &chg_usb_current_ops, &chg_usb_current, 0644);

/***************************************************
 Function: get_prop_from_psy
 Description: get property from power supply instance
 Parameters:
    psy         the power supply instance
    prop        the property need to get
    val         the place used to hold values
 Return:
    0 means read successful
    negative mean fail
***************************************************/
static int get_prop_from_psy(struct power_supply *psy,
                enum power_supply_property prop,
                union power_supply_propval *val)
{
    int rc = 0;

    if (!psy || !val) {
        pr_err("%s: Invalid param, fatal error\n", __func__);
        return -EINVAL;
    }

    rc = psy->get_property(psy, prop, val);
    if (rc < 0) {
        pr_debug("Get property %d from %s psy fail, rc = %d\n",
            prop, psy->name, rc);
    }

    return rc;
}

/***************************************************
 Function: set_prop_from_psy
 Description: set property to power supply instance
 Parameters:
    psy         the power supply instance
    prop        the property need to set
    val         the place used to hold values
 Return:
    0 means read successful
    negative mean fail
***************************************************/
static int set_prop_to_psy(struct power_supply *psy,
                enum power_supply_property prop,
                const union power_supply_propval *val)
{
    int rc = 0;

    if (!psy || !val) {
        pr_err("%s: Invalid param, fatal error\n", __func__);
        return -EINVAL;
    }

    rc = psy->set_property(psy, prop, val);
    if (rc < 0) {
        pr_err("Set property %d to %s psy fail, rc = %d\n",
            prop, psy->name, rc);
    }

    return rc;
}

/***************************************************
 Function: set_fastchg_current_cb
 Description: the callback fucntion for voter,
            used to set fast charge current
 Parameters:
    dev         the device which own vote point
    fcc_ma      the fastchg current voted
    client      voter
    last_fcc_ma the last fastchg current voted
    last_client the last fastchg current voter
 Return:
    0 means read successful
    negative mean fail
***************************************************/
static int set_fastchg_current_cb(struct votable *votable,
                    void *data,
                    int fcc_ma,
                    const char *client)
{
    struct huawei_battery_info *info = data;
    union power_supply_propval val = {0, };
    int rc = 0;

    if (!info || !info->bk_batt_psy) {
        pr_err("Invalid bk battery power supply\n");
        return -EINVAL;
    }

    if (fcc_ma < 0) {
        pr_err("No voters\n");
        return 0;
    }

    val.intval = fcc_ma * MA_TO_UA;
    rc = set_prop_to_psy(info->bk_batt_psy,
                POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX, &val);

    return rc;
}

/***************************************************
 Function: set_usb_current_limit_cb
 Description: the callback fucntion for voter,
            used to set charger input current
 Parameters:
    dev         the device which own vote point
    fcc_ma      the input current voted
    client      voter
    last_fcc_ma the last input current voted
    last_client the last input current voter
 Return:
    0 means read successful
    negative mean fail
***************************************************/
static int set_usb_current_limit_cb(struct votable *votable,
                    void *data,
                    int icl_ma,
                    const char *client)
{
    struct huawei_battery_info *info = data;
    union power_supply_propval val = {0, };
    int rc = 0;

    if (!info || !info->bk_batt_psy) {
        pr_err("Invalid bk battery power supply\n");
        return -EINVAL;
    }

    if (icl_ma < 0) {
        pr_err("No voters\n");
        return 0;
    }

    if (chg_usb_current > icl_ma)
        icl_ma = chg_usb_current;

    val.intval = icl_ma;
    rc = set_prop_to_psy(info->bk_batt_psy,
                    POWER_SUPPLY_PROP_INPUT_CURRENT_MAX, &val);

    return rc;
}

/***************************************************
 Function: set_vbat_cb
 Description: the callback fucntion for voter,
            used to set charge terminal voltage
 Parameters:
    dev         the device which own vote point
    fcc_ma      the terminal voltage voted
    client      voter
    last_fcc_ma the last terminal voltage voted
    last_client the last terminal voltage voter
 Return:
    0 means read successful
    negative mean fail
***************************************************/
static int set_vbat_cb(struct votable *votable, void *data,
                    int vbat_mv, const char *client)
{
    struct huawei_battery_info *info = data; 
    union power_supply_propval val = {0, };
    int rc = 0;

    if (!info || !info->bk_batt_psy) {
        pr_err("Invalid bk battery power supply\n");
        return -EINVAL;
    }

    if (vbat_mv < 0) {
        pr_err("No voters\n");
        return 0;
    }

    val.intval = vbat_mv;
    rc = set_prop_to_psy(info->bk_batt_psy,
                    POWER_SUPPLY_PROP_VOLTAGE_MAX, &val);

    return rc;
}

/***************************************************
 Function: charging_suspend_cb
 Description: the callback fucntion for voter,
            used to suspend/resume charging
 Parameters:
    dev         the device which own vote point
    fcc_ma      the charging status voted
    client      voter
    last_fcc_ma the last charging status voted
    last_client the last charging status voter
 Return:
    0 means read successful
    negative mean fail
***************************************************/
static int charging_suspend_cb(struct votable *votable, void *data,
                    int suspend, const char *client)
{
    int rc = 0;
    struct huawei_battery_info *info = data;
    union power_supply_propval val = {0, };

    if (!info || !info->bk_batt_psy) {
        pr_err("Invalid bk battery power supply\n");
        return -EINVAL;
    }

    if (suspend < 0) {
        pr_err("No voters\n");
        suspend = false;
    }

    val.intval = !suspend;
    rc = set_prop_to_psy(info->bk_batt_psy,
            POWER_SUPPLY_PROP_BATTERY_CHARGING_ENABLED, &val);

    return rc;
}

/***************************************************
 Function: battery_detect_ibus_work
 Description: ibus detect function, used to detect
        the real ibus the charger can support
 Parameters:
    work            work_struct
 Return:
    0 means read successful
    negative mean fail
***************************************************/
static void battery_detect_ibus_work(struct work_struct *work)
{
    struct huawei_battery_info *info = NULL;
    static int ibus_err_count = 0;
    int iusb_ma = 0, current_adjusted = 0;
    union power_supply_propval val = {0, };
    int rc = 0;

    if (!work) {
        pr_err("%s: Invalid param, fatal error\n", __func__);
        return;
    }

    info = container_of(work, struct huawei_battery_info,
                                    ibus_detect_work.work);
    if (!info) {
        pr_err("%s: battery info is NULL, fatal error\n", __func__);
        return;
    }

    rc = get_prop_from_psy(info->usb_psy, POWER_SUPPLY_PROP_PRESENT, &val);
    if (rc < 0) {
        return;
    }

    if (!val.intval) {
        dev_info(info->dev, "Usb absent, Dont do ibus detect\n");
        info->ibus_detecting = false;
        ibus_err_count = 0;
    }

    if (BATTERY_IBUS_CONFIRM_COUNT <= ibus_err_count) {
        ibus_err_count = 0;
        iusb_ma = IINLIM_1000;
    } else {
        rc = get_prop_from_psy(info->bk_batt_psy,
                        POWER_SUPPLY_PROP_INPUT_CURRENT_NOW, &val);
        if (rc < 0) {
            val.intval = IINLIM_1000;
        }
        iusb_ma = val.intval;
    }

    dev_info(info->dev, "detect input current, iusb_ma = %d\n", iusb_ma);
    if (iusb_ma >= (IINLIM_1000)) {
        if (((IINLIM_1500 + BATTERY_IBUS_MARGIN_MA)) < iusb_ma) {
            current_adjusted = IINLIM_2000;
        } else if (((IINLIM_1000 + BATTERY_IBUS_MARGIN_MA)) < iusb_ma) {
            current_adjusted = IINLIM_1500;
        } else {
            current_adjusted = IINLIM_1000;
        }
        ibus_err_count = 0;
        info->ibus_detecting = false;
        info->current_adjusted_ma = current_adjusted;
        rc = vote(info->usb_icl_votable,
                        IBUS_DETECT_VOTER, true, current_adjusted);
        if (rc < 0) {
            dev_err(info->dev, "Vote for usb current limit fail, rc %d\n", rc);
        }
        if (info->thermal_limit_current) {
            rc = vote(info->usb_icl_votable,
                        IN_THERMAL_VOTER, true, info->thermal_limit_current);
            if (rc < 0) {
                dev_err(info->dev, "Vote for thermal limit fail, rc %d\n", rc);
            }
        }
    } else {
        ibus_err_count++;
        dev_err(info->dev, "Get ibus error and try,count %d\n", ibus_err_count);
        schedule_delayed_work(&info->ibus_detect_work,
                    msecs_to_jiffies(BATTERY_IBUS_WORK_DELAY_TIME));
    }
}

/***************************************************
 Function: battery_input_current_adjust
 Description: schedule ibus detect work
 Parameters:
    info            huawei_battery_info
 Return:
    0 means read successful
    negative mean fail
***************************************************/
static void battery_input_current_adjust(struct huawei_battery_info *info)
{
    if (!info) {
        pr_err("%s: Invalid param, fatal error\n", __func__);
        return;
    }

    if (info->ibus_detect_disable) {
        return;
    }

    if (!info->ibus_detecting) {
        vote(info->usb_icl_votable, IN_THERMAL_VOTER, true, info->usb_icl_max);
        info->ibus_detecting = true;
        schedule_delayed_work(&info->ibus_detect_work,
                msecs_to_jiffies(BATTERY_IBUS_WORK_START_TIME));
    }
}

/***************************************************
 Function: batt_imp_curr_limit
 Description: current limit by software jeita
 Parameters:
    info            huawei_battery_info
    curr            the current need to limit
 Return:
    0 means read successful
    negative mean fail
***************************************************/
static inline int batt_imp_curr_limit(struct huawei_battery_info *info, int curr)
{
    int rc = 0;

    if (!info) {
        pr_err("%s: Invalid param, fatal error\n", __func__);
        return -EINVAL;
    }

    if (!info->jeita_hw_curr_limit) {
        pr_info("Soft Limit current %d for jeita\n", curr);
        rc = vote(info->fcc_votable, FCC_JEITA_VOTER, true, curr);
    }
    if (rc < 0) {
        dev_err(info->dev, "jeita limit fcc(%dmA) fail, rc = %d\n", curr, rc);
    }
    return 0;
}

/***************************************************
 Function: batt_imp_volt_limit
 Description: voltage limit by software jeita
 Parameters:
    info            huawei_battery_info
    volt            the voltage need to limit
 Return:
    0 means read successful
    negative mean fail
***************************************************/
static inline int batt_imp_volt_limit(struct huawei_battery_info *info, int volt)
{
    int rc = 0;

    if (!info) {
        pr_err("%s: Invalid param, fatal error\n", __func__);
        return -EINVAL;
    }

    if (!info->jeita_hw_volt_limit) {
        rc = vote(info->vbat_votable, VBAT_JEITA_VOTER, true, volt);
    }
    if (rc < 0) {
        dev_err(info->dev, "jeita limit vbat(%dmV) fail, rc = %d\n", volt, rc);
    }
    return 0;
}

/***************************************************
 Function: batt_imp_chg_dis
 Description: disable charging by software jeita
 Parameters:
    info            huawei_battery_info
    enable          enable/disable flag
 Return:
    0 means read successful
    negative mean fail
***************************************************/
static inline int batt_imp_chg_dis(struct huawei_battery_info *info, bool enable)
{
    int rc = 0;

    if (!info) {
        pr_err("%s: Invalid param, fatal error\n", __func__);
        return -EINVAL;
    }

    if (!info->jeita_hw_chg_dis) {
        rc = vote(info->bch_votable, BCH_JEITA_VOTER, enable, 0);
    }
    if (rc < 0) {
        dev_err(info->dev, "jeita enable charging fail, rc = %d\n", rc);
    }
    return 0;
}

/***************************************************
 Function: battery_jeita_solution
 Description: jeita solution main function
 Parameters:
    info            huawei_battery_info
 Return:
    0 means read successful
    negative mean fail
***************************************************/
static int battery_jeita_solution(struct huawei_battery_info *info)
{
    union power_supply_propval val = {0, };
    static int last_health = POWER_SUPPLY_HEALTH_UNKNOWN;
    int rc = 0, health = 0;

    if (!info) {
        pr_err("Invalid battery info point\n");
        return -EINVAL;
    }

    rc = get_prop_from_psy(info->bk_batt_psy, POWER_SUPPLY_PROP_HEALTH, &val);
    if (rc) {
        dev_err(info->dev, "Cannot get health from bk battery, rc = %d\n", rc);
        return rc;
    }
    health = val.intval;

    if (IS_ENABLED(CONFIG_HLTHERM_RUNTEST) || chg_no_temp_limit)
    {
        if ( (health != POWER_SUPPLY_HEALTH_WARM) \
            && (health != POWER_SUPPLY_HEALTH_OVERHEAT) )
        {
                health = POWER_SUPPLY_HEALTH_GOOD;
        }
    }

    pr_info("battery jeita solution: get health %d, "
                "last health %d\n", health, last_health);

    if (health == last_health) {
        return 0;
    }

    switch(health) {
    case POWER_SUPPLY_HEALTH_GOOD:
        if (POWER_SUPPLY_HEALTH_WARM == last_health)
            vote(info->bch_votable, BCH_WARM_WR_VOTER, true, 0);
        batt_imp_curr_limit(info, info->fastchg_max_current);
        batt_imp_volt_limit(info, info->fastchg_max_voltage);
        batt_imp_chg_dis(info, false);
        if (POWER_SUPPLY_HEALTH_WARM == last_health)
            vote(info->bch_votable, BCH_WARM_WR_VOTER, false, 0);
        break;
    case POWER_SUPPLY_HEALTH_COOL:
        batt_imp_curr_limit(info, info->fastchg_cool_current);
        batt_imp_volt_limit(info, info->fastchg_cool_voltage);
        batt_imp_chg_dis(info, false);
        break;
    case POWER_SUPPLY_HEALTH_WARM:
        batt_imp_curr_limit(info, info->fastchg_warm_current);
        batt_imp_volt_limit(info, info->fastchg_warm_voltage);
        batt_imp_chg_dis(info, false);
        break;
    case POWER_SUPPLY_HEALTH_OVERHEAT:
    case POWER_SUPPLY_HEALTH_COLD:
        batt_imp_chg_dis(info, true);
        break;
    default:
        break;
    }

    last_health = health;
    return 0;
}

/***************************************************
 Function: battery_jeita_work
 Description: the work used to implementation jeita
        solution
 Parameters:
    work                work_struct
 Return: NA
***************************************************/
static void battery_jeita_work(struct work_struct *work)
{
    struct huawei_battery_info *info = NULL;

    if (!work) {
        pr_err("%s: Invalid param, fatal error\n", __func__);
        return;
    }

    info = container_of(work, struct huawei_battery_info, jeita_work.work);

    battery_jeita_solution(info);
}

/***************************************************
 Function: battery_health_handler
 Description: used to accept battery health change
        notification
 Parameters:
    self                notifier_block
    event               event id
    data                private data, can be NULL
 Return:
    0 means success
    negative means fail
***************************************************/
int battery_health_handler(void)
{
    struct huawei_battery_info *info = NULL;
    struct power_supply *batt_psy = power_supply_get_by_name("battery");

    if (!batt_psy) {
        pr_err("%s: Invalid param, fatal error\n", __func__);
        return -EINVAL;
    }

    info = container_of(batt_psy, struct huawei_battery_info, batt_psy);

    pr_info("Schdule jeita work\n");
    schedule_delayed_work(&info->jeita_work, 0);

    return 0;
}

/***************************************************
 Function: battery_monitor_charging_work
 Description: monitor work for current limit in
        [0 ~ 5] temperature
 Parameters:
    work            work_struct
 Return: NA
***************************************************/
#define MONITOR_CHARGING_DELAY_TIME 5000
#define VBAT_LOW_TH     3400000
static void battery_monitor_charging_work(struct work_struct *work)
{
    struct huawei_battery_info *info = NULL;
    union power_supply_propval val = {0, };
    int rc = 0, temp = 0, usb_present = 0, charging_status = 0;

    if (!work) {
        pr_err("Invalid param, fatal error\n");
        return;
    }

    info = container_of(work, struct huawei_battery_info,
                                    monitor_charging_work.work);
    if (!info) {
        pr_err("Invalid battery info point\n");
        return;
    }

    rc = get_prop_from_psy(info->usb_psy, POWER_SUPPLY_PROP_PRESENT, &val);
    if (rc) {
        goto RESCHEDULE;
    }
    usb_present = val.intval;

    if (usb_present) {
        val.intval = 1;
        /* only for qcom pmi serial chips */
        set_prop_to_psy(info->bms_psy, POWER_SUPPLY_PROP_UPDATE_NOW, &val);
        rc = get_prop_from_psy(info->bk_batt_psy, POWER_SUPPLY_PROP_TEMP, &val);
        if (rc) {
            goto RESCHEDULE;
        }

        if (IS_ENABLED(CONFIG_HLTHERM_RUNTEST) || chg_no_temp_limit) {
            temp = DEFAULT_BATT_TEMP;
        } else {
            temp = val.intval;
        }

        if ((temp > info->customize_cool_lower_limit)
            && (temp <= info->customize_cool_upper_limit)) {
            vote(info->fcc_votable, FIVE_DEGREE_VOTER, true,
                        info->fastchg_current_customize_cool);
        } else {
            vote(info->fcc_votable, FIVE_DEGREE_VOTER, true,
                        info->fastchg_max_current);
        }

        /* here implement jeita standard */
        if (!info->jeita_use_interrupt)
            battery_jeita_solution(info);
    }

    if ( IS_ENABLED(CONFIG_HLTHERM_RUNTEST) )
    {
        /*get current charging status*/
        rc = get_prop_from_psy(info->bk_batt_psy, POWER_SUPPLY_PROP_STATUS, &val);
        if (rc) {
            goto RESCHEDULE;
        }
        charging_status = val.intval;

        /*get now voltage*/
        rc = get_prop_from_psy(info->bk_batt_psy, POWER_SUPPLY_PROP_VOLTAGE_NOW, &val);
        if ( (val.intval < VBAT_LOW_TH) && (POWER_SUPPLY_STATUS_CHARGING != charging_status) )
        {
            batt_imp_chg_dis(info, false);
            dev_info(info->dev, "[HLTHERM_RUNTEST]now voltage < 3400mV, enable charging\n");
        }
    }

RESCHEDULE:
    schedule_delayed_work(&info->monitor_charging_work,
            msecs_to_jiffies(MONITOR_CHARGING_DELAY_TIME));
}

/***************************************************
 Function: strncat_protect
 Description: tool funciont, copy data safety
 Parameters:
    dest            dest buffer
    src             src buffer
 Return: NA
***************************************************/
static void strncat_protect(char *dest, char *src)
{
    int str_length = 0;

    if (NULL == dest || NULL == src) {
        pr_err("the dest or src is NULL");
        return;
    }
    if (strlen(dest) >= CHARGELOG_SIZE) {
        pr_err("strncat dest is full!\n");
        return;
    }

    str_length = min(CHARGELOG_SIZE - strlen(dest), strlen(src));
    if (str_length > 0) {
        strncat(dest, src, str_length);
    }
}

/***************************************************
 Function: battery_dump_register_head_raw
 Description: dump register head to buffer
 Parameters:
    regs            registers
    reg_len         register number
    reg_head        buffer
 Return: NA
***************************************************/
static void battery_dump_register_head_raw(u32 *regs, int reg_len, char *reg_head)
{
    int i = 0;
    char buf[BUFFER_SIZE] = {0};

    if (!regs || !reg_head) {
        pr_err("Invalid regs and reg_head param\n");
        return;
    }

    for (i = 0; i < reg_len; i++) {
        snprintf(buf, BUFFER_SIZE, "R[0x%4x] ", regs[i] & 0xffff);
        strncat_protect(reg_head, buf);
    }
}

/***************************************************
 Function: battery_get_register_head
 Description: dump regiser head and some flags
 Parameters:
    info            huawei_battery_info
    reg_head        buffer
 Return: NA
***************************************************/
static void battery_get_register_head(
                    struct huawei_battery_info *info, char *reg_head)
{
    if (!info || !reg_head) {
        dev_info(info->dev, "Invalid battery info point\n");
        return;
    }

    memset(reg_head, 0, CHARGELOG_SIZE);

    snprintf(reg_head, MAX_SIZE, "system_level    bch_en    icur_max    ");

    battery_dump_register_head_raw(info->dump_regs,
                            info->dump_regs_num, reg_head);
    if (info->parallel_regs_num  > 0) {
        battery_dump_register_head_raw(info->parallel_dump_regs,
                            info->parallel_regs_num, reg_head);
    }
}

/***************************************************
 Function: battery_dump_register_raw
 Description: dump register value to buffer
 Parameters:
    info            huawei_battery_info
    regs            registers
    reg_len         register number
    prime           prime charger or parallel charger
    reg_value       buffer
 Return: NA
***************************************************/
static void battery_dump_register_raw(
                    struct huawei_battery_info *info, const u32 *regs,
                    int reg_len, bool prime, char *reg_value)
{
    char buf[BUFFER_SIZE] = {0};
    int i = 0, rc = 0;
    u8 data = 0x0;

    if (!info || !regs || !reg_value) {
        pr_err("Invalid param for %s\n", __func__);
        return;
    }

    for (i = 0; i < reg_len; i++) {
        rc = battery_reg_read(info, regs[i], &data, prime);
        if (rc < 0) {
            pr_err("battery reg read fail: addr %03x,rc = %d\n",
                                regs[i], rc);
            return;
        }

        snprintf(buf, BUFFER_SIZE, "0x%-8.4x", data);
        strncat_protect(reg_value, buf);
    }
}

/***************************************************
 Function: battery_dump_register
 Description: dump register value and flag value
 Parameters:
    info            huawei_battery_info
    reg_value       buffer
 Return: NA
***************************************************/
static void battery_dump_register(
                struct huawei_battery_info *info, char *reg_value)
{
    int system_level = 0, bch_en = 0, icur_max = 0, rc = 0;
    union power_supply_propval val = {0, };

    if (!info || !reg_value) {
        dev_info(info->dev, "Invalid battery info point\n");
        return;
    }

    memset(reg_value, 0, CHARGELOG_SIZE);

    rc = get_prop_from_psy(info->bk_batt_psy,
                POWER_SUPPLY_PROP_SYSTEM_TEMP_LEVEL, &val);
    if (rc) {
        dev_dbg(info->dev, "Cannot get system level from bk btt\n");
        val.intval = 0;
    }
    system_level = val.intval;

    rc = get_prop_from_psy(info->bk_batt_psy,
                POWER_SUPPLY_PROP_BATTERY_CHARGING_ENABLED, &val);
    if (rc) {
        dev_err(info->dev, "Cannot get battery charging "
                                        "enabled from bk batt\n");
        val.intval = 0;
    }
    bch_en = val.intval;

    rc = get_prop_from_psy(info->bk_batt_psy,
                POWER_SUPPLY_PROP_INPUT_CURRENT_MAX, &val);
    if (rc) {
        dev_err(info->dev, "Cannot get aicl level from bk battery\n");
        val.intval = 0;
    }
    icur_max = val.intval;

    snprintf(reg_value, MAX_SIZE, "%-16d %-9d %-11d ",
                            system_level, bch_en, icur_max);

    battery_dump_register_raw(info, info->dump_regs,
                            info->dump_regs_num, true, reg_value);
    if (info->parallel_regs_num > 0) {
        battery_dump_register_raw(info, info->parallel_dump_regs,
                            info->parallel_regs_num, false, reg_value);
    }
}

/***************************************************
 Function: get_batt_health
 Description: get the real battery health
 Parameters:
    info            huawei_battery_info
 Return:
    negative mean fail
    others are battery health, possiable value:
        POWER_SUPPLY_HEALTH_OVERHEAT
        POWER_SUPPLY_HEALTH_COLD
        POWER_SUPPLY_HEALTH_WARM
        POWER_SUPPLY_HEALTH_COOL
        POWER_SUPPLY_HEALTH_GOOD
***************************************************/
static int get_batt_health(struct huawei_battery_info *info)
{
    int rc = 0, batt_temp = 0;
    union power_supply_propval val = {0, };

    if (!info) {
        pr_err("Invali param for %s\n", __func__);
        return -EINVAL;
    }

    if (IS_ENABLED(CONFIG_HLTHERM_RUNTEST) || chg_no_temp_limit) {
        return POWER_SUPPLY_HEALTH_GOOD;
    }

    rc = get_prop_from_psy(info->bk_batt_psy, POWER_SUPPLY_PROP_TEMP, &val);
    if (rc < 0) {
        pr_err("Cannot get battery temp\n");
        return -EINVAL;
    }

    batt_temp = val.intval;
    if (batt_temp > BATT_HEALTH_TEMP_HOT)
        return POWER_SUPPLY_HEALTH_OVERHEAT;
    else if (batt_temp < BATT_HEALTH_TEMP_COLD)
        return POWER_SUPPLY_HEALTH_COLD;
    else if (batt_temp > BATT_HEALTH_TEMP_WARM)
        return POWER_SUPPLY_HEALTH_WARM;
    else if (batt_temp < BATT_HEALTH_TEMP_COOL)
        return POWER_SUPPLY_HEALTH_COOL;
    else
        return POWER_SUPPLY_HEALTH_GOOD;
}

/***************************************************
 Function: get_prop_charge_full_design
 Description: get charge full design from fg psy
 Parameters:
    info            huawei_battery_info
 Return:
    battery full design capacity
***************************************************/
#define DEFAULT_BATT_FULL_CHG_CAPACITY  0
static int get_prop_charge_full_design(struct huawei_battery_info *info)
{
    int rc = 0;
    union power_supply_propval val = {0, };

    if (!info) {
        pr_err("%s: Invalid param, fatal error\n", __func__);
        return -EINVAL;
    }

    if (info->charger_batt_capacity_mah > 0) {
        return info->charger_batt_capacity_mah;
    }

    rc = get_prop_from_psy(info->bms_psy, POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN, &val);
    if (rc < 0) {
        pr_err("Couldnot get charge full rc = %d\n", rc);
        return DEFAULT_BATT_FULL_CHG_CAPACITY;
    }
    return val.intval;
}

/***************************************************
 Function: get_prop_batt_resistance_id
 Description: get battery id ressitance
 Parameters:
    info            huawei_battery_info
 Return:
    battery id resistance
***************************************************/
#define DEFAULT_BATT_RESISTANCE_ID  0
static int get_prop_batt_resistance_id(struct huawei_battery_info *info)
{
    int rc = 0;
    union power_supply_propval val = {0, };

    if (!info) {
        pr_err("%s: Invalid param, fatal error\n", __func__);
        return -EINVAL;
    }

    rc = get_prop_from_psy(info->bms_psy, POWER_SUPPLY_PROP_RESISTANCE_ID, &val);
    if (rc < 0) {
        pr_err("Couldn't get resistance id rc = %d\n", rc);
        return DEFAULT_BATT_RESISTANCE_ID;
    }
    return val.intval;
}

/***************************************************
 Function: get_prop_charge_full
 Description: get charge full capacity
 Parameters:
    info            huawei_battery_info
 Return:
    battery charge full capacity, learned by FG
***************************************************/
#define DEFAULT_BATT_FULL_CHG_CAPACITY  0
static int get_prop_charge_full(struct huawei_battery_info *info)
{
    int rc = 0;
    union power_supply_propval val = {0, };

    if (!info) {
        pr_err("%s: Invalid param, fatal error\n", __func__);
        return -EINVAL;
    }

    rc = get_prop_from_psy(info->bms_psy, POWER_SUPPLY_PROP_CHARGE_FULL, &val);
    if (rc < 0) {
        pr_err("Couldn't get charge_full rc = %d\n", rc);
        return DEFAULT_BATT_FULL_CHG_CAPACITY;
    }
    return val.intval;
}

/***************************************************
 Function: get_prop_batt_profile
 Description: get battery profile status
 Parameters:
    info            huawei_battery_info
 Return:
    battery profile status
***************************************************/
static int get_prop_batt_profile(struct huawei_battery_info *info)
{
    int rc = 0;
    union power_supply_propval val = {0, };

    if (!info) {
        pr_err("%s: Invalid param, fatal error\n", __func__);
        return -EINVAL;
    }

    rc = get_prop_from_psy(info->bms_psy, POWER_SUPPLY_PROP_PROFILE_STATUS, &val);
    if (rc < 0) {
        pr_err("Couldn't get profile status rc = %d\n", rc);
        return 0;
    }
    return val.intval;
}

/***************************************************
 Function: get_prop_cycle_count
 Description: get battery charging cycle count
 Parameters:
    info            huawei_battery_info
 Return:
    battery charging cycle count
***************************************************/
static int get_prop_cycle_count(struct huawei_battery_info *info)
{
    int rc = 0;
    union power_supply_propval val = {0, };

    if (!info) {
        pr_err("%s: Invalid param, fatal error\n", __func__);
        return -EINVAL;
    }

    rc = get_prop_from_psy(info->bms_psy, POWER_SUPPLY_PROP_CYCLE_COUNT, &val);
    if (rc < 0) {
        pr_err("Couldnot get cycle count, rc = %d\n", rc);
        return 0;
    }
    return val.intval;
}

/***************************************************
 Function: set_prop_iin_thermal
 Description: set iin thermal current limit
 Parameters:
    info            huawei_battery_info
    val             iin thermal limit
 Return:
    0 mean set current limit success
    negative mean set iin thermal limit fail
***************************************************/
static int set_prop_iin_thermal(
            struct huawei_battery_info *info, int val)
{
    int rc = 0;

    if (!info) {
        pr_err("%s: Invalid param, fatal error\n", __func__);
        return -EINVAL;
    }

    if (0 >= val) {
        info->thermal_limit_current = info->usb_icl_max;
    } else {
        info->thermal_limit_current = min(val, info->usb_icl_max);
    }
    if (!info->ibus_detecting) {
        rc = vote(info->usb_icl_votable, IN_THERMAL_VOTER,
                            true, info->thermal_limit_current);
    }
    return rc;
}

/***************************************************
 Function: set_prop_safety_timer_enable
 Description: enable/disable charging safety timer
 Parameters:
    info            huawei_battery_info
    user_timer      enable/disable flag
 Return:
    0 mean enable/disable timer success
    negative mean enable/disable timer fail
***************************************************/
static int set_prop_safety_timer_enable(
            struct huawei_battery_info *info, int use_timer)
{
    int rc = 0;
    union power_supply_propval val = {0, };

    if (!info) {
        pr_err("%s: Invalid param, fatal error\n", __func__);
        return -EINVAL;
    }

    if (chg_no_timer || IS_ENABLED(CONFIG_HLTHERM_RUNTEST)) {
        val.intval = 0;
    } else {
        val.intval = use_timer;
    }

    rc = set_prop_to_psy(info->bk_batt_psy,
                        POWER_SUPPLY_PROP_SAFETY_TIMER_ENABLE, &val);
    if (rc) {
        pr_err("Set prop %d fail, rc %d\n",
                        POWER_SUPPLY_PROP_SAFETY_TIMER_ENABLE, rc);
    }
    return rc;
}

/***************************************************
 Function: set_prop_input_current_max
 Description: set max input current
 Parameters:
    info            huawei_battery_info
    curr            input current value
 Return:
    0 mean enable/disable timer success
    negative mean enable/disable timer fail
***************************************************/
static int set_prop_input_current_max(
            struct huawei_battery_info *info, int curr)
{
    int rc = 0;

    if (!info) {
        pr_err("%s: Invalid param, fatal error\n", __func__);
        return -EINVAL;
    }

    rc = vote(info->usb_icl_votable, ICL_USER_VOTER, true, curr);
    return rc;
}

/* battery power supply */
static char *battery_supplicants[] = {
    "bk_battery",
    "bms",
};
static enum power_supply_property battery_ext_props[] = {
    POWER_SUPPLY_PROP_IN_THERMAL,
    POWER_SUPPLY_PROP_CYCLE_COUNT,
    POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN,
    POWER_SUPPLY_PROP_RESISTANCE_ID,
    POWER_SUPPLY_PROP_CHARGE_FULL,
    POWER_SUPPLY_PROP_PROFILE_STATUS,
    POWER_SUPPLY_PROP_SAFETY_TIMER_ENABLE,
};

/***************************************************
 Function: battery_get_property
 Description: get battery power supply property interface
 Parameters:
    psy             power supply instance
    prop            the property need to get
    val             the place to hold values
 Return:
    0 mean get prop successful
    negative mean get prop fail
 Usage:
    For the properties we have to handle in this driver,
 need to return directly after we get the valid property
 value, otherwise we will get the property value from
 original charger driver and then do post process.
***************************************************/
static int battery_get_property(struct power_supply *psy,
                enum power_supply_property prop,
                union power_supply_propval *val)
{
    int rc = 0;
    struct huawei_battery_info *info = NULL;

    if (!psy || !val) {
        pr_err("%s: Invalid param, fatal error\n", __func__);
        return -EINVAL;
    }

    info = container_of(psy, struct huawei_battery_info, batt_psy);

    /* preprocess */
    switch(prop) {
    case POWER_SUPPLY_PROP_IN_THERMAL:
        val->intval = info->thermal_limit_current;
        return 0;
    case POWER_SUPPLY_PROP_HEALTH:
        val->intval = get_batt_health(info);
        if (val->intval < 0) {
            break;
        }
        return 0;
    case POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN:
        val->intval = get_prop_charge_full_design(info);
        return 0;
    case POWER_SUPPLY_PROP_RESISTANCE_ID:
        val->intval = get_prop_batt_resistance_id(info);
        return 0;
    case POWER_SUPPLY_PROP_CHARGE_FULL:
        val->intval = get_prop_charge_full(info);
        return 0;
    case POWER_SUPPLY_PROP_PROFILE_STATUS:
        val->intval = get_prop_batt_profile(info);
        return 0;
    case POWER_SUPPLY_PROP_CYCLE_COUNT:
        val->intval = get_prop_cycle_count(info);
        return 0;
    case POWER_SUPPLY_PROP_CAPACITY:
        if (info->fake_soc >= EMPTY_SOC &&
                    info->fake_soc <= FULL_SOC) {
            val->intval = info->fake_soc;
            return 0;
        }
        break;
    case POWER_SUPPLY_PROP_REGISTER_HEAD:
        battery_get_register_head(info, (char *)val->strval);
        return 0;
    case POWER_SUPPLY_PROP_DUMP_REGISTER:
        battery_dump_register(info, (char *)val->strval);
        return 0;
    case POWER_SUPPLY_PROP_TEMP:
        if (IS_ENABLED(CONFIG_HLTHERM_RUNTEST) || chg_no_temp_limit) {
            val->intval = DEFAULT_BATT_TEMP;
            return 0;
        }
        break;
    case POWER_SUPPLY_PROP_INPUT_CURRENT_MAX:
        val->intval = info->usb_icl_max;
        return 0;
    default:
        break;
    }

    /* call bk_battery function */
    rc = get_prop_from_psy(info->bk_batt_psy, prop, val);

    /* post process */
    switch(prop) {
    case POWER_SUPPLY_PROP_SYSTEM_TEMP_LEVEL:
        if (rc < 0) {
            val->intval = 0;
            rc = 0;
        }
        break;
    case POWER_SUPPLY_PROP_CHARGER_PRESENT:
        if (rc < 0) {
            rc = get_prop_from_psy(info->usb_psy, prop, val);
        }
        break;
    case POWER_SUPPLY_PROP_CAPACITY:
        if (factory_mode &&
                    val->intval <= FACTORY_SOC_LOW) {
            val->intval = FACTORY_SOC_LOW;
            return 0;
        }
        break;
    default:
        break;
    }

    return rc;
}

/***************************************************
 Function: battery_set_property
 Description: set battery power supply property interface
 Parameters:
    psy             power supply instance
    prop            the property need to set
    val             the place to hold values
 Return:
    0 mean set prop successful
    negative mean get prop fail
 Usage:
    For the properties we have to handle in this driver,
 need to return directly after we set the property
 value, otherwise we will set the property value to
 original charger driver and then do post process.
***************************************************/
static int battery_set_property(struct power_supply *psy,
                enum power_supply_property prop,
                const union power_supply_propval *val)
{
    int rc = 0;
    struct huawei_battery_info *info = NULL;

    if (!psy || !val) {
        pr_err("%s: Invalid param, fatal error\n", __func__);
        return -EINVAL;
    }

    info = container_of(psy, struct huawei_battery_info, batt_psy);

    /* pre process */
    switch(prop) {
    case POWER_SUPPLY_PROP_IN_THERMAL:
        rc = set_prop_iin_thermal(info, val->intval);
        goto OUT;
    case POWER_SUPPLY_PROP_CAPACITY:
        info->fake_soc = val->intval;
        goto OUT;
    case POWER_SUPPLY_PROP_SAFETY_TIMER_ENABLE:
        rc = set_prop_safety_timer_enable(info, val->intval);
        goto OUT;
    case POWER_SUPPLY_PROP_INPUT_CURRENT_MAX:
        rc = set_prop_input_current_max(info, val->intval);
        goto OUT;
    case POWER_SUPPLY_PROP_BATTERY_CHARGING_ENABLED:
        rc = vote(info->bch_votable, BCH_USER_VOTER, !val->intval, 0);
        pr_err("%s: bch_votable charging_suspend_cb = %d\n", __func__, !val->intval);
        goto OUT;
    case POWER_SUPPLY_PROP_VOLTAGE_BASP:
        rc = vote(info->vbat_votable, VBAT_BASP_VOTER, true, val->intval);
        goto OUT;
    case POWER_SUPPLY_PROP_VOLTAGE_MAX:
        rc = vote(info->vbat_votable, VBAT_USER_VOTER, true, val->intval);
        goto OUT;
    case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX:
        rc = vote(info->fcc_votable, FCC_USER_VOTER, true, val->intval);
        goto OUT;
    default:
        break;
    }

    /* call bk_battery function */
    rc = set_prop_to_psy(info->bk_batt_psy, prop, val);

    /* post process */
    switch(prop) {
    default:
        break;
    }

OUT:
    power_supply_changed(&info->batt_psy);
    return rc;
}

/***************************************************
 Function: battery_property_is_writeable
 Description: check battery property writeable
 Parameters:
    psy             power supply instance
    prop            the property need to check
 Return:
    0 mean property is not writeable
    1 mean property is writeable
***************************************************/
static int battery_property_is_writeable(struct power_supply *psy,
                enum power_supply_property prop)
{
    int rc = 0;
    struct huawei_battery_info *info = NULL;

    if (!psy) {
        pr_err("%s: Invalid param, fatal error\n", __func__);
        return 0;
    }

    info = container_of(psy, struct huawei_battery_info, batt_psy);

    /* pre process */
    switch(prop) {
    case POWER_SUPPLY_PROP_IN_THERMAL:
    case POWER_SUPPLY_PROP_CAPACITY:
    case POWER_SUPPLY_PROP_SAFETY_TIMER_ENABLE:
    case POWER_SUPPLY_PROP_INPUT_CURRENT_MAX:
        rc = 1;
        return rc;
    default:
        break;
    }

    rc = info->bk_batt_psy->property_is_writeable(info->bk_batt_psy, prop);
    if (rc < 0) {
        rc = 0;
        dev_warn(info->dev, "Check prop %d is writeable fail\n", prop);
    }

    /* post process */
    switch(prop) {
    default:
        break;
    }

    return rc;
}

/***************************************************
 Function: battery_external_power_changed
 Description: the psy cared by battery changed
 Parameters:
    psy             power supply instance
 Return: NA
***************************************************/
static void battery_external_power_changed(struct power_supply *psy)
{
    struct huawei_battery_info *info = NULL;
    union power_supply_propval val = {0, };
    int rc = 0, usb_curr = 0, usb_type = 0;
    int usb_present = false;

    if (!psy) {
        pr_err("%s: Invalid param, fatal error\n", __func__);
        return;
    }

    info = container_of(psy, struct huawei_battery_info, batt_psy);

    rc = get_prop_from_psy(info->usb_psy, POWER_SUPPLY_PROP_PRESENT, &val);
    if (rc < 0) {
        dev_err(info->dev, "Get Usb present status fail, rc = %d\n", rc);
        goto OUT;
    }
    usb_present = val.intval;

    /* PROP_PRESENT is true means usb is present */
    if (true == usb_present) {
        /* usb present */
        schedule_delayed_work(&info->monitor_charging_work, 0);
    } else {
        cancel_delayed_work(&info->monitor_charging_work);
        cancel_delayed_work(&info->ibus_detect_work);
        info->ibus_detecting = false;
        info->current_adjusted_ma = 0;
        vote(info->usb_icl_votable, IBUS_DETECT_VOTER, true, info->usb_icl_max);
        goto OUT;
    }

    rc = get_prop_from_psy(info->usb_psy, POWER_SUPPLY_PROP_TYPE, &val);
    if (rc < 0) {
        dev_err(info->dev, "Get Usb type fail, rc = %d\n", rc);
        goto OUT;
    }
    usb_type = val.intval;

    rc = get_prop_from_psy(info->usb_psy, POWER_SUPPLY_PROP_CURRENT_MAX, &val);
    if (rc < 0) {
        dev_err(info->dev, "Cannot get current max from usb psy\n");
        goto OUT;
    }
    usb_curr = val.intval;

    if (POWER_SUPPLY_TYPE_USB == usb_type && usb_curr < chg_usb_current) {
        /* need to change usb max current */
        val.intval = chg_usb_current;
        rc = set_prop_to_psy(info->usb_psy, POWER_SUPPLY_PROP_CURRENT_MAX, &val);
        if (rc) {
            dev_err(info->dev, "Set usb current max fail, rc = %d\n", rc);
            goto OUT;
        } else {
            /* Donot need to do more */
            return;
        }
    }

    if (POWER_SUPPLY_TYPE_USB == usb_type || POWER_SUPPLY_TYPE_USB_CDP == usb_type) {
        dev_info(info->dev, "Neednot adjust ibus for usb/cdp\n");
        goto OUT;
    }

    if (usb_curr / MA_TO_UA > info->current_adjusted_ma) {
        /* need to start current adjust again */
        battery_input_current_adjust(info);
    }

OUT:
    power_supply_changed(&info->batt_psy);
    huawei_handle_charger_event();
}

/***************************************************
 Function: battery_initial_status
 Description: determine the battery init status
 Parameters:
    info            huawei_battery_info
 Return:
    0 mean init status successful
    negative mean fail
***************************************************/
#define CHG_UNSATE_TIMER_CURRENT 900000
static int battery_initial_status(struct huawei_battery_info *info)
{
    /* use -1 as invalid soc */
    info->fake_soc = -1;

    if (!info) {
        pr_err("%s: Invalid param, fatal error\n", __func__);
        return -EINVAL;
    }

    if (IS_ENABLED(CONFIG_HLTHERM_RUNTEST)) {
        /* for high/low temp test
         * 1. disable charging temp limit
         * 2. disable charging safety timer
         */
        chg_no_timer = 1;
        set_prop_safety_timer_enable(info, false);
    } else if (IS_ENABLED(CONFIG_LONG_TIME_TEST_WITH_USB)) {
        /* for long time test
         * 1. disalbe charging safety timer
         * 2. enlarge usb curren to 900mA
         */
        chg_no_timer = 1;
        chg_usb_current = CHG_UNSATE_TIMER_CURRENT;
        set_prop_safety_timer_enable(info, false);
        batt_set_usb_current(info);
    }

    return 0;
}

/***************************************************
 Function: of_dev_node_match
 Description: tool function, used to check device node
        match status
 Parameters:
    dev             the device need to check
    data            device node
 Return:
    true device has the same device node with data
    false mean it's not match
***************************************************/
static int of_dev_node_match(struct device *dev, void *data)
{
    if (!dev || !data) {
        pr_err("%s: Invalid param, fatal error\n", __func__);
        return 0;
    }

    return dev->of_node == data;
}

/***************************************************
 Function: battery_parse_spmi_info
 Description: parse device node for spmi bus
 Parameters:
    node            device node need to parse
    info            huawei_battery_info
 Return:
    0 mean parse device node successful
    negative mean parse device node fail
***************************************************/
static int battery_parse_spmi_info(struct device_node *node,
                struct huawei_battery_info *info)
{
    int rc = 0;
    struct bus_type *spmi_bus_type = NULL;
    struct spmi_bus_info *spmi_info = NULL;
    struct device_node *bk_batt_node = NULL;

    if (!node || !info) {
        pr_err("%s: Invalid param, fatal error\n", __func__);
        return -EINVAL;
    }

    spmi_info = &info->spmi_info;
    /* bus num is always 0 */
    spmi_info->spmi_ctrl = spmi_busnum_to_ctrl(0);
    if (!spmi_info->spmi_ctrl) {
        dev_err(info->dev, "spmi ctrl is invalid\n");
        rc = -EINVAL;
        goto ERR;
    }
    spmi_bus_type = spmi_info->spmi_ctrl->dev.bus;
    bk_batt_node = of_parse_phandle(node, "huawei,spmi-charger", 0);
    if (!bk_batt_node) {
        dev_err(info->dev, "Missing spmi-charger config\n");
        rc = -EINVAL;
        goto ERR;
    }
    info->bk_battery_dev = bus_find_device(spmi_bus_type, NULL,
                    bk_batt_node, of_dev_node_match);
    if (!info->bk_battery_dev) {
        dev_err(info->dev, "Cannot find the spmi charger device\n");
        rc = -ENODEV;
        goto ERR;
    }
    info->battery_reg_read = spmi_reg_read;
    info->battery_reg_write = spmi_reg_write;
    return 0;

ERR:
    return rc;
}

/***************************************************
 Function: battery_parse_i2c_info
 Description: parse device node for i2c bus
 Parameters:
    node            device node need to parse
    info            huawei_battery_info
 Return:
    0 mean parse device node successful
    negative mean parse device node fail
***************************************************/
static int battery_parse_i2c_info(struct device_node *node,
                    struct huawei_battery_info *info)
{
    int rc = 0;
    struct device_node *bk_batt_node = NULL;
    struct i2c_client *i2c_client = NULL;
    struct i2c_bus_info *i2c_info = NULL;

    if (!node || !info) {
        pr_err("%s: invalid param, fatal error\n", __func__);
        return -EINVAL;
    }

    i2c_info = &info->i2c_info;
    bk_batt_node = of_parse_phandle(node, "huawei,i2c-charger", 0);
    if (!bk_batt_node) {
        dev_err(info->dev, "Missging i2c-charger config\n");
        rc = -EINVAL;
        goto ERR;
    }
    i2c_client = of_find_i2c_device_by_node(bk_batt_node);
    if (!i2c_client) {
        dev_err(info->dev, "Cannot find i2c client, please check the config\n");
        rc = -EINVAL;
        goto ERR;
    }
    i2c_info->i2c_adapter = i2c_client->adapter;
    i2c_info->i2c_addr = i2c_client->addr;
    i2c_info->i2c_flags = i2c_client->flags;
    info->bk_battery_dev = &i2c_client->dev;
    info->battery_reg_read = i2c_reg_read;
    info->battery_reg_write = i2c_reg_write;
    return 0;

ERR:
    return rc;
}

/***************************************************
 Function: battery_parse_parallel_charger
 Description: parse parall charger device node config
 Parameters:
    node            device node need to parse
    info            huawei_battery_info
 Return:
    0 mean parse device node successful
    negative mean parse device node fail
***************************************************/
static int battery_parse_parallel_charger(struct device_node *node,
                    struct huawei_battery_info *info)
{
    int rc = 0;
    struct device_node *parallel_charger = NULL;
    struct i2c_client *i2c_client = NULL;
    struct i2c_bus_info *i2c_info = NULL;

    if (!node || !info) {
        pr_err("%s: invalid param, fatal error\n", __func__);
        return -EINVAL;
    }

    i2c_info = &info->second_i2c_info;
    parallel_charger = of_parse_phandle(node, "qcom,parallel-charger", 0);
    if (!parallel_charger) {
        dev_err(info->dev, "Missing parallel charger\n");
        rc = -ENODEV;
        goto ERR;
    }
    i2c_client = of_find_i2c_device_by_node(parallel_charger);
    if (!i2c_client) {
        dev_err(info->dev, "Cannot find i2c client, please check the config\n");
        rc = -EINVAL;
        goto ERR;
    }
    i2c_info->i2c_adapter = i2c_client->adapter;
    i2c_info->i2c_addr = i2c_client->addr;
    i2c_info->i2c_flags = i2c_client->flags;
    info->battery_second_reg_read = i2c_reg_read;
    info->battery_second_reg_write = i2c_reg_write;

    return 0;

ERR:
    return rc;
}

/***************************************************
 Function: battery_parse_dump_regs
 Description: parse the registers need to dump for
        charge log
 Parameters:
    node            device node need to parse
    info            huawei_battery_info
 Return:
    0 mean parse device node successful
    negative mean parse device node fail
***************************************************/
static int battery_parse_dump_regs(struct device_node *node,
                struct huawei_battery_info *info)
{
    int rc = 0;

    if (!node || !info) {
        pr_err("%s: invalid param, fatal error\n", __func__);
        return -EINVAL;
    }

    rc = of_property_read_u32(node, "huawei,dump-regs-num",
                                        &info->dump_regs_num);
    if (rc < 0) {
        dev_info(info->dev, "No dump regs decleared\n");
        info->dump_regs_num = 0;
        info->dump_regs = NULL;
    } else {
        info->dump_regs = devm_kzalloc(info->dev,
                    sizeof(u32) * info->dump_regs_num, GFP_KERNEL);
        if (!info->dump_regs) {
            dev_err(info->dev, "Alloc memory fail\n");
            rc = -ENOMEM;
            goto ALLOC_FAIL;
        }
        rc = of_property_read_u32_array(node, "huawei,dump-regs",
                    info->dump_regs, info->dump_regs_num);
        if (rc < 0) {
            dev_err(info->dev, "dump regs dismatch, please check\n");
            goto READ_FAIL;
        }
    }


    if (!info->support_parallel_charger) {
        dev_dbg(info->dev, "Donot support parallel charger\n");
        return 0;
    }

    rc = of_property_read_u32(node, "huawei,parallel-regs-num",
                                        &info->parallel_regs_num);
    if (rc < 0) {
        dev_dbg(info->dev, "No dump regs for parallel charger\n");
        info->parallel_regs_num = 0;
        info->parallel_dump_regs = NULL;
    } else {
        info->parallel_dump_regs = devm_kzalloc(info->dev,
                    sizeof(u32) * info->parallel_regs_num, GFP_KERNEL);
        if (!info->parallel_dump_regs) {
            dev_err(info->dev, "Alloc memory fail\n");
            rc = -ENOMEM;
            goto ALLOC_FAIL_1;
        }
        rc = of_property_read_u32_array(node, "huawei,parallel-dump-regs",
                    info->parallel_dump_regs, info->parallel_regs_num);
        if (rc < 0) {
            dev_err(info->dev, "parallel regs dismatch, please check\n");
            goto READ_FAIL_1;
        }
    }

    return 0;

READ_FAIL_1:
    devm_kfree(info->dev, info->parallel_dump_regs);
    info->parallel_dump_regs = NULL;
    info->parallel_regs_num = 0;
ALLOC_FAIL_1:
READ_FAIL:
    devm_kfree(info->dev, info->dump_regs);
    info->dump_regs = NULL;
    info->dump_regs_num = 0;
ALLOC_FAIL:
    return rc;
}

/***************************************************
 Function: battery_parse_bus_type
 Description: this driver support spmi & i2c device both,
        need to confirm which bus used by the real board
 Parameters:
    node            device node need to parse
    info            huawei_battery_info
 Return:
    0 mean parse device node successful
    negative mean parse device node fail
***************************************************/
static int battery_parse_bus_type(struct device_node *node,
                struct huawei_battery_info *info)
{
    int rc = 0;

    if (!node || !info) {
        pr_err("%s: Invalid param, fatal error\n", __func__);
        return -EINVAL;
    }

    /* Supported bus type */
    rc = of_property_read_u32(node, "huawei,bus-type", &info->bus_type);
    if (rc) {
        dev_err(info->dev, "Cannot get bus type %d\n", rc);
        return rc;
    }
    switch(info->bus_type) {
    case SPMI_BUS:
        dev_info(info->dev, "Support SPMI Bus\n");
        rc = battery_parse_spmi_info(node, info);
        break;
    case I2C_BUS:
        dev_info(info->dev, "Support I2C Bus\n");
        rc = battery_parse_i2c_info(node, info);
        break;
    default:
        dev_err(info->dev, "Invalid Bus Type, please check\n");
        return -EINVAL;
    }
    if (rc) {
        dev_err(info->dev, "Parse bus config fail, rc = %d\n", rc);
        return rc;
    }

    /* Parallel charger */
    info->support_parallel_charger = of_property_read_bool(node,
                    "huawei,support-parallel-charger");
    if (!info->support_parallel_charger) {
        dev_dbg(info->dev, "Not support parallel charger\n");
        info->battery_second_reg_read = dummy_reg_read;
        info->battery_second_reg_write = dummy_reg_write;
    } else {
        rc = battery_parse_parallel_charger(node, info);
        if (rc < 0) {
            dev_err(info->dev, "Parse parallel chg config fail,"
                        " rc = %d\n", rc);
            return rc;
        }
    }
    return 0;
}

/***************************************************
 Function: battery_parse_jeita_config
 Description: parse jeita config
 Parameters:
    node            device node need to parse
    info            huawei_battery_info
 Return:
    0 mean parse device node successful
    negative mean parse device node fail
***************************************************/
#define OF_READ_U32(name, var, def) \
    rc = of_property_read_u32(node, "huawei," name, &var); \
    if (rc < 0) { \
        dev_info(info->dev, "Cannot get " name ", use default %d\n", def); \
        var = def; \
    }

static int battery_parse_jeita_config(struct device_node *node,
                struct huawei_battery_info *info)
{
    int rc = 0;
    u32 tmp[CHG_DIS_REG_NUM] = {0};

    if (!node || !info) {
        pr_err("%s: Invalid param, fatal error\n", __func__);
        return -EINVAL;
    }

    info->jeita_use_interrupt = of_property_read_bool(node,
                                    "huawei,jeita-use-interrupt");
    if (!info->jeita_use_interrupt) {
        dev_info(info->dev, "Using loop solutino for jeita\n");
        return 0;
    }

    dev_info(info->dev, "Using interrupt solution for jeita\n");
    info->jeita_hw_curr_limit = of_property_read_bool(node,
                    "huawei,jeita-hardware-current-limit");
    info->jeita_hw_volt_limit = of_property_read_bool(node,
                    "huawei,jeita-hardware-voltage-limit");
    info->jeita_hw_chg_dis = of_property_read_bool(node,
                    "huawei,jeita-hardware-charge-disable");
	if (IS_ENABLED(CONFIG_HLTHERM_RUNTEST))	{
		info->jeita_hw_chg_dis = false;
	}
    dev_info(info->dev, "Using %s curr limit, %s volt limit, %s chg disable\n",
                info->jeita_hw_curr_limit ? "hardware" : "software",
                info->jeita_hw_volt_limit ? "hardware" : "software",
                info->jeita_hw_chg_dis ? "hardware" : "software");
    if (!info->jeita_hw_chg_dis) {
        return 0;
    }
    rc = of_property_read_u32_array(node,
                "huawei,jeita-hardware-charge-disable-reg", tmp, CHG_DIS_REG_NUM);
    if (rc) {
        dev_err(info->dev, "Get jeita hw chg dis reg fail, rc = %d\n", rc);
        info->jeita_hw_chg_dis = false;
    } else {
        info->jeita_chg_dis_reg = tmp[0];
        info->jeita_chg_dis_mask = tmp[1];
        info->jeita_chg_dis_value = tmp[2];
        dev_info(info->dev, "jeita param 0x%x, 0x%x, 0x%x\n",
                info->jeita_chg_dis_reg,
                info->jeita_chg_dis_mask,
                info->jeita_chg_dis_value);
    }

    return 0;
}

/***************************************************
 Function: battery_parse_config
 Description: parse curr & volt config
 Parameters:
    node            device node need to parse
    info            huawei_battery_info
 Return:
    0 mean parse device node successful
    negative mean parse device node fail
***************************************************/
static int battery_parse_config(struct device_node *node,
                struct huawei_battery_info *info)
{
    int rc = 0;

    if (!node || !info) {
        pr_err("%s: Invalid param, fatal error\n", __func__);
        return -EINVAL;
    }

    OF_READ_U32("fastchg-max-current",
                info->fastchg_max_current, DEFAULT_FASTCHG_MAX_CURRENT);
    pr_info("%s: fastchg max current %dmA\n",
                __func__, info->fastchg_max_current);

    OF_READ_U32("fastchg-max-voltage",
                info->fastchg_max_voltage, DEFAULT_FASTCHG_MAX_VOLTAGE);
    pr_info("%s: fastchg max voltage %dmV\n",
                __func__, info->fastchg_max_voltage);

    OF_READ_U32("fastchg-warm-current",
                info->fastchg_warm_current, DEFAULT_FASTCHG_WARM_CURRENT);
    pr_info("%s: fastchg warm current %dmA\n",
                __func__, info->fastchg_warm_current);

    OF_READ_U32("fastchg-warm-voltage",
                info->fastchg_warm_voltage, DEFAULT_FASTCHG_WARM_VOLTAGE);
    pr_info("%s: fastchg warm voltage %dmV\n",
                __func__, info->fastchg_warm_voltage);

    OF_READ_U32("fastchg-cool-current",
                info->fastchg_cool_current, DEFAULT_FASTCHG_COOL_CURRENT);
    pr_info("%s: fastchg cool current %dmA\n",
                __func__, info->fastchg_cool_current);

    OF_READ_U32("fastchg-cool-voltage",
                info->fastchg_cool_voltage, DEFAULT_FASTCHG_COOL_VOLTAGE);
    pr_info("%s: fastchg cool voltage %dmV\n",
                __func__, info->fastchg_cool_voltage);

    OF_READ_U32("usb-icl-max", info->usb_icl_max, DEFAULT_USB_ICL_CURRENT);
    pr_info("%s: usb icl max %dmA\n", __func__, info->usb_icl_max);

    OF_READ_U32("customize-cool-lower-limit",
                info->customize_cool_lower_limit, DEFAULT_CUSTOM_COOL_LOW_TH);
    pr_info("%s: customize cool lower limit %d\n",
                __func__, info->customize_cool_lower_limit);

    OF_READ_U32("customize-cool-upper-limit",
                info->customize_cool_upper_limit, DEFAULT_CUSTOM_COOL_UP_TH);
    pr_info("%s: customize cool upper limit %d\n",
                __func__, info->customize_cool_upper_limit);

    OF_READ_U32("fastchg-current-customize-cool-ma",
                info->fastchg_current_customize_cool, DEFAULT_CUSTOM_COOL_CURRENT);
    pr_info("%s: fastchg current cusome cool ma %d\n",
                __func__, info->fastchg_current_customize_cool);

    OF_READ_U32("charger-batt-capacity-mah", info->charger_batt_capacity_mah, 0);
    pr_info("%s: charger battery capacity mah is %d\n",
                __func__, info->charger_batt_capacity_mah);

    info->ibus_detect_disable = of_property_read_bool(node,
                                    "huawei,ibus-detect-disable");
    pr_info("%s: ibus detect feature is %s\n",
                __func__, info->ibus_detect_disable ? "disabled" : "enabled");
    return 0;
}

/***************************************************
 Function: battery_parse_dt
 Description: parse dts config interface
 Parameters:
    node            device node need to parse
    info            huawei_battery_info
 Return:
    0 mean parse device node successful
    negative mean parse device node fail
***************************************************/
static int battery_parse_dt(struct huawei_battery_info *info)
{
    int rc = 0;
    struct device_node *node = NULL;
    u32 tmp[CHG_OVP_REG_NUM] = {0};

    if (!info) {
        pr_err("%s: Invalid param, fatal error\n", __func__);
        return -EINVAL;
    }

    node = info->dev->of_node;
    rc = battery_parse_bus_type(node, info);
    if (rc) {
        return rc;
    }

    rc = battery_parse_dump_regs(node, info);
    if (rc) {

        return rc;
    }

    rc = battery_parse_jeita_config(node, info);
    if (rc) {
        return rc;
    }

    rc = battery_parse_config(node, info);
    if (rc) {
        return rc;
    }

    return 0;
}

/***************************************************
 Function: sync_max_voltage
 Description: sync max voltage between bms and bk battery
 Parameters:
    info            huawei_battery_info
 Return:
    0 means sccuess, negative means error
***************************************************/
static int sync_max_voltage(struct huawei_battery_info *info)
{
    int rc = 0;
    union power_supply_propval val = {0, };

    if (!info) {
        pr_err("%s: Invalid param, fatal error\n", __func__);
        return -EINVAL;
    }

    rc = info->bms_psy->get_property(info->bms_psy,
                    POWER_SUPPLY_PROP_VOLTAGE_MAX, &val);
    if (rc < 0) {
        return rc;
    }

    rc = vote(info->vbat_votable, VBAT_BASP_VOTER, true, val.intval);

    return rc;
}

/***************************************************
 Function: battery_prepare_properties
 Description: prepare the properties battey psy support
 Parameters:
    info                huawei_battery_info
 Return:
    0 mean prepare properties successful
    negative mean prepare fail
***************************************************/
static int battery_prepare_properties(struct huawei_battery_info *info)
{
    int bk_batt_props = 0;
    int batt_ext_props = ARRAY_SIZE(battery_ext_props);

    if (!info) {
        pr_err("%s: Invalid param, fatal error\n", __func__);
        return -EINVAL;
    }

    bk_batt_props = info->bk_batt_psy->num_properties;
    info->num_props = bk_batt_props + batt_ext_props;
    info->batt_props = devm_kzalloc(info->dev,
        info->num_props * sizeof(enum power_supply_property), GFP_KERNEL);
    if (!info->batt_props) {
        dev_err(info->dev, "alloc memory for battery properties fail\n");
        return -ENOMEM;
    }

    memcpy(info->batt_props, info->bk_batt_psy->properties,
            sizeof(enum power_supply_property) * bk_batt_props);
    memcpy(info->batt_props + bk_batt_props, battery_ext_props,
            sizeof(enum power_supply_property) * batt_ext_props);

    return 0;
}

/***************************************************
 Function: huawei_battery_probe
 Description: battery driver probe
 Parameters:
    pdev            the device related with battery
 Return:
    0 mean drvier probe successful
    negative mean probe fail
***************************************************/
static int huawei_battery_probe(struct platform_device *pdev)
{
    int ret = 0;
    struct huawei_battery_info *info = NULL;
    struct device_node *node = NULL;
    const char *psy_name = NULL;

    if (!pdev) {
        pr_err("%s: Invalid param, fatal error\n", __func__);
        return -EINVAL;
    }

    node = pdev->dev.of_node;

    info = devm_kzalloc(&pdev->dev, sizeof(*info), GFP_KERNEL);
    if (!info) {
        dev_err(&pdev->dev, "Unable to alloc memory!\n");
        return -ENOMEM;
    }
    info->dev = &pdev->dev;
    platform_set_drvdata(pdev, info);

    ret = of_property_read_string(node, "huawei,bms-psy-name", &psy_name);
    if (ret)
        psy_name = "bms";
    info->bms_psy = power_supply_get_by_name(psy_name);
    if (!info->bms_psy) {
        dev_err(info->dev, "Cannot get bms psy %s\n", psy_name);
        ret = -EPROBE_DEFER;
        goto PSY_GET_FAIL;
    }

    ret = of_property_read_string(node, "huawei,bk_battery-psy-name", &psy_name);
    if (ret)
        psy_name = "bk_battery";
    info->bk_batt_psy = power_supply_get_by_name(psy_name);
    if (!info->bk_batt_psy) {
        dev_err(info->dev, "Cannot get bk_batery psy %s\n", psy_name);
        ret = -EPROBE_DEFER;
        goto PSY_GET_FAIL;
    }

    ret = of_property_read_string(node, "huawei,usb-psy-name", &psy_name);
    if (ret)
        psy_name = "usb";
    info->usb_psy = power_supply_get_by_name(psy_name);
    if (!info->usb_psy) {
        dev_err(info->dev, "Cannot get usb psy %s\n",psy_name);
        ret = -EPROBE_DEFER;
        goto PSY_GET_FAIL;
    }

    /* fast charge current votable */
    info->fcc_votable = create_votable("BATT: fcc",
                VOTE_MIN,
                set_fastchg_current_cb, info);
    if (IS_ERR(info->fcc_votable)) {
        ret = PTR_ERR(info->fcc_votable);
        goto VOTER_CREATE_FAIL;
    }

    /* usb input current limit votable */
    info->usb_icl_votable = create_votable("BATT: usb_icl",
                VOTE_MIN,
                set_usb_current_limit_cb, info);
    if (IS_ERR(info->usb_icl_votable)) {
        ret = PTR_ERR(info->usb_icl_votable);
        goto VOTER_CREATE_FAIL;
    }

    /* battery voltage votable */
    info->vbat_votable = create_votable("BATT: vbat",
                VOTE_MIN,
                set_vbat_cb, info);
    if (IS_ERR(info->vbat_votable)) {
        ret = PTR_ERR(info->vbat_votable);
        goto VOTER_CREATE_FAIL;
    }

    /* battery charging enable votable */
    info->bch_votable = create_votable("BATT: bch",
                VOTE_SET_ANY,
                charging_suspend_cb, info);
    if (IS_ERR(info->bch_votable)) {
        ret = PTR_ERR(info->bch_votable);
        goto VOTER_CREATE_FAIL;
    }

    ret = battery_parse_dt(info);
    if (ret) {
        dev_err(&pdev->dev, "Cannot parse the dts, pleae check\n");
        goto PARSE_DT_FAIL;
    }

    ret = battery_initial_status(info);
    if (ret) {
        dev_err(&pdev->dev, "Init battery status fail, please check\n");
        goto INIT_STATUS_FAIL;
    }

    INIT_DELAYED_WORK(&info->ibus_detect_work, battery_detect_ibus_work);
    INIT_DELAYED_WORK(&info->monitor_charging_work, battery_monitor_charging_work);
    INIT_DELAYED_WORK(&info->jeita_work, battery_jeita_work);

    /* try to register psy */
    ret = battery_prepare_properties(info);
    if (ret < 0) {
        dev_err(&pdev->dev, "battery prepare properties error ret = %d\n", ret);
        goto PREP_PROP_FAIL;
    }
    info->batt_psy.name = "battery";
    info->batt_psy.type = POWER_SUPPLY_TYPE_BATTERY;
    info->batt_psy.get_property = battery_get_property;
    info->batt_psy.set_property = battery_set_property;
    info->batt_psy.external_power_changed = battery_external_power_changed;
    info->batt_psy.supplied_to = battery_supplicants;
    info->batt_psy.num_supplicants = ARRAY_SIZE(battery_supplicants);
    info->batt_psy.property_is_writeable = battery_property_is_writeable;
    info->batt_psy.properties = info->batt_props;
    info->batt_psy.num_properties = info->num_props;
    ret = power_supply_register(info->dev, &info->batt_psy);
    if (ret < 0) {
        dev_err(&pdev->dev, "battery failed to register rc = %d\n", ret);
        goto PSY_REG_FAIL;
    }

    battery_health_handler();
    sync_max_voltage(info);

    schedule_delayed_work(&info->monitor_charging_work,
                msecs_to_jiffies(MONITOR_CHARGING_DELAY_TIME));
    return 0;

PSY_REG_FAIL:
    if (info->batt_props)
        devm_kfree(info->dev, info->batt_props);
    info->batt_props = NULL;
    info->num_props = 0;
PREP_PROP_FAIL:
INIT_STATUS_FAIL:
PARSE_DT_FAIL:
VOTER_CREATE_FAIL:
    if (info->bch_votable) {
        devm_kfree(&pdev->dev, info->bch_votable);
        info->bch_votable = NULL;
    }
    if (info->vbat_votable) {
        devm_kfree(&pdev->dev, info->vbat_votable);
        info->vbat_votable = NULL;
    }
    if (info->usb_icl_votable) {
        devm_kfree(&pdev->dev, info->usb_icl_votable);
        info->usb_icl_votable = NULL;
    }
    if (info->fcc_votable) {
        devm_kfree(&pdev->dev, info->fcc_votable);
        info->fcc_votable = NULL;
    }
PSY_GET_FAIL:
    devm_kfree(&pdev->dev, info);
    info = NULL;
    return ret;
}

/***************************************************
 Function: huawei_battery_remove
 Description: battery driver remove
 Parameters:
    pdev            the device related with battery
 Return:
    0 mean driver remove successful
    negative mean remove fail
***************************************************/
static int huawei_battery_remove(struct platform_device *pdev)
{
    struct huawei_battery_info *info = NULL;

    if (!pdev) {
        pr_err("%s: Invalid param, fatal error\n", __func__);
        return -EINVAL;
    }

    info = dev_get_drvdata(&pdev->dev);

    cancel_delayed_work_sync(&info->ibus_detect_work);
    cancel_delayed_work_sync(&info->monitor_charging_work);
    power_supply_unregister(&info->batt_psy);
    devm_kfree(&pdev->dev, info->bch_votable);
    devm_kfree(&pdev->dev, info->vbat_votable);
    devm_kfree(&pdev->dev, info->usb_icl_votable);
    devm_kfree(&pdev->dev, info->fcc_votable);
    devm_kfree(&pdev->dev, info->batt_props);
    devm_kfree(&pdev->dev, info);

    return 0;
}

/***************************************************
 Function: huawei_battery_suspend
 Description: battery driver suspend
 Parameters:
    pdev            the device related with battery
 Return:
    0 mean driver suspend successful
    negative mean suspend fail
***************************************************/
static int huawei_battery_suspend(struct device *dev)
{
    struct huawei_battery_info *info = NULL;

    if (!dev) {
        pr_err("%s: Invalid param, fatal error\n", __func__);
        return -EINVAL;
    }

    info = dev_get_drvdata(dev);

    cancel_delayed_work_sync(&info->ibus_detect_work);
    cancel_delayed_work_sync(&info->monitor_charging_work);
    if (info->jeita_use_interrupt) {
        flush_delayed_work(&info->jeita_work);
    }
    return 0;
}

/***************************************************
 Function: huawei_battery_resume
 Description: battery driver resume
 Parameters:
    pdev            the device related with battery
 Return:
    0 mean driver resume successful
***************************************************/
static int huawei_battery_resume(struct device *dev)
{
    struct huawei_battery_info *info = NULL;

    if (!dev) {
        pr_err("Invalid param for %s\n", __func__);
        return 0;
    }

    info = dev_get_drvdata(dev);

    schedule_delayed_work(&info->monitor_charging_work,
            msecs_to_jiffies(MONITOR_CHARGING_DELAY_TIME));
    return 0;
}

static const struct dev_pm_ops huawei_battery_pm_ops = {
    .suspend = huawei_battery_suspend,
    .resume = huawei_battery_resume,
};

static struct of_device_id battery_match_table[] =
{
    {
        .compatible = "huawei,battery",
        .data = NULL,
    },
    {
    },
};

static struct platform_driver huawei_battery_driver =
{
    .probe = huawei_battery_probe,
    .remove = huawei_battery_remove,
    .driver =
    {
        .name = "huawei,battery",
        .owner = THIS_MODULE,
        .of_match_table = of_match_ptr(battery_match_table),
        .pm = &huawei_battery_pm_ops,
    },
};

static int __init huawei_battery_init(void)
{
    return platform_driver_register(&huawei_battery_driver);
}
static void __exit huawei_battery_exit(void)
{
    platform_driver_unregister(&huawei_battery_driver);
}
module_init(huawei_battery_init);
module_exit(huawei_battery_exit);

MODULE_DESCRIPTION("huawei battery drivery");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:huawei-battery");
