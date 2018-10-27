/************************************************************
*
* Copyright (C), 1988-2015, Huawei Tech. Co., Ltd.
* FileName: huawei_dsm_charger.c
* Author: jiangfei(00270021)       Version : 0.1      Date:  2015-03-17
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
*  Description:    .c adatper file for charger radar
*  Version:
*  Function List:
*  History:
*  <author>  <time>   <version >   <desc>
***********************************************************/

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/workqueue.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/of_irq.h>
#include <asm/irq.h>
#include <linux/interrupt.h>
#include <linux/mutex.h>
#include <linux/wait.h>
#include <linux/time.h>
#include <linux/syscalls.h>
#include <linux/power_supply.h>
#include <linux/rtc.h>
#include <linux/power/huawei_dsm_charger.h>
#include <linux/power/huawei_charger.h>

#define USB_SUSPEND_CURR    2
#define OCUPPY_RETRY_TIME   3
#define OCUPPY_RETRY_DELAY  300 /* ms */

struct hw_dsm_charger_info
{
	struct device        *dev;
	struct delayed_work   check_charging_batt_status_work;
	struct power_supply *batt_psy;
	struct power_supply *usb_psy;
	struct mutex         dsm_dump_lock;
	struct hw_batt_temp_info *temp_ctrl;
	struct dsm_charger_ops *chg_ops;
	struct dsm_bms_ops *bms_ops;
	struct dsm_err_info *dsm_err;
	bool   charging_disabled;
	int    error_no;
	int dsm_cold_bat_degree;		/* cold bat degree parsed from dts */
	int dsm_customize_cool_bat_degree; 	/* customize cool bat degree (5 °C) parsed from dts */
	int dsm_imaxma_customize_cool_bat; 	/* customize cool bat charge current parsed from dts */
	int dsm_cool_bat_degree;		/* cool bat degree parsed from dts */
	int dsm_imaxma_cool_bat;		/* cool bat charge current parsed from dts */
	int dsm_vmaxmv_cool_bat;		/* cool bat max voltage parsed from dts */
	int dsm_warm_bat_degree;		/* warm bat degree parsed from dts */
	int dsm_imaxma_warm_bat;		/* warm bat charge current parsed from dts */
	int dsm_vmaxmv_warm_bat;		/* warm bat max voltage parsed from dts */
	int dsm_hot_bat_degree;			/* hot bat degree parsed from dts */
	int dsm_bat_ov_th;

	/* coul threshold */
	int dsm_volt_when_soc_0;
	int dsm_volt_when_soc_2;
	int dsm_volt_when_soc_90;
	int dsm_low_volt_when_soc_2;

	struct power_supply dsm_psy;

	int fg_use_coul;
};

struct hw_dsm_charger_info *dsm_info = NULL;

/* bms dsm client definition */
struct dsm_dev dsm_bms =
{
	.name = "dsm_bms",	/* dsm client name */
	.fops = NULL,
	.buff_size = 4096,	/* buffer size in bytes */
};
struct dsm_client *bms_dclient = NULL;
EXPORT_SYMBOL(bms_dclient);

/* charger dsm client definition */
struct dsm_dev dsm_charger =
{
	.name = "dsm_charger",
	.fops = NULL,
	.buff_size = 4096, /* buffer size in bytes */
};
struct dsm_client *charger_dclient = NULL;
EXPORT_SYMBOL(charger_dclient);

struct hw_batt_temp_info temp_info =
{
	.cold_bat_degree = 0, /* default cold bat degree: 0 degree */
	.cool_bat_degree = 100, /* default cool bat degree: 10 degree */
	.imaxma_cool_bat = 1200, /* default cool bat charge current: 1200mA */
	.vmaxmv_cool_bat = 4400, /* default cool bat max voltage: 4400mV */
	.warm_bat_degree = 450, /* default warm bat degree: 48 degree */
	.imaxma_warm_bat = 1300, /* default warm bat charge current: 1300mA */
	.vmaxmv_warm_bat = 4100, /* default warm bat max voltage: 4100mV */
	.hot_bat_degree = 500, /* default hot bat degree: 55 degree */
};

/* radar error count struct, same error report 3 time*/
struct dsm_err_info dsm_err;
static int temp_resume_flag = 0;
static int soc_jump_resume_flag = 0;

/* -1 as invalid */
SqQueue chg_bms_info = {.head = -1, .tail = -1};

/*
extern bool uvlo_event_trigger;
*/

static int dump_bms_chg_info(struct hw_dsm_charger_info *di,
			struct dsm_client *dclient,int type, char *info)
{
    int count = 0;

	if (!dclient || !di || !info) {
		pr_err("%s: invalid param, fatal error!\n", __func__);
		return -EINVAL;
	}
	di->dsm_err->err_no = type - PMU_ERR_NO_MIN;
	if (di->dsm_err->count[di->dsm_err->err_no]++ < REPORT_MAX) {
		mutex_lock(&di->dsm_dump_lock);

		/* try three times to get permission to use the buffer */
		while (dsm_client_ocuppy(dclient) && count++ < OCUPPY_RETRY_TIME) {
            msleep(OCUPPY_RETRY_DELAY);
        }

        if (count >= OCUPPY_RETRY_DELAY) {
			/* buffer is busy */
			pr_err("%s: buffer is busy!\n", __func__);
			mutex_unlock(&di->dsm_dump_lock);
			return -EBUSY;
		}
		dsm_client_record(dclient, "%s\n", info);
		dsm_client_notify(dclient, type);

		mutex_unlock(&di->dsm_dump_lock);
		return 0;
	} else {
		di->dsm_err->count[di->dsm_err->err_no] = REPORT_MAX;
	}
	return 0;
}

int dsm_post_chg_bms_info(int erro_no, char *format, ...)
{
	va_list args;
	int *qhead = NULL, *qtail = NULL;

	if ((erro_no < PMU_ERR_NO_MIN) || (erro_no > PMU_ERR_NO_MAX)) {
		pr_err("%s: erro_no:%d is out of range!\n", __func__, erro_no);
		return -EINVAL;
	}
	qhead = &chg_bms_info.head;
	qtail = &chg_bms_info.tail;
    /* *qhead and *qtail is the index */
	if (-1 == *qtail) {
		*qtail = 0;
	} else if ((*qtail+1)%QUEUE_INIT_SIZE == *qhead) {
		/*queue full*/
		pr_err("%s: queue is already full!\n", __func__);
		return -EBUSY;
	}

	memset(chg_bms_info.base[*qtail].content_info, 0, sizeof(chg_bms_info.base[*qtail].content_info));
	if (format) {
		va_start(args, format);
		vsnprintf(chg_bms_info.base[*qtail].content_info,
				sizeof(chg_bms_info.base[*qtail].content_info),
				format, args);
		va_end(args);
	}
	chg_bms_info.base[*qtail].erro_number = erro_no;
	*qtail = (*qtail + 1)%QUEUE_INIT_SIZE;
	if (*qhead == -1) {
		*qhead = 0;
	}
	pr_debug("%s: queue add success!\n", __func__);
	return 0;
}

static int dsm_get_chg_bms_info(void)
{
	int qhead = 0, qtail = 0;
	struct hw_dsm_charger_info *di = NULL;

	di = dsm_info;
	if (NULL == di) {
		/*dclient is not ready*/
		pr_err("%s: dclient is not ready!\n", __func__);
		return -EINVAL;
	}
	pr_debug("%s: dclient is ready!\n", __func__);
	qhead = chg_bms_info.head;
	qtail = chg_bms_info.tail;
	if (qhead == qtail) {
		/*queue empty*/
		pr_debug( "%s: queue is empty!\n",__func__);
		return 0;
	} else {
		chg_bms_info.head = (chg_bms_info.head + 1) % QUEUE_INIT_SIZE;
		if ((chg_bms_info.base[qhead].erro_number >= DSM_BMS_NORMAL_SOC_CHANGE_MUCH)
			&& (chg_bms_info.base[qhead].erro_number < DSM_NOT_CHARGE_WHEN_ALLOWED)) {
			dump_bms_chg_info(di,bms_dclient,chg_bms_info.base[qhead].erro_number,
					chg_bms_info.base[qhead].content_info);
		} else if ((chg_bms_info.base[qhead].erro_number >= DSM_NOT_CHARGE_WHEN_ALLOWED)
			&& (chg_bms_info.base[qhead].erro_number < PMU_ERR_NO_MAX)) {
			dump_bms_chg_info(di,charger_dclient,chg_bms_info.base[qhead].erro_number,
					chg_bms_info.base[qhead].content_info);
		} else {
			pr_err("%s: err_no is exceed available number, do nothing!\n", __func__);
			return -EINVAL;
		}

		return 0;
	}
	return 0;
}

static int dsm_get_property_from_psy(struct power_supply *psy,
		enum power_supply_property prop)
{
	int rc = 0;
	int val = 0;
	union power_supply_propval ret = {0, };

    if (!psy) {
        pr_err("%s: invalid param, fatal error\n", __func__);
        return -EINVAL;
    }

	rc = psy->get_property(psy, prop, &ret);
	if (rc) {
		pr_err("psy doesn't support reading prop %d rc = %d\n",
				prop, rc);
		return rc;
	}
	val = ret.intval;
	return val;
}

/* return 0 means battery is abnormal, 1 means battery is ok */
static int is_battery_in_normal_condition(int voltage, int temp,
				struct hw_dsm_charger_info *chip)
{
    if (!chip) {
        pr_err("%s: invalid param, fatal error\n", __func__);
        return -EINVAL;
    }
    /*make sure voltage uv to mv*/
    voltage = voltage / VOLT_UV_TO_MV;
	if ((temp >= (chip->dsm_warm_bat_degree - TEMP_BUFFER))
		&& (temp < (chip->dsm_hot_bat_degree - TEMP_BUFFER))
		&& (voltage <= (chip->dsm_vmaxmv_warm_bat - WARM_VOL_BUFFER))) {
		return 1;
	} else if (((chip->dsm_cold_bat_degree + TEMP_BUFFER) < temp)
		&& (temp < (chip->dsm_warm_bat_degree - TEMP_BUFFER))) {
		return 1;
	} else {
		return 0;
	}
}

/* return 0 means battery temp is normal, otherwise means battery abnormal */
static int is_batt_temp_in_normal_range(int pre_temp,int curr_temp)
{
	if ((TEMP_LOWER_THR <=curr_temp) && (TEMP_UPPER_THR >= curr_temp)
		&& (TEMP_LOWER_THR <= pre_temp) && (TEMP_UPPER_THR >= pre_temp)
		&& (INIT_TEMP != pre_temp)) {
		return 1;
	} else {
		return 0;
	}
}

static void print_basic_info_before_dump(struct hw_dsm_charger_info *chip,
						struct dsm_client *dclient, const int type)
{
	int error_type = 0;

    if (!chip || !dclient) {
        pr_err("%s: invalid param, fatal error\n", __func__);
        return;
    }

	error_type = type;
	switch(error_type)
	{
	case DSM_BMS_VOL_SOC_DISMATCH_1:
		dsm_client_record(dclient,
			"battery voltage is over %dmV, but soc is "
			"0 percent, not match\n", chip->dsm_volt_when_soc_0 / VOLT_UV_TO_MV);
		pr_info("battery voltage is over %dmV, but soc is "
			"0 percent, not match\n", chip->dsm_volt_when_soc_0 / VOLT_UV_TO_MV);
	break;

	case DSM_BMS_VOL_SOC_DISMATCH_2:
		dsm_client_record(dclient,
			"battery voltage is over %dmV, but soc is "
			"no more than 2 percent, not match\n", chip->dsm_volt_when_soc_2 / VOLT_UV_TO_MV);
		pr_info("battery voltage is over %dmV, but soc is "
			"no more than 2 persent, not match\n", chip->dsm_volt_when_soc_2 / VOLT_UV_TO_MV);
		break;

	case DSM_BMS_VOL_SOC_DISMATCH_3:
		dsm_client_record(dclient,
			"battery voltage is over %dmV, but  "
			"soc is below 90 percent\n", chip->dsm_volt_when_soc_90 / VOLT_UV_TO_MV);
		pr_info("battery voltage is over %dmV, but  "
			"soc is below 90 percent\n", chip->dsm_volt_when_soc_90 / VOLT_UV_TO_MV);
		break;

	case DSM_VM_BMS_VOL_SOC_DISMATCH_4:
		dsm_client_record(dclient,
			"battery voltage is too low(%dmV) when "
			"soc is 2 percent\n", chip->dsm_low_volt_when_soc_2 / VOLT_UV_TO_MV);
		pr_info("battery voltage is too low(%dmV) when "
			"soc is 2 percent\n", chip->dsm_low_volt_when_soc_2 / VOLT_UV_TO_MV);
		break;

	case DSM_NOT_CHARGE_WHEN_ALLOWED:
		dsm_client_record(dclient,
			"cannot charging when allowed charging\n");
		pr_info("cannot charging when allowed charging\n");
		break;

	case DSM_BATT_PRES_ERROR_NO:
		dsm_client_record(dclient,
			"battery is absent!\n");
		pr_info("battery is absent!\n");
		break;

	case DSM_WARM_CURRENT_LIMIT_FAIL:
		dsm_client_record(dclient,
			"set battery warm charge current failed\n");
		pr_info("set battery warm charge current failed\n");
		break;

	case DSM_COOL_CURRENT_LIMIT_FAIL:
		dsm_client_record(dclient,
			"set battery cool charge current failed\n");
		pr_info("set battery cool charge current failed\n");
		break;

	case DSM_FULL_WHEN_CHARGER_ABSENT:
		dsm_client_record(dclient,
			"battery status is full when charger is absent\n");
		pr_info("battery status is full when charger is absent\n");
		break;

	case DSM_BATT_OVER_VOLT:
		dsm_client_record(dclient,
			"battery voltage is over %dmV\n", chip->dsm_bat_ov_th / VOLT_UV_TO_MV);
		pr_info("battery voltage is over %dmV\n", chip->dsm_bat_ov_th / VOLT_UV_TO_MV);
		break;

	case DSM_FAKE_FULL:
		dsm_client_record(dclient,
			"report charging full when actual soc is below 95 percent\n");
		pr_info("report charging full when actual soc is below 95 percent\n");
		break;

	case DSM_ABNORMAL_CHARGE_STATUS:
		dsm_client_record(dclient,
			"charging status is charging while charger is not online\n");
		pr_info("charging status is charging while charger is not online\n");
		break;

	case DSM_BATT_VOL_TOO_LOW:
		dsm_client_record(dclient,
			"battery voltage is too low(below 2.5V)\n");
		pr_info("battery voltage is too low(below 2.5V)\n");
		break;

	case DSM_STILL_CHARGE_WHEN_HOT:
		dsm_client_record(dclient,
			"still charge when battery is hot\n");
		pr_info("still charge when battery is hot\n");
		break;

	case DSM_STILL_CHARGE_WHEN_COLD:
		dsm_client_record(dclient,
			"still charge when battery is cold\n");
		pr_info("still charge when battery is cold\n");
		break;

	case DSM_STILL_CHARGE_WHEN_SET_DISCHARGE:
		dsm_client_record(dclient,
			"still charge when we set discharge\n");
		pr_info("still charge when we set discharge\n");
		break;

	case DSM_STILL_CHARGE_WHEN_OVER_VOLT:
		dsm_client_record(dclient,
			"still charge when battery voltage reach or over %dmV\n", chip->dsm_bat_ov_th / VOLT_UV_TO_MV);
		pr_info("still charge when battery voltage reach or over %dmV\n", chip->dsm_bat_ov_th / VOLT_UV_TO_MV);
		break;

	case DSM_HEATH_OVERHEAT:
		dsm_client_record(dclient,
			"battery health is overheat\n");
		pr_info("battery health is overheat\n");
		break;

	case DSM_BATT_TEMP_JUMP:
		dsm_client_record(dclient,
			"battery temperature change more than 5 degree in short time\n");
		pr_info("battery temperature change more than 5 degree in short time\n");
		break;

	case DSM_BATT_TEMP_BELOW_0:
		dsm_client_record(dclient,
			"battery temperature is below 0 degree\n");
		pr_info("battery temperature is below 0 degree\n");
		break;

	case DSM_BATT_TEMP_OVER_60:
		dsm_client_record(dclient,
			"battery temperature is over 60 degree\n");
		pr_info("battery temperature is over 60 degree\n");
		break;

	case DSM_NOT_CHARGING_WHEN_HOT:
		dsm_client_record(dclient,
			"battery is hot, not charging\n");
		pr_info("battery is hot, not charging\n");
		break;

	case DSM_NOT_CHARGING_WHEN_COLD:
		dsm_client_record(dclient,
			"battery is cold, not charging\n");
		pr_info("battery is cold, not charging\n");
		break;

	case DSM_ABNORMAL_CHARGE_FULL_STATUS:
		dsm_client_record(dclient,
			"report discharging but actual soc is 100 percent\n");
		pr_info("report discharging but actual soc is 100 percent\n");
		break;

	default:
		break;
	}
}

/* dump charger ic, bms registers and some adc values, and notify to dsm */
#define DUMP_BATTERY_TEMP 250
static int dump_info_and_adc(struct hw_dsm_charger_info *di,
					struct dsm_client *dclient, int type)
{
    int count = 0;
	int vbat_uv = 0;
	int batt_temp = DUMP_BATTERY_TEMP;
	int current_ma = 0;

	if (NULL == di || NULL == dclient) {
		pr_err("%s: there is no dclient!\n", __func__);
		return -EINVAL;
	}

	mutex_lock(&di->dsm_dump_lock);

	/* try three times to get permission to use the buffer */
	while (dsm_client_ocuppy(dclient) && count++ < OCUPPY_RETRY_TIME) {
        msleep(OCUPPY_RETRY_DELAY);
    }

    if (count >= OCUPPY_RETRY_TIME) {
		/* buffer is busy */
		pr_err("%s: buffer is busy!\n", __func__);
		mutex_unlock(&di->dsm_dump_lock);
		return -EBUSY;
	}

	print_basic_info_before_dump(di, dclient, type);

	/*save battery vbat, current, temp values and so on*/
	batt_temp = dsm_get_property_from_psy(di->batt_psy,
						POWER_SUPPLY_PROP_TEMP);
	vbat_uv = dsm_get_property_from_psy(di->batt_psy,
						POWER_SUPPLY_PROP_VOLTAGE_NOW);
	current_ma = dsm_get_property_from_psy(di->batt_psy,
						POWER_SUPPLY_PROP_CURRENT_NOW);
	current_ma = current_ma / CURR_UA_TO_MA;

	dsm_client_record(dclient,
			"ADC values: vbat=%d current=%d batt_temp=%d\n",
			vbat_uv, current_ma, batt_temp);

	pr_info ("ADC values: vbat=%d current=%d batt_temp=%d\n",
			vbat_uv, current_ma, batt_temp);

	dsm_client_notify(dclient, type);
	mutex_unlock(&di->dsm_dump_lock);
	return 0;
}

/* interface for be called to dump and notify*/
int dsm_dump_log(struct dsm_client *dclient, int err_no)
{
	struct hw_dsm_charger_info *di = NULL;

    if (NULL == dclient) {
        pr_err("%s: invalid param, fatal error\n", __func__);
        return -EINVAL;
    }

	di = dsm_info;
	if ( NULL == di) {
		pr_err("%s: dsm_charger is not ready!\n", __func__);
		return -EINVAL;
	}
	if ((PMU_ERR_NO_MIN > err_no) || (PMU_ERR_NO_MAX < err_no)) {
		pr_err("%s: err_no is exceed available number, do nothing!\n", __func__);
		return -EINVAL;
	}
	di->dsm_err->err_no = err_no - DSM_BMS_NORMAL_SOC_CHANGE_MUCH;
	if (di->dsm_err->count[di->dsm_err->err_no]++ < REPORT_MAX) {
		dump_info_and_adc(di, dclient, err_no);
	} else {
		di->dsm_err->count[di->dsm_err->err_no] = REPORT_MAX;
	}
	return 0;
}
EXPORT_SYMBOL(dsm_dump_log);

static int get_current_time(unsigned long *now_tm_sec)
{
	int rc = 0;
	struct rtc_time tm;
	struct rtc_device *rtc = 0;

    if (!now_tm_sec) {
        pr_err("%s: invalid param, fatal error\n", __func__);
        return -EINVAL;
    }

	rtc = rtc_class_open(CONFIG_RTC_HCTOSYS_DEVICE);
	if (rtc == NULL) {
		pr_err("%s: unable to open rtc device (%s)\n",
			__FILE__, CONFIG_RTC_HCTOSYS_DEVICE);
		return -EINVAL;
	}

	rc = rtc_read_time(rtc, &tm);
	if (rc) {
		pr_err("Error reading rtc device (%s) : %d\n",
			CONFIG_RTC_HCTOSYS_DEVICE, rc);
		goto close_time;
	}

	rc = rtc_valid_tm(&tm);
	if (rc) {
		pr_err("Invalid RTC time (%s): %d\n",
			CONFIG_RTC_HCTOSYS_DEVICE, rc);
		goto close_time;
	}
	rtc_tm_to_time(&tm, now_tm_sec);

close_time:
	rtc_class_close(rtc);
	return rc;
}


static void check_charging_batt_status_work(struct work_struct *work)
{
	int vbat_uv = 0, batt_temp = 0, bat_present = 0, cur_status = 0;
	int health = 0, batt_level = 0, usb_present = 0, chg_present = 0;
	static int cannot_charge_count = 0;
	static int start_dismatch_detect = 0;
	static int hot_charging_count = 0, cold_charging_count = 0;
	static int warm_exceed_limit_count = 0;
    static int cool_exceed_limit_count = 0;
    static int customize_cool_exceed_limit_count = 0;
	static int previous_temp = INIT_TEMP;
	int current_max = 0, current_ma = 0;
	int voltage_regulation = 0;
	static unsigned long previous_tm_sec = 0;
	unsigned long now_tm_sec = 0;
	struct hw_dsm_charger_info *chip = NULL;

    if (!work) {
        pr_err("%s: invalid param, fatal error\n", __func__);
        return;
    }

    chip = container_of(work, struct hw_dsm_charger_info,
                        check_charging_batt_status_work.work);
    if (!chip) {
        pr_err("%s: Cannot get dsm charger info, fatal error\n", __func__);
        return;
    }

	chg_present = dsm_get_property_from_psy(chip->batt_psy, POWER_SUPPLY_PROP_CHARGER_PRESENT);
	usb_present = dsm_get_property_from_psy(chip->usb_psy, POWER_SUPPLY_PROP_PRESENT);
	/*
	 * if we can read charger present from battery psy, and it's not
	 * same with the one from usb psy, reschedule until it's consistent
	 * PS: chg_present >= 0 means we get a valid charger ppresent status
	 * from battery psy
	 */
	if (chg_present >= 0 && chg_present != usb_present) {
		pr_debug("USB & battery status is inconsistent\n");
		goto RESCHEDULE;
	}

	if (chip->batt_psy && chip->batt_psy->get_property) {
		batt_level = dsm_get_property_from_psy(chip->batt_psy, POWER_SUPPLY_PROP_CAPACITY);
		bat_present = dsm_get_property_from_psy(chip->batt_psy, POWER_SUPPLY_PROP_PRESENT);
		batt_temp = dsm_get_property_from_psy(chip->batt_psy, POWER_SUPPLY_PROP_TEMP);
		vbat_uv = dsm_get_property_from_psy(chip->batt_psy, POWER_SUPPLY_PROP_VOLTAGE_NOW);
		current_ma = dsm_get_property_from_psy(chip->batt_psy,
							POWER_SUPPLY_PROP_CURRENT_NOW);
		current_ma = current_ma / CURR_UA_TO_MA;
		health = dsm_get_property_from_psy(chip->batt_psy, POWER_SUPPLY_PROP_HEALTH);
		chip->charging_disabled = !(dsm_get_property_from_psy(chip->batt_psy,
							POWER_SUPPLY_PROP_CHARGING_ENABLED));
		cur_status = dsm_get_property_from_psy(chip->batt_psy, POWER_SUPPLY_PROP_STATUS);
	}

	current_max = dsm_get_property_from_psy(chip->usb_psy,
							POWER_SUPPLY_PROP_CURRENT_MAX);
	current_max = current_max / CURR_UA_TO_MA;

	/*
	 * if all the charging conditions are avaliable, but it still cannot charge,
	 * we report the error and dump the basic info and ADC values
	 */
	if(is_battery_in_normal_condition(vbat_uv, batt_temp, chip)) {
		if ((POWER_SUPPLY_STATUS_DISCHARGING == cur_status)
			&& (BATT_FULL_LEVEL != batt_level) && usb_present
			&& (current_max > USB_SUSPEND_CURR)
			&& (!chip->charging_disabled)
			&& bat_present
			&& (POWER_SUPPLY_STATUS_FULL != cur_status)) {
			if(cannot_charge_count++ < NOT_CHARGE_COUNT) {
				pr_info("cannot charge when allowed, count is %d\n", cannot_charge_count);
			} else {
				cannot_charge_count = 0;
				pr_info("cannot charge when allowed!\n");
				dsm_dump_log(charger_dclient, DSM_NOT_CHARGE_WHEN_ALLOWED);
			}
		} else {
			cannot_charge_count = 0;
		}
	}

	if (!bat_present) {
		pr_info("battery is absent!\n");
		dsm_dump_log(charger_dclient, DSM_BATT_PRES_ERROR_NO);
	} else {
		if (!usb_present) {
			if (!chip->fg_use_coul && START_DISMATCH_COUNT <= start_dismatch_detect++) {
				start_dismatch_detect = START_DISMATCH_COUNT;
				if ((chip->dsm_volt_when_soc_0 <= vbat_uv) && (SOC_ZERO == batt_level)) {
					dsm_dump_log(bms_dclient, DSM_BMS_VOL_SOC_DISMATCH_1);
				}

				if ((chip->dsm_volt_when_soc_2 <= vbat_uv) && (SOC_ZERO != batt_level)
						&& (SOC_THR1 >= batt_level)) {
					dsm_dump_log(bms_dclient, DSM_BMS_VOL_SOC_DISMATCH_2);
				}

				if ((chip->dsm_volt_when_soc_90 <= vbat_uv) && (SOC_ZERO != batt_level)
						&& (SOC_HIGH >= batt_level)) {
					dsm_dump_log(bms_dclient, DSM_BMS_VOL_SOC_DISMATCH_3);
				}

				if ((chip->dsm_low_volt_when_soc_2 >= vbat_uv) && (SOC_THR1 == batt_level)) {
					dsm_dump_log(bms_dclient, DSM_VM_BMS_VOL_SOC_DISMATCH_4);
				}
			}

			if (chip->dsm_bat_ov_th <= vbat_uv) {
				dsm_dump_log(charger_dclient, DSM_BATT_OVER_VOLT);
			}
		}

		/* usb present */
		if (usb_present) {
			if ((POWER_SUPPLY_STATUS_FULL == cur_status)
				&& (SOC_HIGH_THR >= batt_level)) {
				dsm_dump_log(charger_dclient, DSM_FAKE_FULL);
			}
			if ((POWER_SUPPLY_STATUS_DISCHARGING == cur_status)
				&& (BATT_FULL_LEVEL == batt_level)) {
				dsm_dump_log(charger_dclient, DSM_ABNORMAL_CHARGE_FULL_STATUS);
			}
			/* chip->charging_disabled is true*/
			if (chip->charging_disabled) {
				if (POWER_SUPPLY_STATUS_CHARGING == cur_status) {
					dsm_dump_log(charger_dclient, DSM_STILL_CHARGE_WHEN_SET_DISCHARGE);
				}
			} else {
				if ((chip->dsm_bat_ov_th <= vbat_uv)
					&& (POWER_SUPPLY_STATUS_CHARGING == cur_status)) {
					dsm_dump_log(charger_dclient, DSM_STILL_CHARGE_WHEN_OVER_VOLT);
				}

				if (((chip->dsm_hot_bat_degree + TEMP_BUFFER) < batt_temp)
					&& ((POWER_SUPPLY_STATUS_DISCHARGING == cur_status)
					|| (POWER_SUPPLY_STATUS_NOT_CHARGING == cur_status))) {
					dsm_dump_log(charger_dclient, DSM_NOT_CHARGING_WHEN_HOT);
				}

				if (((chip->dsm_cold_bat_degree - TEMP_BUFFER) > batt_temp)
					&& ((POWER_SUPPLY_STATUS_DISCHARGING == cur_status)
					|| (POWER_SUPPLY_STATUS_NOT_CHARGING == cur_status))) {
					dsm_dump_log(charger_dclient, DSM_NOT_CHARGING_WHEN_COLD);
				}

				if (((chip->dsm_hot_bat_degree + TEMP_BUFFER) < batt_temp)
					&& (POWER_SUPPLY_STATUS_CHARGING == cur_status)) {
					if (hot_charging_count++ < DSM_COUNT) {
						pr_info("still charge when battery is hot, count is %d\n",
								hot_charging_count);
					} else {
						hot_charging_count = 0;
						dsm_dump_log(charger_dclient, DSM_STILL_CHARGE_WHEN_HOT);
					}
				} else {
					hot_charging_count = 0;
				}

				if (((chip->dsm_cold_bat_degree - TEMP_BUFFER) > batt_temp)
					&&(POWER_SUPPLY_STATUS_CHARGING == cur_status)) {
					if (cold_charging_count++ < DSM_COUNT) {
						pr_info("still charge when battery is cold, count is %d\n",
								cold_charging_count);
					} else {
						cold_charging_count = 0;
						dsm_dump_log(charger_dclient, DSM_STILL_CHARGE_WHEN_COLD);
					}
				} else {
					cold_charging_count = 0;
				}

				if (((chip->dsm_warm_bat_degree + TEMP_BUFFER) < batt_temp)
					&& (chip->dsm_imaxma_warm_bat < abs(current_ma)) && (current_ma < 0)) {
					if (warm_exceed_limit_count++ < DSM_COUNT) {
						pr_info("current is over warm current limit when warm, count is %d\n",
								warm_exceed_limit_count);
					} else {
						warm_exceed_limit_count = 0;
						dsm_dump_log(charger_dclient, DSM_WARM_CURRENT_LIMIT_FAIL);
					}
				} else {
					warm_exceed_limit_count = 0;
				}

				if (((chip->dsm_cool_bat_degree - TEMP_BUFFER) > batt_temp)
					&& (chip->dsm_imaxma_cool_bat < abs(current_ma)) && (current_ma < 0)) {
					if (cool_exceed_limit_count++ < DSM_COUNT) {
						pr_info("current is over cool current limit when cool, count is %d\n",
								cool_exceed_limit_count);
					} else {
						cool_exceed_limit_count = 0;
						dsm_dump_log(charger_dclient, DSM_COOL_CURRENT_LIMIT_FAIL);
					}
				} else {
					cool_exceed_limit_count = 0;
				}

				/*do the compare when customize_cool_bat_degree differs from cool_bat_degree only*/
				if (((chip->dsm_customize_cool_bat_degree - TEMP_BUFFER) > batt_temp)
					&& (chip->dsm_customize_cool_bat_degree != chip->dsm_cool_bat_degree)
					&& (chip->dsm_imaxma_customize_cool_bat < abs(current_ma)) && (current_ma < 0)) {
					if (customize_cool_exceed_limit_count++ < DSM_COUNT) {
						pr_info("current is over cool current limit when cool, count is %d\n",
								customize_cool_exceed_limit_count);
					} else {
						customize_cool_exceed_limit_count = 0;
						dsm_dump_log(charger_dclient, DSM_COOL_CURRENT_LIMIT_FAIL);
					}
				} else {
					customize_cool_exceed_limit_count = 0;
				}
			}
		}

		/*
		 * only care 20 to 40 temperature zone in centigrade,
		 * if temp jumps in this zone in 30 seconds, notify to dsm server
		 */
		get_current_time(&now_tm_sec);
		if ((abs(previous_temp - batt_temp) >= TEMP_DELTA)
			&& is_batt_temp_in_normal_range(previous_temp,batt_temp)
			&& (HALF_MINUTE >=(now_tm_sec -previous_tm_sec))) {
			if (temp_resume_flag) {
				pr_debug("temp_resume_flag is 1, ignore battery temp jump.\n");
				temp_resume_flag = 0;
			} else {
				dsm_dump_log(charger_dclient, DSM_BATT_TEMP_JUMP);
			}
		}
		previous_temp = batt_temp;
		previous_tm_sec = now_tm_sec;

		if (POWER_SUPPLY_HEALTH_OVERHEAT == health) {
			dsm_dump_log(charger_dclient, DSM_HEATH_OVERHEAT);
		}

		if (HOT_TEMP_60 < batt_temp) {
			dsm_dump_log(charger_dclient, DSM_BATT_TEMP_OVER_60);
		}

		if (LOW_TEMP > batt_temp) {
			dsm_dump_log(charger_dclient, DSM_BATT_TEMP_BELOW_0);
		}

/*
		if (uvlo_event_trigger) {
			if (ABNORMAL_UVLO_VOL_THR <= vbat_uv) {
				dsm_dump_log(bms_dclient,
						DSM_BMS_HIGH_VOLTAGE_UVLO);
			}
			uvlo_event_trigger = false;
		}
*/

	}

	dsm_get_chg_bms_info();

RESCHEDULE:

	schedule_delayed_work(&chip->check_charging_batt_status_work,
			msecs_to_jiffies(CHECKING_TIME));
}

static char *chg_dsm_supplied_from[] = {
	"battery",
	"usb"
};

static int chg_dsm_get_property(struct power_supply *psy,
						enum power_supply_property psp,
						union power_supply_propval *val)
{
	if (!psy || !val) {
		return -EINVAL;
	}
	return 0;
}

static int chg_dsm_set_property(struct power_supply *psy,
						enum power_supply_property psp,
						const union power_supply_propval *val)
{
	if (!psy || !val) {
		return -EINVAL;
	}
	return 0;
}

static void chg_dsm_external_power_changed(struct power_supply *psy)
{
	struct hw_dsm_charger_info *chip = NULL;
	static int soc = INVALID_VALUE;
	static int usb_pres = INVALID_VALUE;
	static int batt_volt = INVALID_VALUE;
	static int batt_curr = INVALID_VALUE;
	static int batt_temp = INVALID_VALUE;
	int soc_now = INVALID_VALUE;
	int usb_pres_now = INVALID_VALUE;
	int batt_volt_now = INVALID_VALUE;
	int batt_curr_now = INVALID_VALUE;
	int batt_temp_now = INVALID_VALUE;
	int profile_status = 0;

	if (!psy) {
		pr_err("invalid param, fatal error\n");
		return;
	}

	chip = container_of(psy, struct hw_dsm_charger_info, dsm_psy);

	profile_status = dsm_get_property_from_psy(chip->batt_psy, POWER_SUPPLY_PROP_PROFILE_STATUS);
	if (profile_status <= 0) {
		pr_debug("battery profile is not loaded yet, the soc is inaccurate\n");
		return;
	}

	soc_now = dsm_get_property_from_psy(chip->batt_psy, POWER_SUPPLY_PROP_CAPACITY);
	usb_pres_now = dsm_get_property_from_psy(chip->usb_psy, POWER_SUPPLY_PROP_PRESENT);
	batt_volt_now = dsm_get_property_from_psy(chip->batt_psy, POWER_SUPPLY_PROP_VOLTAGE_NOW);
	batt_curr_now = dsm_get_property_from_psy(chip->batt_psy, POWER_SUPPLY_PROP_CURRENT_NOW);
	batt_temp_now = dsm_get_property_from_psy(chip->batt_psy, POWER_SUPPLY_PROP_TEMP);

	pr_debug("dsm external power changed: soc now %d, soc %d, usb_pres_now %d, usb_pres %d,"
				"batt_volt_now %d, batt_volt %d, batt_curr_now %d, batt_curr %d"
				"batt_temp_now %d, batt_temp %d\n",
				soc_now, soc, usb_pres_now, usb_pres, batt_volt_now, batt_volt,
				batt_curr_now, batt_curr, batt_temp_now, batt_temp);

	if (batt_temp_now < SOC_JUMP_LOW_TEMP) {
		soc = INVALID_VALUE;
		usb_pres = INVALID_VALUE;
		goto OUT;
	}

	if (soc_jump_resume_flag) {
		soc = INVALID_VALUE;
		usb_pres = INVALID_VALUE;
		soc_jump_resume_flag = 0;
	}

	if (INVALID_VALUE == soc) {
		goto OUT;
	}

	/* skip lower battery voltage interrupt case */
	if (0 != soc_now && abs(soc - soc_now) >= SOC_JUMP_STEP) {
		pr_info("dsm soc jumped with step %d\n", SOC_JUMP_STEP);
		/* soc jumped */
		if (INVALID_VALUE == usb_pres || usb_pres == usb_pres_now) {
			dsm_post_chg_bms_info(DSM_BMS_NORMAL_SOC_CHANGE_MUCH,
					"last soc %d, now soc %d, last batt_volt %dmV, batt_volt %dmV,"
					"last batt_curr %dmA, batt_curr %dmA, last batt_temp %d, batt_temp %d\n",
					soc, soc_now, batt_volt / VOLT_UV_TO_MV, batt_volt_now / VOLT_UV_TO_MV,
					batt_curr / CURR_UA_TO_MA, batt_curr_now / CURR_UA_TO_MA,
					batt_temp, batt_temp_now);
		} else {
			dsm_post_chg_bms_info(DSM_BMS_SOC_CHANGE_PLUG_INOUT,
					"last soc %d, now soc %d, last usb pres %d, now usb pres %d, "
					"last batt_volt %dmV, batt_volt %dmV, last batt_curr %dmA, batt_curr %dmA,"
					"last batt_temp %d, batt_temp %d\n",
					soc, soc_now, usb_pres, usb_pres_now,
					batt_volt / VOLT_UV_TO_MV, batt_volt_now / VOLT_UV_TO_MV,
					batt_curr / CURR_UA_TO_MA, batt_curr_now / CURR_UA_TO_MA,
					batt_temp, batt_temp_now);
		}
	}

OUT:
	soc = soc_now;
	usb_pres = usb_pres_now;
	batt_volt = batt_volt_now;
	batt_curr = batt_curr_now;
	batt_temp = batt_temp_now;
}

/* update dsm iv_val if need to raise dsm iv range */
static int dsm_update_iv_val(int ori_val, int raise_pct)
{
    int temp_val = 0;
    temp_val = (ori_val * raise_pct) / 100;
    temp_val = ori_val + temp_val;
    return temp_val;
}

/*parse dt info*/
static void dsm_get_dt(struct hw_dsm_charger_info *chip,struct device_node* np)
{
	int ret = 0;
    int raise_pct = 0;

    if (!chip || !np) {
        pr_err("%s: invalid param, fatal error\n", __func__);
        return;
    }

		ret = of_property_read_u32(np, "qcom,fg-use-coul", &chip->fg_use_coul);
		if (ret) {
			chip->fg_use_coul = 1;
			pr_err("get fg_use_coul info fail, use default = 1\n");
		}

		ret = of_property_read_u32(np,"qcom,dsm_cold_bat_degree",&chip->dsm_cold_bat_degree);
			if (ret) {
				chip->dsm_cold_bat_degree = chip->temp_ctrl->cold_bat_degree;
				pr_err("get dsm_cold_bat_degree info fail!use default = %d\n",chip->dsm_cold_bat_degree);
			}

		ret = of_property_read_u32(np,"qcom,dsm_cool_bat_degree",&chip->dsm_cool_bat_degree);
			if (ret) {
				chip->dsm_cool_bat_degree = chip->temp_ctrl->cool_bat_degree;
				pr_err("get dsm_cool_bat_degree info fail!use default = %d\n",chip->dsm_cool_bat_degree);
			}

		ret = of_property_read_u32(np,"qcom,dsm_imaxma_cool_bat",&chip->dsm_imaxma_cool_bat);
			if (ret) {
				chip->dsm_imaxma_cool_bat = chip->temp_ctrl->imaxma_cool_bat;
				pr_err("get dsm_imaxma_cool_bat info fail!use default = %d\n",chip->dsm_imaxma_cool_bat);
			}

		ret = of_property_read_u32(np,"qcom,dsm_vmaxmv_cool_bat",&chip->dsm_vmaxmv_cool_bat);
			if (ret) {
				chip->dsm_vmaxmv_cool_bat = chip->temp_ctrl->vmaxmv_cool_bat;
				pr_err("get dsm_vmaxmv_cool_bat info fail!use default = %d\n",chip->dsm_vmaxmv_cool_bat);
			}

		ret = of_property_read_u32(np,"qcom,dsm_customize_cool_bat_degree",&chip->dsm_customize_cool_bat_degree);
                        if (ret) {
				/*if doesn't exist customize_cool_bat_degree then use cool_bat_degree instead*/
				chip->dsm_customize_cool_bat_degree = chip->dsm_cool_bat_degree;
				pr_err("get dsm_customize_cool_bat_degree info fail!use dsm_cool's para instead = %d\n",chip->dsm_customize_cool_bat_degree);
				}

		ret = of_property_read_u32(np,"qcom,dsm_imaxma_customize_cool_bat",&chip->dsm_imaxma_customize_cool_bat);
			if (ret) {
				/*if doesn't exist imaxma_customize_cool_bat then use imaxma_cool_bat instead*/
				chip->dsm_imaxma_customize_cool_bat = chip->dsm_imaxma_cool_bat;
				pr_err("get dsm_imaxma_customize_cool_bat info fail!use dsm_cool's para instead = %d\n",chip->dsm_imaxma_customize_cool_bat);
				}

		ret = of_property_read_u32(np,"qcom,dsm_warm_bat_degree",&chip->dsm_warm_bat_degree);
			if (ret) {
				chip->dsm_warm_bat_degree = chip->temp_ctrl->warm_bat_degree;
				pr_err("get dsm_warm_bat_degree info fail!use default = %d\n",chip->dsm_warm_bat_degree);
			}

		ret = of_property_read_u32(np,"qcom,dsm_imaxma_warm_bat",&chip->dsm_imaxma_warm_bat);
			if (ret) {
				chip->dsm_imaxma_warm_bat = chip->temp_ctrl->imaxma_warm_bat;
				pr_err("get dsm_imaxma_warm_bat info fail!use default = %d\n",chip->dsm_imaxma_warm_bat);
			}

		ret = of_property_read_u32(np,"qcom,dsm_vmaxmv_warm_bat",&chip->dsm_vmaxmv_warm_bat);
			if (ret) {
				chip->dsm_vmaxmv_warm_bat  = chip->temp_ctrl->vmaxmv_warm_bat;
				pr_err("get dsm_vmaxmv_warm_bat info fail!use default = %d\n",chip->dsm_vmaxmv_warm_bat);
			}

		ret = of_property_read_u32(np,"qcom,dsm_hot_bat_degree",&chip->dsm_hot_bat_degree);
			if (ret) {
				chip->dsm_hot_bat_degree  = chip->temp_ctrl->hot_bat_degree;
				pr_err("get dsm_hot_bat_degree info fail!use default = %d\n",chip->dsm_hot_bat_degree);
			}

		pr_info("dsm_charge_info:dsm_cold_bat_degree = %d, dsm_customize_cool_bat_degree = %d, dsm_imaxma_customize_cool_bat = %d, dsm_cool_bat_degree = %d, dsm_imaxma_cool_bat = %d, dsm_vmaxmv_cool_bat = %d, dsm_warm_bat_degree = %d, dsm_imaxma_warm_bat = %d, dsm_vmaxmv_warm_bat = %d, dsm_hot_bat_degree = %d\n", chip->dsm_cold_bat_degree, chip->dsm_customize_cool_bat_degree, chip->dsm_imaxma_customize_cool_bat, chip->dsm_cool_bat_degree, chip->dsm_imaxma_cool_bat, chip->dsm_vmaxmv_cool_bat, chip->dsm_warm_bat_degree, chip->dsm_imaxma_warm_bat, chip->dsm_vmaxmv_warm_bat, chip->dsm_hot_bat_degree);

		/* get dts config */
		ret = of_property_read_u32(np, "huawei,bat-ov-th", &chip->dsm_bat_ov_th);
		if (ret) {
			pr_info("read battery over voltage threshold, use default value 4450mV\n");
			chip->dsm_bat_ov_th = HIGH_VOL;
		}
		ret = of_property_read_u32(np, "huawei,volt-when-soc-0", &chip->dsm_volt_when_soc_0);
		if (ret) {
			pr_info("read volt when soc 0 failed, use default value 3600mV\n");
			chip->dsm_volt_when_soc_0 = VOL_THR1;
		}
		ret = of_property_read_u32(np, "huawei,volt-when-soc-2", &chip->dsm_volt_when_soc_2);
		if (ret) {
			pr_info("read volt when soc 2 failed, use default value 3700mV\n");
			chip->dsm_volt_when_soc_2 = VOL_THR2;
		}
		ret = of_property_read_u32(np, "huawei,volt-when-soc-90", &chip->dsm_volt_when_soc_90);
		if (ret) {
			pr_info("read volt when soc 90 failed, use default value 4350mV\n");
			chip->dsm_volt_when_soc_90 = VOL_HIGH;
		}
		ret = of_property_read_u32(np, "huawei,low-volt-when-soc-2", &chip->dsm_low_volt_when_soc_2);
		if (ret) {
			pr_info("read low volt when soc 2 failed, use default value 3200mV");
			chip->dsm_low_volt_when_soc_2 = VOL_TOO_LOW;
		}

    /* read dsm iv range raise percentage */
    ret = of_property_read_u32(np, "qcom,dsm_iv_range_raise_pct", &raise_pct);
    if (ret)
    {
        raise_pct = 0;
        pr_err("failed to read dsm iv range raise percentage\n");
        return;
    }

    /* if raise_pct > 0, meaning need to raise dsm iv range, update dsm iv_val */
    if ( 0 < raise_pct )
    {
        chip->dsm_imaxma_cool_bat = dsm_update_iv_val(chip->dsm_imaxma_cool_bat, raise_pct);
        chip->dsm_imaxma_customize_cool_bat = dsm_update_iv_val(chip->dsm_imaxma_customize_cool_bat, raise_pct);
        chip->dsm_imaxma_warm_bat = dsm_update_iv_val(chip->dsm_imaxma_warm_bat, raise_pct);
    }
}

static int huawei_dsm_charger_probe(struct platform_device *pdev)
{
	struct hw_dsm_charger_info *chip = NULL;
	struct power_supply *usb_psy = NULL;
	struct power_supply *batt_psy = NULL;
	struct device_node* np = NULL;
	int rc = 0;

    if (!pdev) {
        pr_err("%s: invalid param, fatal error\n", __func__);
        return -EINVAL;
    }

	usb_psy = power_supply_get_by_name("usb");
	if (!usb_psy) {
		pr_err("usb supply not found deferring probe\n");
		return -EPROBE_DEFER;
	}
	batt_psy = power_supply_get_by_name("battery");
	if (!batt_psy) {
		pr_err("batt supply not found deferring probe\n");
		return -EPROBE_DEFER;
	}

	np = pdev->dev.of_node;
	if(NULL == np) {
		pr_err("np is NULL\n");
		return -EPROBE_DEFER;
	}

	pr_info("%s: entry.\n", __func__);

	chip = kzalloc(sizeof(*chip), GFP_KERNEL);
	if (!chip) {
		pr_err("alloc mem failed!\n");
		return -ENOMEM;
	}
	chip->usb_psy = usb_psy;
	chip->batt_psy = batt_psy;
	chip->dev = &pdev->dev;
	dev_set_drvdata(chip->dev, chip);

	chip->dsm_psy.name = "dsm";
	chip->dsm_psy.type = POWER_SUPPLY_TYPE_UNKNOWN;
	chip->dsm_psy.properties = NULL;
	chip->dsm_psy.num_properties = 0;
	chip->dsm_psy.get_property = chg_dsm_get_property;
	chip->dsm_psy.set_property = chg_dsm_set_property;
	chip->dsm_psy.external_power_changed = chg_dsm_external_power_changed;
	chip->dsm_psy.supplied_from = chg_dsm_supplied_from;
	chip->dsm_psy.num_supplies = ARRAY_SIZE(chg_dsm_supplied_from);
	chip->dsm_psy.property_is_writeable = NULL;
	rc = power_supply_register(chip->dev, &chip->dsm_psy);
	if (rc < 0) {
		pr_err("dsm psy register failed, rc = %d\n", rc);
	}

	chip->temp_ctrl = &temp_info;
	chip->dsm_err = &dsm_err;
	mutex_init(&chip->dsm_dump_lock);

	dsm_get_dt(chip,np);

	dsm_info = chip;

	if (!bms_dclient) {
		bms_dclient = dsm_register_client(&dsm_bms);
	}

	if (!charger_dclient) {
		charger_dclient = dsm_register_client(&dsm_charger);
	}

	INIT_DELAYED_WORK(&chip->check_charging_batt_status_work,
			check_charging_batt_status_work);

	schedule_delayed_work(&chip->check_charging_batt_status_work,
			msecs_to_jiffies(DELAY_TIME));

	pr_info("%s: OK.\n", __func__);
	return 0;
}

static int  huawei_dsm_charger_remove(struct platform_device *pdev)
{
	struct hw_dsm_charger_info *chip = NULL;

    if (!pdev) {
        pr_err("%s: invalid para, fatal error\n", __func__);
        return -EINVAL;
    }

    chip = dev_get_drvdata(&pdev->dev);
    if (!chip) {
        pr_err("%s: Cannot get dsm charger info, fatal error\n", __func__);
        return -EINVAL;
    }

	cancel_delayed_work_sync(&chip->check_charging_batt_status_work);
	mutex_destroy(&chip->dsm_dump_lock);
	return 0;
}

static int dsm_charger_suspend(struct device *dev)
{
	struct hw_dsm_charger_info *chip = NULL;

    if (!dev) {
        pr_err("%s: invalid para, fatal error\n", __func__);
        return -EINVAL;
    }

    chip = dev_get_drvdata(dev);
    if (!chip) {
        pr_err("%s: Cannot get dsm charger info, fatal error\n", __func__);
        return -EINVAL;
    }

	cancel_delayed_work_sync(&chip->check_charging_batt_status_work);
	return 0;
}
static int dsm_charger_resume(struct device *dev)
{
	struct hw_dsm_charger_info *chip = NULL;

    if (!dev) {
        pr_err("%s: invalid para, fatal error\n", __func__);
        return -EINVAL;
    }

    chip = dev_get_drvdata(dev);
    if (!chip) {
        pr_err("%s: Cannot get dsm charger info, fatal error\n", __func__);
        return -EINVAL;
    }

    /* reset resume flag as resume event happened */
	temp_resume_flag = 1;
	soc_jump_resume_flag = 1;
	schedule_delayed_work(&chip->check_charging_batt_status_work,
			msecs_to_jiffies(0));
	return 0;
}

static const struct dev_pm_ops hw_dsm_pm_ops =
{
	.suspend    = dsm_charger_suspend,
	.resume	    = dsm_charger_resume,
};


static struct of_device_id platform_hw_charger_ids[] =
{
	{
		.compatible = "huawei,dsm_charger",
		.data = NULL,
	},
	{
	},
};

static struct platform_driver huawei_dsm_charger_driver =
{
	.probe        = huawei_dsm_charger_probe,
	.remove       = huawei_dsm_charger_remove,
	.driver       = {
		.name           = "dsm_charger",
		.owner          = THIS_MODULE,
		.of_match_table = of_match_ptr(platform_hw_charger_ids),
		.pm             = &hw_dsm_pm_ops,
	},
};

static int __init huawei_dsm_charger_init(void)
{
	int ret = 0;
	ret = platform_driver_register(&huawei_dsm_charger_driver);

	return ret;
}
late_initcall_sync(huawei_dsm_charger_init);

static void __exit huawei_dsm_charger_exit(void)
{
	platform_driver_unregister(&huawei_dsm_charger_driver);
}

module_exit(huawei_dsm_charger_exit);

MODULE_AUTHOR("HUAWEI Inc");
MODULE_DESCRIPTION("hw dsm charger driver");
MODULE_LICENSE("GPL");
