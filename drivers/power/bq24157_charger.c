/*
 * bq2415x charger driver
 *
 * Copyright (C) 2011-2013	Pali Rohár <pali.rohar@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

/*
 * Datasheets:
 * http://www.ti.com/product/bq24150
 * http://www.ti.com/product/bq24150a
 * http://www.ti.com/product/bq24152
 * http://www.ti.com/product/bq24153
 * http://www.ti.com/product/bq24153a
 * http://www.ti.com/product/bq24155
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/param.h>
#include <linux/err.h>
#include <linux/workqueue.h>
#include <linux/sysfs.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/idr.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/power/bq24157_charger.h>
#include <linux/interrupt.h>
#include <linux/gpio.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/kthread.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regulator/machine.h>
#include <linux/debugfs.h>
#include <linux/bitops.h>
#include <linux/math64.h>
#include <linux/alarmtimer.h>
#include <linux/power/huawei_charger.h>
#include <linux/power/huawei_dsm_charger.h>

/* timeout for resetting chip timer */
#define DELAYED_TIME			2500
#define HYSTERTSIS_TIME			20	//sec


#define USB_CURRENT_LIMIT_2MA	2
#define USB_CURRENT_LIMIT_500MA	500
#define USB_CURRENT_LIMIT		540
#define USB_CHARGE_CURRENT		550
#define CURRENT_LIMIT_100MA		100
#define CURRENT_LIMIT_500MA		500
#define CURRENT_LIMIT_800MA		800
#define CURRENT_LIMIT_1800MA	1800
#define CURRENT_LIMIT_2000MA    2000
#define MAX_CHARGE_CURRENT		1500
#define BATTERY_VOL_THRESHOLD	3600
#define BQ24152_REG_0_FAULT_MASK	0x07
#define BQ24152_REG_0_STAT_MASK		0x30
#define BQ24152_CHG_STAT_FAULT	3
#define POOR_INPUT_FAULT_STATUS	3

#define CHARGE_CURRENT_STEP		100


#define BQ2415X_TIMER_TIMEOUT	10

#define BQ2415X_REG_STATUS		0x00
#define BQ2415X_REG_CONTROL		0x01
#define BQ2415X_REG_VOLTAGE		0x02
#define BQ2415X_REG_VENDER		0x03
#define BQ2415X_REG_CURRENT		0x04
#define BQ2415X_REG_SPECIAL		0x05
#define BQ2415X_REG_SAFE        0x06

/* safe charge current and voltage */
#define BQ2415X_SAFE_UNLIMIT    0xff

/* reset state for all registers */
#define BQ2415X_RESET_STATUS	BIT(6)
#define BQ2415X_RESET_CONTROL	(BIT(4)|BIT(5))
#define BQ2415X_RESET_VOLTAGE	(BIT(1)|BIT(3))
#define BQ2415X_RESET_CURRENT	(BIT(0)|BIT(3)|BIT(7))
#define BQ2415X_SPECIAL_CONTROL	BIT(2)

/* status register */
#define BQ2415X_BIT_TMR_RST		7
#define BQ2415X_BIT_OTG			7
#define BQ2415X_BIT_EN_STAT		6
#define BQ2415X_MASK_STAT		(BIT(4)|BIT(5))
#define BQ2415X_SHIFT_STAT		4
#define BQ2415X_BIT_BOOST		3
#define BQ2415X_MASK_FAULT		(BIT(0)|BIT(1)|BIT(2))
#define BQ2415X_SHIFT_FAULT		0

/* control register */
#define BQ2415X_MASK_LIMIT		(BIT(6)|BIT(7))
#define BQ2415X_SHIFT_LIMIT		6
#define BQ2415X_MASK_VLOWV		(BIT(4)|BIT(5))
#define BQ2415X_SHIFT_VLOWV		4
#define BQ2415X_BIT_TE			3
#define BQ2415X_BIT_CE			2
#define BQ2415X_BIT_HZ_MODE		1
#define BQ2415X_BIT_OPA_MODE		0

/* voltage register */
#define BQ2415X_MASK_VO			(BIT(2)|BIT(3)|BIT(4)|BIT(5)|BIT(6)|BIT(7))
#define BQ2415X_SHIFT_VO		2
#define BQ2415X_BIT_OTG_PL		1
#define BQ2415X_BIT_OTG_EN		0

/* vender register */
#define BQ2415X_MASK_VENDER		(BIT(5)|BIT(6)|BIT(7))
#define BQ2415X_SHIFT_VENDER	5
#define BQ2415X_MASK_PN			(BIT(3)|BIT(4))
#define BQ2415X_SHIFT_PN		3
#define BQ2415X_MASK_REVISION	(BIT(0)|BIT(1)|BIT(2))
#define BQ2415X_SHIFT_REVISION	0

/* current register */
#define BQ2415X_MASK_RESET		BIT(7)
#define BQ2415X_MASK_VI_CHRG	(BIT(4)|BIT(5)|BIT(6))
#define BQ2415X_SHIFT_VI_CHRG	4
/* N/A					BIT(3) */
#define BQ2415X_MASK_VI_TERM	(BIT(0)|BIT(1)|BIT(2))
#define BQ2415X_SHIFT_VI_TERM	0

#define BYTE_BIT_NUM			8
#define REVISION_MIN	0	/* which means revision 1.0 */
#define REVISION_MAX	3	/* which means revision 1.3 */
#define REVISION_ONE	1	/* which means revision 1.1 */
#define VENDER_SHIFT_1	10
#define VENDER_SHIFT_2	100
#define BQ2415X_SLAVE_ADDR1 	0x6b
#define BQ2415X_SLAVE_ADDR2 	0x6a
#define BQ2415X_REG_NUMS     7

#define IIN_100_MA				100
#define IIN_500_MA				500
#define IIN_800_MA				800
#define IIN_1800_MA				1800
#define IIN_100_REG_VAL			0
#define IIN_500_REG_VAL			1
#define IIN_800_REG_VAL			2
#define IIN_UNLIMIT_REG_VAL		3
#define WEAK_VBAT_3400			3400
#define WEAK_VBAT_3500			3500
#define WEAK_VBAT_3600			3600
#define WEAK_VBAT_STEP			100
#define WEAK_VBAT_GAP			50
#define CHG_VOLT_BASE			3500
#define CHG_VOLT_MAX			4400
#define CHG_VOLT_STEP			20
#define CHG_CURR_BASE 			37400
#define CHG_CURR_MAX_VAL		7
#define CHG_CURR_STEP			6800
#define CHG_ITERM_BASE			3400
#define CHG_ITERM_STEP			3400
#define CHG_ITERM_MAX_VAL		7

#define HOT_DESIGN_MAX_CURRENT	3000
#define CDP_DETECT_DELAY		10
#define CHG_FULL_DEFAULT		3100
#define BATT_TEMP_DEFAULT		250
#define CHG_VOLT_DEFAULT		4400
#define JEITA_COLD_DEGREE		0
#define JEITA_COOL_DEGREE		10
#define JEITA_GOOD_DEGREE		45
#ifdef CONFIG_HLTHERM_RUNTEST
#define JEITA_WARM_DEGREE		60
#else
#define JEITA_WARM_DEGREE		50
#endif
#define JEITA_RESUME_DEGREE		2

#define AUTO_MODE_UNSUPPORT		-1
#define AUTO_MODE_DISABLED		0
#define AUTO_MODE_ENABLED		1
#define AUTO_TIMER_ENABLE		1
#define AUTO_TIMER_DISABLE		0

#define CHG_STS_READY			0
#define CHG_STS_CHARGING		1
#define CHG_STS_DONE			2
#define CHG_STS_FAULT			3

/* fault for charge mode */
#define CHG_NO_ERROR			0x0
#define CHG_VBUS_OVP			0x01
#define CHG_SLEEP_MODE			0x02
#define CHG_BAD_ADAPTOR			0x03
#define CHG_OUT_OVP				0x04
#define CHG_THERM_SHUTDOWN		0x05
#define CHG_TIMER_EXPIRE		0x06
#define CHG_NO_BATTERY			0x07
/* fault for boost mode */
#define BOOST_NO_ERROR			0x0
#define BOOST_VBUS_OVP			0x01
#define BOOST_OVER_LOAD			0x02
#define BOOST_VBAT_LOW			0x03
#define BOOST_VBAT_OVP			0x04
#define BOOST_THERM_SHUTDOWN	0x05
#define BOOST_TIMER_EXPIRE		0x06
#define BOOST_NA				0x07

#define MA_TO_UA                1000
#define MV_TO_UV                1000
#define REG_MAX                 4
#define MAX_NUMBER_BYTE         255
#define INPUT_NUMBER_BASE       10
#define FULL_CAPACITY           100
#define GAUGE_OLD_VERSION_ID    113
#define DEFAULT_TERM_CURR       100

#define VENDOR_CODE_OFFSET      5
#define VENDOR_CODE_TI          0x2
#define VENDOR_CODE_ONSEMI      0x4

#define RSENSE_68MOHM           68
#define RSENSE_100MOHM          100
#ifdef CONFIG_HLTHERM_RUNTEST
static int high_temp =JEITA_WARM_DEGREE;
#endif
static int onsemi_chg_curr_setting_68[] = {
    550, 650, 750, 850, 1050, 1150, 1350, 1450
};
static int onsemi_chg_curr_setting_100[] = {
    374, 442, 510, 578, 714, 782, 918, 986
};

static int g_boost_mode_enable_flag = false;
static int user_set_term_current_ma = DEFAULT_TERM_CURR;
static int g_chg_status = POWER_SUPPLY_STATUS_UNKNOWN;
static int use_fg_ctrl_chg = true;
static int user_set_charge_current_ma = MAX_CHARGE_CURRENT;
static int user_set_charge_voltage_mv = CHG_VOLT_MAX; 
static int factory_diag_flag = 0;
static int factory_diag_last_current_ma = 0;
static int bq2415x_factory_diag(struct bq2415x_device *bq,int val);
static void bq2415x_update_charge_status(struct bq2415x_device *bq);



/* each registered chip must have unique id */
static DEFINE_IDR(bq2415x_id);

static DEFINE_MUTEX(bq2415x_id_mutex);
static DEFINE_MUTEX(bq2415x_timer_mutex);
static DEFINE_MUTEX(bq2415x_i2c_mutex);
static int poor_input_enable = 0;
static int recharge_cv_lock = 0;

static struct bq2415x_device *g_bq = NULL;
static int hot_design_current = HOT_DESIGN_MAX_CURRENT;
static int user_in_curr_limit = CURRENT_LIMIT_1800MA;
static void set_charging_by_current_limit(struct bq2415x_device *bq);
void bq2415x_get_register_head(char *);
void bq2415x_dump_register(char *);
/**** i2c read functions ****/

/* read value from register */
static int bq2415x_i2c_read(struct bq2415x_device *bq, u8 reg)
{
	struct i2c_client *client = NULL;
	struct i2c_msg msg[2];
	u8 val = 0;
	int ret = 0;

	if (!bq) {
		pr_err("%s: Invalid para, fatal error\n", __func__);
		return -EINVAL;
	}
	client = to_i2c_client(bq->dev);
	if (!client->adapter)
		return -ENODEV;

	msg[0].addr = client->addr;
	msg[0].flags = 0;
	msg[0].buf = &reg;
	msg[0].len = sizeof(reg);
	msg[1].addr = client->addr;
	msg[1].flags = I2C_M_RD;
	msg[1].buf = &val;
	msg[1].len = sizeof(val);

	mutex_lock(&bq2415x_i2c_mutex);
	ret = i2c_transfer(client->adapter, msg, ARRAY_SIZE(msg));
	mutex_unlock(&bq2415x_i2c_mutex);

	if (ret < 0) {
		pr_err("%s: ret = %d\n", __func__, ret);
		return ret;
	}

	return val;
}

/* read value from register, apply mask and right shift it */
static int bq2415x_i2c_read_mask(struct bq2415x_device *bq, u8 reg,
				 u8 mask, u8 shift)
{
	int ret = 0;

	if (!bq) {
		pr_err("%s: Invalid para, fatal error\n", __func__);
		return -EINVAL;
	}

	if (shift > BYTE_BIT_NUM)
		return -EINVAL;

	ret = bq2415x_i2c_read(bq, reg);
	if (ret < 0)
		return ret;
	return (ret & mask) >> shift;
}

/* read value from register and return one specified bit */
static int bq2415x_i2c_read_bit(struct bq2415x_device *bq, u8 reg, u8 bit)
{
	if (!bq) {
		pr_err("%s: Invalid para, fatal error\n", __func__);
		return -EINVAL;
	}

	if (bit > BYTE_BIT_NUM)
		return -EINVAL;
	return bq2415x_i2c_read_mask(bq, reg, BIT(bit), bit);
}

/**** i2c write functions ****/

/* write value to register */
static int bq2415x_i2c_write(struct bq2415x_device *bq, u8 reg, u8 val)
{
	struct i2c_client *client = NULL;
	struct i2c_msg msg[1];
	u8 data[2];
	int ret = 0;

	if (!bq) {
		pr_err("%s: Invalid para, fatal error\n", __func__);
		return -EINVAL;
	}

	client = to_i2c_client(bq->dev);

	data[0] = reg;
	data[1] = val;

	msg[0].addr = client->addr;
	msg[0].flags = 0;
	msg[0].buf = data;
	msg[0].len = ARRAY_SIZE(data);

	mutex_lock(&bq2415x_i2c_mutex);
	ret = i2c_transfer(client->adapter, msg, ARRAY_SIZE(msg));
	mutex_unlock(&bq2415x_i2c_mutex);

	/* i2c_transfer returns number of messages transferred */
	if (ret < 0)
		return ret;
	else if (ret != 1)
		return -EIO;

	return 0;
}

/* read value from register, change it with mask left shifted and write back */
static int bq2415x_i2c_write_mask(struct bq2415x_device *bq, u8 reg, u8 val,
				  u8 mask, u8 shift)
{
	int ret = 0, rc = 0;

	if (!bq) {
		pr_err("%s: invalid param, fatal error\n", __func__);
		return -EINVAL;
	}

	if (shift > BYTE_BIT_NUM)
		return -EINVAL;

	ret = bq2415x_i2c_read(bq, reg);
	if (ret < 0)
	{
	   pr_info("bq24152 i2c read faild, ret = %d!\n, ret");
		return ret;
	}

	ret &= ~mask;
	ret |= val << shift;

	rc = bq2415x_i2c_write(bq, reg, ret);
	if(rc < 0)
	{
		pr_info("bq24152 i2c write faild!\n");
	}
	return rc;
}

/* change only one bit in register */
static int bq2415x_i2c_write_bit(struct bq2415x_device *bq, u8 reg,
				 bool val, u8 bit)
{
	if (!bq) {
		pr_err("%s: Invalid param, fatal error\n", __func__);
		return -EINVAL;
	}

	if (bit > BYTE_BIT_NUM)
		return -EINVAL;
	return bq2415x_i2c_write_mask(bq, reg, val, BIT(bit), bit);
}

/**** global functions ****/
static void bq2415x_dump_reg(struct bq2415x_device *bq)
{
	int ret = 0;
	int i = 0;
    /* dump valid register for bq24157 chip */
	int reg[BQ2415X_REG_NUMS] = {0x00,0x01,0x02,0x03,0x04,0x05,0x06};

	if (!bq) {
		pr_err("%s: invalid param, fatal error\n", __func__);
		return;
	}

	for(i=0;i<BQ2415X_REG_NUMS;i++){
		ret = bq2415x_i2c_read(bq, reg[i]);
		pr_info("bq2415x_dump_reg reg:%x=>%x\n",reg[i],ret);
	}
}

/* exec command function */
static int bq2415x_exec_command(struct bq2415x_device *bq,
				enum bq2415x_command command)
{
	int ret = 0;

	if (!bq) {
		pr_err("%s: invalid param, fatal error\n", __func__);
		return -EINVAL;
	}

	switch (command) {
	case BQ2415X_TIMER_RESET:
		return bq2415x_i2c_write_bit(bq, BQ2415X_REG_STATUS,
				1, BQ2415X_BIT_TMR_RST);
	case BQ2415X_OTG_STATUS:
		return bq2415x_i2c_read_bit(bq, BQ2415X_REG_STATUS,
				BQ2415X_BIT_OTG);
	case BQ2415X_STAT_PIN_STATUS:
		return bq2415x_i2c_read_bit(bq, BQ2415X_REG_STATUS,
				BQ2415X_BIT_EN_STAT);
	case BQ2415X_STAT_PIN_ENABLE:
		return bq2415x_i2c_write_bit(bq, BQ2415X_REG_STATUS, 1,
				BQ2415X_BIT_EN_STAT);
	case BQ2415X_STAT_PIN_DISABLE:
		return bq2415x_i2c_write_bit(bq, BQ2415X_REG_STATUS, 0,
				BQ2415X_BIT_EN_STAT);
	case BQ2415X_CHARGE_STATUS:
		return bq2415x_i2c_read_mask(bq, BQ2415X_REG_STATUS,
				BQ2415X_MASK_STAT, BQ2415X_SHIFT_STAT);
	case BQ2415X_BOOST_STATUS:
		return bq2415x_i2c_read_bit(bq, BQ2415X_REG_STATUS,
				BQ2415X_BIT_BOOST);
	case BQ2415X_FAULT_STATUS:
		return bq2415x_i2c_read_mask(bq, BQ2415X_REG_STATUS,
			BQ2415X_MASK_FAULT, BQ2415X_SHIFT_FAULT);

	case BQ2415X_CHARGE_TERMINATION_STATUS:
		return bq2415x_i2c_read_bit(bq, BQ2415X_REG_CONTROL,
				BQ2415X_BIT_TE);
	case BQ2415X_CHARGE_TERMINATION_ENABLE:
		return bq2415x_i2c_write_bit(bq, BQ2415X_REG_CONTROL,
				1, BQ2415X_BIT_TE);
	case BQ2415X_CHARGE_TERMINATION_DISABLE:
		return bq2415x_i2c_write_bit(bq, BQ2415X_REG_CONTROL,
				0, BQ2415X_BIT_TE);
	case BQ2415X_CHARGER_STATUS:
		ret = bq2415x_i2c_read_bit(bq, BQ2415X_REG_CONTROL,
			BQ2415X_BIT_CE);
		if (ret < 0)
			return ret;
		else
			return ret > 0 ? 0 : 1;
	case BQ2415X_CHARGER_ENABLE:
		return bq2415x_i2c_write_bit(bq, BQ2415X_REG_CONTROL,
				0, BQ2415X_BIT_CE);
	case BQ2415X_CHARGER_DISABLE:
		return bq2415x_i2c_write_bit(bq, BQ2415X_REG_CONTROL,
				1, BQ2415X_BIT_CE);
	case BQ2415X_HIGH_IMPEDANCE_STATUS:
		return bq2415x_i2c_read_bit(bq, BQ2415X_REG_CONTROL,
				BQ2415X_BIT_HZ_MODE);
	case BQ2415X_HIGH_IMPEDANCE_ENABLE:
		return bq2415x_i2c_write_bit(bq, BQ2415X_REG_CONTROL,
				1, BQ2415X_BIT_HZ_MODE);
	case BQ2415X_HIGH_IMPEDANCE_DISABLE:
		return bq2415x_i2c_write_bit(bq, BQ2415X_REG_CONTROL,
				0, BQ2415X_BIT_HZ_MODE);
	case BQ2415X_BOOST_MODE_STATUS:
		return bq2415x_i2c_read_bit(bq, BQ2415X_REG_CONTROL,
				BQ2415X_BIT_OPA_MODE);
	case BQ2415X_BOOST_MODE_ENABLE:
        ret = bq2415x_i2c_write_bit(bq, BQ2415X_REG_CONTROL, 1, BQ2415X_BIT_OPA_MODE);
        if ( ret < 0 )
            pr_err("BQ2415X_BOOST_MODE_ENABLE: enable boost mode failed\n");
        ret = bq2415x_i2c_write_bit(bq, BQ2415X_REG_VOLTAGE, 1, BQ2415X_BIT_OTG_EN);
        if ( ret < 0 )
            pr_err("BQ2415X_BOOST_MODE_ENABLE: enable otg pin failed\n");
        return ret;
	case BQ2415X_BOOST_MODE_DISABLE:
        ret = bq2415x_i2c_write_bit(bq, BQ2415X_REG_CONTROL, 0, BQ2415X_BIT_OPA_MODE);
        if ( ret < 0 )
            pr_err("BQ2415X_BOOST_MODE_DISABLE: disable boost mode failed\n");
        ret = bq2415x_i2c_write_bit(bq, BQ2415X_REG_VOLTAGE, 0, BQ2415X_BIT_OTG_EN);
        if ( ret < 0 )
            pr_err("BQ2415X_BOOST_MODE_DISABLE: disable otg pin failed\n");
        return ret;
	case BQ2415X_OTG_LEVEL:
		return bq2415x_i2c_read_bit(bq, BQ2415X_REG_VOLTAGE,
				BQ2415X_BIT_OTG_PL);
	case BQ2415X_OTG_ACTIVATE_HIGH:
		return bq2415x_i2c_write_bit(bq, BQ2415X_REG_VOLTAGE,
				1, BQ2415X_BIT_OTG_PL);
	case BQ2415X_OTG_ACTIVATE_LOW:
		return bq2415x_i2c_write_bit(bq, BQ2415X_REG_VOLTAGE,
				0, BQ2415X_BIT_OTG_PL);
	case BQ2415X_OTG_PIN_STATUS:
		return bq2415x_i2c_read_bit(bq, BQ2415X_REG_VOLTAGE,
				BQ2415X_BIT_OTG_EN);
	case BQ2415X_OTG_PIN_ENABLE:
		return bq2415x_i2c_write_bit(bq, BQ2415X_REG_VOLTAGE,
				1, BQ2415X_BIT_OTG_EN);
	case BQ2415X_OTG_PIN_DISABLE:
		return bq2415x_i2c_write_bit(bq, BQ2415X_REG_VOLTAGE,
				0, BQ2415X_BIT_OTG_EN);

	case BQ2415X_VENDER_CODE:
		return bq2415x_i2c_read_mask(bq, BQ2415X_REG_VENDER,
			BQ2415X_MASK_VENDER, BQ2415X_SHIFT_VENDER);
	case BQ2415X_PART_NUMBER:
		return bq2415x_i2c_read_mask(bq, BQ2415X_REG_VENDER,
				BQ2415X_MASK_PN, BQ2415X_SHIFT_PN);
	case BQ2415X_REVISION:
		return bq2415x_i2c_read_mask(bq, BQ2415X_REG_VENDER,
			BQ2415X_MASK_REVISION, BQ2415X_SHIFT_REVISION);
	}
	return -EINVAL;
}

/* detect chip type */
static enum bq2415x_chip bq2415x_detect_chip(struct bq2415x_device *bq)
{
	struct i2c_client *client = NULL;
    int ret = 0;

	if (!bq) {
		pr_err("%s: INvalid param, fatal error\n", __func__);
		return -EINVAL;
	}

	client = to_i2c_client(bq->dev);
	ret = bq2415x_exec_command(bq, BQ2415X_PART_NUMBER);
	pr_err("bq2415x_detect_chip ret = %d\n",ret);
	if (ret < 0)
		return ret;

	switch (client->addr) {
	case BQ2415X_SLAVE_ADDR1:
		switch (ret) {
		case 0:
			if (bq->chip == BQ24151A)
				return bq->chip;
			else
				return BQ24151;
		case 1:
			if (bq->chip == BQ24150A ||
				bq->chip == BQ24152 ||
				bq->chip == BQ24155)
				return bq->chip;
			else
				return BQ24150;
		case 2:
			if (bq->chip == BQ24157)
				return bq->chip;
			else
				return BQ24153;
		default:
			return BQUNKNOWN;
		}
		break;

	case BQ2415X_SLAVE_ADDR2:
		switch (ret) {
		case 0:
			if (bq->chip == BQ24156A)
				return bq->chip;
			else
				return BQ24156;
		case 2:
			if (bq->chip == BQ24157)
				return bq->chip;
		default:
			return BQUNKNOWN;
		}
		break;
	}

	return BQUNKNOWN;
}


/* detect chip revision */
static int bq2415x_detect_revision(struct bq2415x_device *bq)
{
    int ret = 0;
    int chip = 0;

	if (!bq) {
		pr_err("%s: INvalid param, fatal error\n", __func__);
		return -EINVAL;
	}

	ret = bq2415x_exec_command(bq, BQ2415X_REVISION);
	chip = bq2415x_detect_chip(bq);
	pr_err("bq2415x_detect_revision %d %d\n",ret,chip);
	if (ret < 0 || chip < 0)
		return -EINVAL;

	switch (chip) {
	case BQ24150:
	case BQ24150A:
	case BQ24151:
	case BQ24151A:
	case BQ24152:
		if (ret >= REVISION_MIN && ret <= REVISION_MAX)
			return ret;
		else
			return -EINVAL;
	case BQ24153:
	case BQ24153A:
	case BQ24156:
	case BQ24156A:
	case BQ24157:
	case BQ24158:
		if (ret == REVISION_MAX)
			return REVISION_MIN;
		else if (ret == REVISION_ONE)
			return REVISION_ONE;
		else
			return -EINVAL;
	case BQ24155:
		/* for BQ24155 only have 1.3 */
		if (ret == REVISION_MAX)
			return REVISION_MAX;
		else
			return -EINVAL;
	case BQUNKNOWN:
		return -EINVAL;
	}

	return -EINVAL;
}

/* return chip vender code */
static int bq2415x_get_vender_code(struct bq2415x_device *bq)
{
	int ret = 0;

	if (!bq) {
		pr_err("%s: INvalid param, fatal error\n", __func__);
		return -EINVAL;
	}

	ret = bq2415x_exec_command(bq, BQ2415X_VENDER_CODE);
	if (ret < 0)
		return 0;

	/* convert to binary */
	return (ret & 0x1) +
		   ((ret >> 1) & 0x1) * VENDER_SHIFT_1 +
		   ((ret >> 2) & 0x1) * VENDER_SHIFT_2;
}

/* reset all chip registers to default state */
static void bq2415x_reset_chip(struct bq2415x_device *bq)
{
	if (!bq) {
		pr_err("%s: INvalid param, fatal error\n", __func__);
		return -EINVAL;
	}
    /* unlimit safe charge current and voltage */
    bq2415x_i2c_write(bq, BQ2415X_REG_SAFE, BQ2415X_SAFE_UNLIMIT);
    /*safety register should be written twice to ensure setting*/
    bq2415x_i2c_write(bq, BQ2415X_REG_SAFE, BQ2415X_SAFE_UNLIMIT);
	bq2415x_i2c_write(bq, BQ2415X_REG_CURRENT, BQ2415X_RESET_CURRENT);
	bq2415x_i2c_write(bq, BQ2415X_REG_VOLTAGE, BQ2415X_RESET_VOLTAGE);
	bq2415x_i2c_write(bq, BQ2415X_REG_CONTROL, BQ2415X_RESET_CONTROL);
	bq2415x_i2c_write(bq, BQ2415X_REG_STATUS,  BQ2415X_RESET_STATUS);
	bq->timer_error = NULL;
}

static struct power_supply *get_bms_psy(struct bq2415x_device *bq)
{
	if (!bq) {
		pr_err("%s: INvalid param, fatal error\n", __func__);
		return -EINVAL;
	}

	if (bq->bms_psy)
		return bq->bms_psy;
	bq->bms_psy = power_supply_get_by_name("bms");
	if (!bq->bms_psy)
		pr_debug("bms power supply not found\n");
	
	return bq->bms_psy;
}

static int bq2415x_get_batt_property(struct bq2415x_device *bq,
				enum power_supply_property psp,
				union power_supply_propval *val)
{
	struct power_supply *bms_psy = NULL;
	int ret = 0;

	if (!bq || !val) {
		pr_err("%s: INvalid param, fatal error\n", __func__);
		return -EINVAL;
	}

	bms_psy = get_bms_psy(bq);
	if (!bms_psy)
		return -EINVAL;
	
	ret = bms_psy->get_property(bms_psy, psp, val);

	return ret;
}

/**** properties functions ****/

/* set current limit in mA */
static int bq2415x_set_current_limit(struct bq2415x_device *bq, int mA)
{
	int val = 0;

	if (!bq) {
		pr_err("%s: INvalid param, fatal error\n", __func__);
		return -EINVAL;
	}

	if (mA <= IIN_100_MA)
		val = IIN_100_REG_VAL;
	else if (mA <= IIN_500_MA)
		val = IIN_500_REG_VAL;
	else if (mA <= IIN_800_MA)
		val = IIN_800_REG_VAL;
	else
		val = IIN_UNLIMIT_REG_VAL;

	return bq2415x_i2c_write_mask(bq, BQ2415X_REG_CONTROL, val,
			BQ2415X_MASK_LIMIT, BQ2415X_SHIFT_LIMIT);
}

/* get current limit in mA */
static int bq2415x_get_current_limit(struct bq2415x_device *bq)
{
	int ret = 0;

	if (!bq) {
		pr_err("%s: INvalid param, fatal error\n", __func__);
		return -EINVAL;
	}

	ret = bq2415x_i2c_read_mask(bq, BQ2415X_REG_CONTROL,
			BQ2415X_MASK_LIMIT, BQ2415X_SHIFT_LIMIT);
	if (ret < 0)
		return ret;

	else if (ret == IIN_100_REG_VAL)
		return IIN_100_MA;
	else if (ret == IIN_500_REG_VAL)
		return IIN_500_MA;
	else if (ret == IIN_800_REG_VAL)
		return IIN_800_MA;
	else if (ret == IIN_UNLIMIT_REG_VAL)
		return IIN_1800_MA;

	return -EINVAL;
}

/* set weak battery voltage in mV */
static int bq2415x_set_weak_battery_voltage(struct bq2415x_device *bq, int mV)
{
	int val = 0;

	if (!bq) {
		pr_err("%s: INvalid param, fatal error\n", __func__);
		return -EINVAL;
	}

	/* round to 100mV */
	if (mV <= WEAK_VBAT_3400 + WEAK_VBAT_GAP)
		val = 0;
	else if (mV <= WEAK_VBAT_3500 + WEAK_VBAT_GAP)
		val = 1;
	else if (mV <= WEAK_VBAT_3600 + WEAK_VBAT_GAP)
		val = 2;
	else
		val = 3;

	return bq2415x_i2c_write_mask(bq, BQ2415X_REG_CONTROL, val,
			BQ2415X_MASK_VLOWV, BQ2415X_SHIFT_VLOWV);
}

/* get weak battery voltage in mV */
static int bq2415x_get_weak_battery_voltage(struct bq2415x_device *bq)
{
	int ret = 0;

	if (!bq) {
		pr_err("%s: INvalid param, fatal error\n", __func__);
		return -EINVAL;
	}

	ret = bq2415x_i2c_read_mask(bq, BQ2415X_REG_CONTROL,
			BQ2415X_MASK_VLOWV, BQ2415X_SHIFT_VLOWV);
	if (ret < 0)
		return ret;
	return WEAK_VBAT_3400 + WEAK_VBAT_STEP * ret;
}

/* set battery regulation voltage in mV */
static int bq2415x_set_battery_regulation_voltage(struct bq2415x_device *bq,
						  int mV)
{
	int val = 0;

	if (!bq) {
		pr_err("%s: INvalid param, fatal error\n", __func__);
		return -EINVAL;
	}

	if (mV > CHG_VOLT_MAX)
		mV = CHG_VOLT_MAX;

	/*
	 * According to datasheet, maximum battery regulation voltage is
	 * 4440mV which is b101111 = 47.
	 */
	val = (mV - CHG_VOLT_BASE) / CHG_VOLT_STEP;
	if (val < 0)
		val = 0;

	return bq2415x_i2c_write_mask(bq, BQ2415X_REG_VOLTAGE, val,
			BQ2415X_MASK_VO, BQ2415X_SHIFT_VO);
}

/* get battery regulation voltage in mV */
static int bq2415x_get_battery_regulation_voltage(struct bq2415x_device *bq)
{
	int ret = 0;

	if (!bq) {
		pr_err("%s: INvalid param, fatal error\n", __func__);
		return -EINVAL;
	}
	ret = bq2415x_i2c_read_mask(bq, BQ2415X_REG_VOLTAGE,
			BQ2415X_MASK_VO, BQ2415X_SHIFT_VO);

	if (ret < 0)
		return ret;

	return CHG_VOLT_BASE + CHG_VOLT_STEP * ret;
}

/* set charge current in mA (platform data must provide resistor sense) */
static int bq2415x_set_charge_current(struct bq2415x_device *bq, int mA)
{
	int val = 0;
	int setting_cnt = 0;
	int *setting = NULL;

	if (!bq) {
		pr_err("%s: INvalid param, fatal error\n", __func__);
		return -EINVAL;
	}

	if (bq->init_data.resistor_sense <= 0)
		return -ENOSYS;

	if (VENDOR_CODE_ONSEMI != bq->vendor_code) {
		val = (mA * bq->init_data.resistor_sense - CHG_CURR_BASE) / CHG_CURR_STEP;
	} else {
		if (RSENSE_68MOHM == bq->init_data.resistor_sense) {
			setting = onsemi_chg_curr_setting_68;
			setting_cnt = ARRAY_SIZE(onsemi_chg_curr_setting_68);
		} else if (RSENSE_100MOHM == bq->init_data.resistor_sense) {
			setting = onsemi_chg_curr_setting_100;
			setting_cnt = ARRAY_SIZE(onsemi_chg_curr_setting_100);
		} else {
			pr_err("%s: Invalid resistor sense %d\n",
					__func__, bq->init_data.resistor_sense);
			return -ENOSYS;
		}

		for (val = setting_cnt - 1; val >= 0; val--) {
			if (mA >= setting[val])
				break;
		}
	}

	if (val < 0)
		val = 0;
	else if (val > CHG_CURR_MAX_VAL)
		val = CHG_CURR_MAX_VAL;
	pr_info("bq2415x_set_charge_current val = %d\n",val);

	return bq2415x_i2c_write_mask(bq, BQ2415X_REG_CURRENT, val,
			BQ2415X_MASK_VI_CHRG | BQ2415X_MASK_RESET,
			BQ2415X_SHIFT_VI_CHRG);
}

/* get charge current in mA (platform data must provide resistor sense) */
static int bq2415x_get_charge_current(struct bq2415x_device *bq)
{
	int ret = 0;

	if (!bq) {
		pr_err("%s: INvalid param, fatal error\n", __func__);
		return -EINVAL;
	}

	if (bq->init_data.resistor_sense <= 0)
		return -ENOSYS;

	ret = bq2415x_i2c_read_mask(bq, BQ2415X_REG_CURRENT,
			BQ2415X_MASK_VI_CHRG, BQ2415X_SHIFT_VI_CHRG);
	if (ret < 0)
		return ret;

	return (CHG_CURR_BASE + CHG_CURR_STEP*ret) / bq->init_data.resistor_sense;
}

/* set termination current in mA (platform data must provide resistor sense) */
static int bq2415x_set_termination_current(struct bq2415x_device *bq, int mA)
{
	int val = 0;

	if (!bq) {
		pr_err("%s: INvalid param, fatal error\n", __func__);
		return -EINVAL;
	}

	if (bq->init_data.resistor_sense <= 0)
		return -ENOSYS;

    if (mA > user_set_term_current_ma) {
        mA = user_set_term_current_ma;
    }

	val = (mA * bq->init_data.resistor_sense - CHG_ITERM_BASE) / CHG_ITERM_STEP;
	if (val < 0)
		val = 0;
	else if (val > CHG_ITERM_MAX_VAL)
		val = CHG_ITERM_MAX_VAL;

	return bq2415x_i2c_write_mask(bq, BQ2415X_REG_CURRENT, val,
			BQ2415X_MASK_VI_TERM | BQ2415X_MASK_RESET,
			BQ2415X_SHIFT_VI_TERM);
}

/* get termination current in mA (platform data must provide resistor sense) */
static int bq2415x_get_termination_current(struct bq2415x_device *bq)
{
	int ret = 0;

	if (!bq) {
		pr_err("%s: INvalid param, fatal error\n", __func__);
		return -EINVAL;
	}

	if (bq->init_data.resistor_sense <= 0)
		return -ENOSYS;

	ret = bq2415x_i2c_read_mask(bq, BQ2415X_REG_CURRENT,
			BQ2415X_MASK_VI_TERM, BQ2415X_SHIFT_VI_TERM);
	if (ret < 0)
		return ret;
	return (CHG_ITERM_BASE + CHG_ITERM_STEP*ret) / bq->init_data.resistor_sense;
}

/* set default value of property */
#define bq2415x_set_default_value(bq, prop) \
	do { \
		int ret = 0; \
		if (bq->init_data.prop != -1) \
			ret = bq2415x_set_##prop(bq, bq->init_data.prop); \
		if (ret < 0) \
			return ret; \
	} while (0)

/* set default values of all properties */
static int bq2415x_set_defaults(struct bq2415x_device *bq)
{
	int rc = 0;

	if (!bq) {
		pr_err("%s: Invalid param, fatal error\n", __func__);
		return -EINVAL;
	}

	rc = bq2415x_exec_command(bq, BQ2415X_STAT_PIN_DISABLE);
	if(rc < 0)
	{
		pr_info("disable pin failed\n");
		return rc;
	}

	rc = bq2415x_exec_command(bq, BQ2415X_BOOST_MODE_DISABLE);
	if(rc < 0)
	{
		pr_info("boost mode set failed\n");
		return rc;
	}
	rc = bq2415x_exec_command(bq, BQ2415X_CHARGER_DISABLE);
	if(rc < 0)
	{
		pr_info("charger disable set failed\n");
		return rc;
	}

	rc = bq2415x_exec_command(bq, BQ2415X_CHARGE_TERMINATION_DISABLE);
	if(rc < 0)
	{
		pr_info("charge termination disable set failed\n");
		return rc;
	}

	bq2415x_set_default_value(bq, current_limit);
	bq2415x_set_default_value(bq, weak_battery_voltage);
	bq2415x_set_default_value(bq, battery_regulation_voltage);

	if (bq->init_data.resistor_sense > 0) {
		bq2415x_set_default_value(bq, charge_current);
		bq2415x_set_default_value(bq, termination_current);
		if (!use_fg_ctrl_chg)
			bq2415x_exec_command(bq, BQ2415X_CHARGE_TERMINATION_ENABLE);
	}

	rc = bq2415x_exec_command(bq, BQ2415X_CHARGER_ENABLE);
	if(rc < 0)
	{
		pr_info("charge enable set failed\n");
		return rc;
	}

	return 0;
}

/**** charger mode functions ****/

/* set charger mode */
static int bq2415x_set_mode(struct bq2415x_device *bq, enum bq2415x_mode mode)
{
	int ret = 0;
	int charger = 0;
	int boost = 0;

	if (!bq) {
		pr_err("%s: Invalid param, fatal error\n", __func__);
		return -EINVAL;
	}

	if (mode == BQ2415X_MODE_BOOST)
		boost = 1;
	else if (mode != BQ2415X_MODE_OFF)
		charger = 1;

	if (!charger)
		ret = bq2415x_exec_command(bq, BQ2415X_CHARGER_DISABLE);

	if (!boost)
		ret = bq2415x_exec_command(bq, BQ2415X_BOOST_MODE_DISABLE);

	if (ret < 0)
		return ret;

	switch (mode) {
	case BQ2415X_MODE_OFF:
		dev_dbg(bq->dev, "changing mode to: Offline\n");
		ret = bq2415x_set_current_limit(bq, IIN_100_MA);
		break;
	case BQ2415X_MODE_NONE:
		dev_dbg(bq->dev, "changing mode to: N/A\n");
		ret = bq2415x_set_current_limit(bq, IIN_100_MA);
		break;
	case BQ2415X_MODE_HOST_CHARGER:
		dev_dbg(bq->dev, "changing mode to: Host/HUB charger\n");
		ret = bq2415x_set_current_limit(bq, IIN_500_MA);
		break;
	case BQ2415X_MODE_DEDICATED_CHARGER:
		dev_dbg(bq->dev, "changing mode to: Dedicated charger\n");
		ret = bq2415x_set_current_limit(bq, IIN_1800_MA);
		break;
	case BQ2415X_MODE_BOOST: /* Boost mode */
		dev_dbg(bq->dev, "changing mode to: Boost\n");
		ret = bq2415x_set_current_limit(bq, IIN_100_MA);
		break;
	}

	if (ret < 0)
		return ret;

	if (charger)
		ret = bq2415x_exec_command(bq, BQ2415X_CHARGER_ENABLE);
	else if (boost)
		ret = bq2415x_exec_command(bq, BQ2415X_BOOST_MODE_ENABLE);

	if (ret < 0)
		return ret;

	bq2415x_set_default_value(bq, weak_battery_voltage);
	bq2415x_set_default_value(bq, battery_regulation_voltage);

	bq->mode = mode;
	sysfs_notify(&bq->batt_psy.dev->kobj, NULL, "mode");

	return 0;

}



static void bq2415x_hook_function(enum bq2415x_mode mode, void *data)
{
	struct bq2415x_device *bq = data;

	if (!bq)
		return;

	dev_dbg(bq->dev, "hook function was called\n");
	bq->reported_mode = mode;

	/* if automode is not enabled do not tell about reported_mode */
	if (bq->automode < 1)
		return;

	sysfs_notify(&bq->batt_psy.dev->kobj, NULL, "reported_mode");
	bq2415x_set_mode(bq, bq->reported_mode);
}
/**** timer functions ****/

/* enable/disable auto resetting chip timer */
static void bq2415x_set_autotimer(struct bq2415x_device *bq, int state)
{
	if (!bq) {
		pr_err("%s: Invalid param, fatal error\n", __func__);
		return;
	}

	mutex_lock(&bq2415x_timer_mutex);

	if (bq->autotimer == state) {
		mutex_unlock(&bq2415x_timer_mutex);
		return;
	}

	bq->autotimer = state;

	if (state) {
		schedule_delayed_work(&bq->work, BQ2415X_TIMER_TIMEOUT * HZ);
		bq2415x_exec_command(bq, BQ2415X_TIMER_RESET);
		bq->timer_error = NULL;
	} else {
		cancel_delayed_work_sync(&bq->work);
	}

	mutex_unlock(&bq2415x_timer_mutex);
}

/* called by bq2415x_timer_work on timer error */
static void bq2415x_timer_error(struct bq2415x_device *bq, const char *msg)
{
	if (!bq || !msg) {
		pr_err("%s Invalid param, fatal error\n", __func__);
		return;
	}

	dev_err(bq->dev, "%s\n", msg);

}

/* delayed work function for auto resetting chip timer */
static void bq2415x_timer_work(struct work_struct *work)
{
	struct bq2415x_device *bq = NULL;
	int ret = 0;
	int error = 0;
	int boost = 0;

	if (!work) {
		pr_err("%s: Invalid param, fatal error\n", __func__);
		return;
	}

	bq = container_of(work, struct bq2415x_device, work.work);
	if (bq->automode > 0 && (bq->reported_mode != bq->mode)) {
		sysfs_notify(&bq->batt_psy.dev->kobj, NULL, "reported_mode");
		bq2415x_set_mode(bq, bq->reported_mode);
	}

	if (!bq->autotimer)
		return;
	pr_info("bq2415x_timer_work start ,bq->autotimer %d\n",bq->autotimer);

	boost = bq2415x_exec_command(bq, BQ2415X_BOOST_MODE_STATUS);
	if (boost < 0) {
		bq2415x_timer_error(bq, "Unknown error");
		return;
	}

	error = bq2415x_exec_command(bq, BQ2415X_FAULT_STATUS);
	if (error < 0) {
		bq2415x_timer_error(bq, "Unknown error");
		return;
	}

	if (boost || g_boost_mode_enable_flag) {
		switch (error) {
		/* Non fatal errors, chip is OK */
		case BOOST_NO_ERROR: /* No error */
			break;
		case BOOST_TIMER_EXPIRE: /* Timer expired */
			dev_err(bq->dev, "boost mode Timer expired\n");
			break;
		case BOOST_VBAT_LOW: /* Battery voltage too low */
			dev_err(bq->dev, "boost mode Battery voltage to low\n");
			break;

		/* Fatal errors, disable and reset chip */
		case BOOST_VBUS_OVP: /* Overvoltage protection (chip fried) */
			bq2415x_timer_error(bq,
				"boost mode vbus ovp (chip fried)");
			ret = bq2415x_set_mode(bq, BQ2415X_MODE_BOOST);
			if(ret < 0)
				dev_err(bq->dev, "BOOST_VBUS_OVP: set boost mode failed\n");
			break;
		case BOOST_OVER_LOAD: /* Overload */
			bq2415x_timer_error(bq, "boost mode Overload");
			ret = bq2415x_set_mode(bq, BQ2415X_MODE_BOOST);
			if(ret < 0)
				dev_err(bq->dev, "BOOST_OVER_LOAD: set boost mode failed\n");
			break;
		case BOOST_VBAT_OVP: /* Battery overvoltage protection */
			bq2415x_timer_error(bq,
				"boost mode Battery overvoltage protection");
			break;
		case BOOST_THERM_SHUTDOWN: /* Thermal shutdown (too hot) */
			bq2415x_timer_error(bq,
					"boost mode Thermal shutdown (too hot)");
			break;
		case BOOST_NA: /* N/A */
			bq2415x_timer_error(bq, "boost mode Unknown error");
			break;
		}

		boost = bq2415x_exec_command(bq, BQ2415X_BOOST_MODE_STATUS);
		dev_err(bq->dev, "get boost mode status: boost=%d\n", boost);
		if (boost < 0) {
			bq2415x_timer_error(bq, "get boost mode status, Unknown error");
		}

		if (!boost && g_boost_mode_enable_flag)
		{
			bq2415x_timer_error(bq, "need enable boost mode but not ok, try again!");
			/*safety register should be written twice to ensure setting*/
			ret = bq2415x_i2c_write(bq, BQ2415X_REG_SAFE, BQ2415X_SAFE_UNLIMIT);
			ret = bq2415x_i2c_write(bq, BQ2415X_REG_SAFE, BQ2415X_SAFE_UNLIMIT);
			if(ret < 0)
			{
				dev_err(bq->dev,"BQ2415X_REG_SAFE: bq24152 i2c write failed!\n");
			}
			ret = bq2415x_set_mode(bq, BQ2415X_MODE_BOOST);
			if(ret < 0)
				dev_err(bq->dev, "Set boost mode failed\n");
		}
	} else {
		switch (error) {
		/* Non fatal errors, chip is OK */
		case CHG_NO_ERROR: /* No error */
			break;
		case CHG_SLEEP_MODE: /* Sleep mode */
			dev_err(bq->dev, "Sleep mode\n");
			break;
		case CHG_BAD_ADAPTOR: /* Poor input source */
#ifdef CONFIG_HUAWEI_PMU_DSM
			dsm_post_chg_bms_info(DSM_CHG_BAD_CHARGER,
				"very weak charger\n");
#endif
			dev_err(bq->dev, "Poor input source\n");
			break;
		case CHG_TIMER_EXPIRE: /* Timer expired */
			dev_err(bq->dev, "Timer expired\n");
			break;
		case CHG_NO_BATTERY: /* No battery */
			dev_err(bq->dev, "No battery\n");
			break;

		/* Fatal errors, disable and reset chip */
		case CHG_VBUS_OVP: /* Overvoltage protection (chip fried) */
#ifdef CONFIG_HUAWEI_PMU_DSM
			dsm_post_chg_bms_info(DSM_CHG_OVP_ERROR_NO,
				"charger overvoltage protection\n");
#endif
			bq2415x_timer_error(bq,
				"Overvoltage protection (chip fried)");
			break;
		case CHG_OUT_OVP: /* Battery overvoltage protection */
			bq2415x_timer_error(bq,
				"Battery overvoltage protection");
			break;
		case CHG_THERM_SHUTDOWN: /* Thermal shutdown (too hot) */
			bq2415x_timer_error(bq,
				"Thermal shutdown (too hot)");
			break;
		}
	}

	ret = bq2415x_exec_command(bq, BQ2415X_TIMER_RESET);
	if (ret < 0) {
		bq2415x_timer_error(bq, "Resetting timer failed");
	}
	schedule_delayed_work(&bq->work, BQ2415X_TIMER_TIMEOUT * HZ);
}


static void bq2415x_iusb_work(struct work_struct *work)
{
	int rc = 0;
	int usb_ma=0, vterm = 0;
	struct bq2415x_device *bq = NULL;

	if (!work) {
		pr_err("%s: Invalid param, fatal error\n", __func__);
		return -EINVAL;
	}

	bq = container_of(work, struct bq2415x_device, iusb_work);
	usb_ma = min(bq->iusb_limit, user_in_curr_limit);
	if(usb_ma == 0) {
		pr_info("usb current is %d\n",bq->iusb_limit);
		rc = bq2415x_exec_command(bq, BQ2415X_CHARGER_DISABLE);
		if(rc < 0){
			pr_info("disable charger failed\n");
			return;
		}
		rc = bq2415x_exec_command(bq, BQ2415X_HIGH_IMPEDANCE_DISABLE);
		if (rc < 0) {
			pr_info("disalbe hiz mode fail\n");
			return;
		}

	} else {
		bq2415x_i2c_write(bq, BQ2415X_REG_SPECIAL, BQ2415X_SPECIAL_CONTROL);
		pr_info("usb current is %d\n",bq->iusb_limit);
		rc = bq2415x_set_current_limit(bq,usb_ma);
		if(rc < 0)
		{
			pr_info("current limit setting failed\n");
			return;
		}

		if(!recharge_cv_lock){
			vterm = min(CHG_VOLT_DEFAULT, user_set_charge_voltage_mv);
			rc = bq2415x_set_battery_regulation_voltage(bq, vterm);
			if(rc < 0)
			{
				pr_info("current limit setting failed\n");
				return;
			}
		}

		/*Set output current for DCP charger*/
		usb_ma = min(usb_ma, user_set_charge_current_ma);
		rc = bq2415x_set_charge_current(bq, usb_ma);
		if(rc < 0){
			pr_err("set charge current failed\n");
			return;
		}
		/* enable charging otherwise high/cold temperature or high impedance */
		if((!bq->charge_disable)
			&& (!bq2415x_exec_command(bq, BQ2415X_HIGH_IMPEDANCE_STATUS))){
			rc = bq2415x_exec_command(bq, BQ2415X_CHARGER_ENABLE);
			if(rc < 0){
				pr_info("charge enable set failed\n");
				return;
			}
		}

	}
	/* disable stat pin for turn off the led */
	rc = bq2415x_exec_command(bq, BQ2415X_STAT_PIN_DISABLE);
	if(rc < 0)
	{
		pr_info("disable pin failed\n");
		return;
	}

	power_supply_changed(&bq->batt_psy);

}

static inline bool is_device_suspended(struct bq2415x_device *bq);

/*===========================================
FUNCTION: bq2415x_get_charge_type
DESCRIPTION: get charge type
IPNUT:bq2415x_device *bq
RETURN:NONE or FAST
=============================================*/
int bq2415x_get_charge_type(struct bq2415x_device *bq)
{
	int rc = POWER_SUPPLY_CHARGE_TYPE_NONE;

    if (!bq) {
        pr_err("%s: invalid param, fatal error\n", __func__);
        return POWER_SUPPLY_CHARGE_TYPE_NONE;
    }

	if (is_device_suspended(bq))
		return POWER_SUPPLY_CHARGE_TYPE_NONE;

	rc = bq2415x_exec_command(bq, BQ2415X_CHARGE_STATUS);
	if (rc < 0) {
		pr_err("failed to read status");
		return POWER_SUPPLY_CHARGE_TYPE_NONE;
	} else if (rc == CHG_STS_CHARGING) {
		return POWER_SUPPLY_CHARGE_TYPE_FAST;
	} else {
		return POWER_SUPPLY_CHARGE_TYPE_NONE;
	}
}
/*===========================================
FUNCTION: bq24152_charger_present
DESCRIPTION: get charge present
IPNUT:NA
RETURN:1 or 0
=============================================*/
static bool bq2415x_charger_present(void)
{
	 return 1;
}

static int bq2415x_get_battery_temp(struct bq2415x_device *bq)
{
	int ret = 0;
	union power_supply_propval prop = {0, };
	if (!bq) {
		pr_err("%s: invalid param, fatal error\n", __func__);
		return BATT_TEMP_DEFAULT;
	}

	ret = bq->bms_psy->get_property(bq->bms_psy, POWER_SUPPLY_PROP_TEMP, &prop);
	if (ret) {
		pr_err("%s: get property temp from bms fail, ret = %d\n", __func__, ret);
		return BATT_TEMP_DEFAULT;
	}

	return prop.intval;
}

/*===========================================
FUNCTION: bq24152_get_battery_health
DESCRIPTION: get battery health
IPNUT:NA
RETURN:good or overheat
=============================================*/
#define CONVERT_TEMP    10
static int bq2415x_get_battery_health(struct bq2415x_device *bq)
{
	int temp = 0, health = POWER_SUPPLY_HEALTH_GOOD;
	static int last_health = POWER_SUPPLY_HEALTH_UNKNOWN;

	if (!bq) {
		pr_err("%s: invalid param, fatal error\n", __func__);
		return POWER_SUPPLY_HEALTH_UNKNOWN;
	}

	temp = bq2415x_get_battery_temp(bq) / CONVERT_TEMP;
	if (temp < JEITA_COLD_DEGREE) {
		health = POWER_SUPPLY_HEALTH_COLD;
	} else if (temp < JEITA_COOL_DEGREE) {
		health = POWER_SUPPLY_HEALTH_COOL;
	} else if (temp < JEITA_GOOD_DEGREE) {
		health = POWER_SUPPLY_HEALTH_GOOD;
#ifdef CONFIG_HLTHERM_RUNTEST
	} else if (temp < high_temp) {
#else
	} else if (temp < JEITA_WARM_DEGREE) {
#endif
		health = POWER_SUPPLY_HEALTH_WARM;
	} else {
		health = POWER_SUPPLY_HEALTH_OVERHEAT;
	}

#ifndef CONFIG_HLTHERM_RUNTEST
	if ( (POWER_SUPPLY_HEALTH_WARM == last_health) \
		&& (POWER_SUPPLY_HEALTH_GOOD == health) ) {
		/*from warm to good resume range*/
		if ( temp < (JEITA_GOOD_DEGREE - JEITA_RESUME_DEGREE) )
			health = POWER_SUPPLY_HEALTH_GOOD;
		else
			health = POWER_SUPPLY_HEALTH_WARM;
	} else if ( (POWER_SUPPLY_HEALTH_GOOD == last_health) \
		&& (POWER_SUPPLY_HEALTH_COOL == health) ) {
		/*from good to cool resume range*/
		if ( temp < (JEITA_COOL_DEGREE - JEITA_RESUME_DEGREE) )
			health = POWER_SUPPLY_HEALTH_COOL;
		else
			health = POWER_SUPPLY_HEALTH_GOOD;
	}

	if (last_health != health)
		last_health = health;
	else
		return health;

	if (true == use_fg_ctrl_chg) {
		/*judge to disable/enable IC termination*/
		if (POWER_SUPPLY_HEALTH_WARM == health) {
			bq2415x_exec_command(bq, BQ2415X_CHARGE_TERMINATION_ENABLE);
			pr_err("%s: in HEALTH_WARM to enable IC termination \n", __func__);
		} else {
			bq2415x_exec_command(bq, BQ2415X_CHARGE_TERMINATION_DISABLE);
			pr_err("%s: out HEALTH_WARM to disable IC termination \n", __func__);
		}
	}
#endif
	return health;
}

static int bq2415x_get_battery_ui_soc(struct bq2415x_device *bq)
{
	int ret = 0;
	union power_supply_propval prop = {0, };
	if (!bq) {
		pr_err("%s: invalid param, fatal error\n", __func__);
		return 0;
	}

	ret = bq->bms_psy->get_property(bq->bms_psy, POWER_SUPPLY_PROP_CAPACITY, &prop);
	if (ret) {
		pr_err("%s: get property ui soc from bms fail, ret = %d\n", __func__, ret);
		return 0;
	}

	return prop.intval;
}

static int bq2415x_get_battery_real_full_status(struct bq2415x_device *bq)
{
	int ret = 0;
	union power_supply_propval prop = {0, };
	if (!bq) {
		pr_err("%s: invalid param, fatal error\n", __func__);
		return 0;
	}

	ret = bq->bms_psy->get_property(bq->bms_psy, POWER_SUPPLY_PROP_ENERGY_FULL, &prop);
	if (ret) {
		pr_err("%s: get property real full status from bms fail, ret = %d\n", __func__, ret);
		return 0;
	}

	return prop.intval;
}

static int bq2415x_get_otg_present(struct bq2415x_device* bq)
{
	int ret = 0;
	union power_supply_propval prop = {0, };
	if (!bq) {
		pr_err("%s: invalid param, fatal error\n", __func__);
		return 0;
	}

	ret = bq->usb_psy->get_property(bq->usb_psy, POWER_SUPPLY_PROP_USB_OTG, &prop);
	if (ret) {
		pr_err("%s: get property usb otg status from usb_phy fail, ret = %d\n", __func__, ret);
		return 0;
	}

	return prop.intval;
}

static int bq2415x_get_usb_present_status(struct bq2415x_device *bq)
{
	int ret = 0;
	union power_supply_propval prop = {0, };
	if (!bq) {
		pr_err("%s: invalid param, fatal error\n", __func__);
		return 0;
	}

	ret = bq->usb_psy->get_property(bq->usb_psy, POWER_SUPPLY_PROP_PRESENT, &prop);
	if (ret) {
		pr_err("%s: get property usb present status from usb_phy fail, ret = %d\n", __func__, ret);
		return 0;
	}

	return prop.intval;
}

static void bq2415x_update_charge_status(struct bq2415x_device *bq)
{
	int ret = 0;
	int chg_ic_sts = CHG_STS_READY;
	int batt_health = POWER_SUPPLY_HEALTH_GOOD;
	int batt_soc = 0;
	int usb_present = false;
	int otg_present = false;
	int hiz_status = false;

	if (!bq) {
		pr_err("bq2415x_update_charge_status: invalid param, fatal error\n");
		return;
	}

	usb_present = bq->usb_present;
	if (false == usb_present) /*usb absent to set discharging*/
	{
		g_chg_status = POWER_SUPPLY_STATUS_DISCHARGING;
		return;
	}

	batt_health = bq2415x_get_battery_health(bq);
	batt_soc = bq2415x_get_battery_ui_soc(bq);
	otg_present = bq2415x_get_otg_present(bq);

	hiz_status = bq2415x_exec_command(bq, BQ2415X_HIGH_IMPEDANCE_STATUS);
	if (ret < 0) {
		pr_err("%s: get hiz_status failed\n", __func__);
	}

	chg_ic_sts = bq2415x_exec_command(bq, BQ2415X_CHARGE_STATUS);
	pr_err("chg_ic_sts = %x\n", chg_ic_sts);
	if (chg_ic_sts < 0)
	{
		pr_err("bq2415x get charge ic status error\n");
	}

	if ( (batt_health == POWER_SUPPLY_HEALTH_COLD) \
			|| (batt_health == POWER_SUPPLY_HEALTH_OVERHEAT) )
	{
		g_chg_status = POWER_SUPPLY_STATUS_NOT_CHARGING;
	}
	else if ( (batt_soc >= FULL_CAPACITY) && (true == usb_present) ) /* Battery Soc Charge Full */
	{
		g_chg_status = POWER_SUPPLY_STATUS_FULL;
	}
	else if (chg_ic_sts == CHG_STS_CHARGING) /*charge ic Charge in progress*/
	{
		g_chg_status = POWER_SUPPLY_STATUS_CHARGING;
	}
	else if ( (chg_ic_sts == CHG_STS_DONE) && (false == use_fg_ctrl_chg) ) /* charge ic Charge done */
	{
		g_chg_status = POWER_SUPPLY_STATUS_FULL;
	}
	else if (true == usb_present) /*other situation usb present notify charging status*/
	{
		g_chg_status = POWER_SUPPLY_STATUS_CHARGING;
		/* for charging status when hiz*/
		if (true == hiz_status){
			g_chg_status = POWER_SUPPLY_STATUS_DISCHARGING;
		}
	}
	else
	{
		g_chg_status = POWER_SUPPLY_STATUS_DISCHARGING;
	}

	if (true == otg_present) /*otg mode need to set discharging*/
	{
		g_chg_status = POWER_SUPPLY_STATUS_DISCHARGING;
	}
	return;
}

/*===========================================
FUNCTION: bq2415x_usb_low_power_work
DESCRIPTION: to ajust charge current for low power(poor input) charger
IPNUT:	bq2415x_device *bq
RETURN:	N/A
=============================================*/
static void bq2415x_usb_low_power_work(struct work_struct *work)
{
	struct bq2415x_device *bq = NULL;
	int usb_ma = 0, usb_present = 0, rc = 0;
	int vbat = 0;
	int chg_status = 0, chg_fault = 0;
	u8 reg_val = 0;
	union power_supply_propval val = {0};

	if (!work) {
		pr_err("%s: Invalid param, fatal error\n", __func__);
		return -EINVAL;
	}

	bq = container_of(work, struct bq2415x_device, lower_power_charger_work.work);
	if (!bq) {
		pr_err("%s: Cannot get bq2415x device, fatal error\n", __func__);
		return;
	}

	/* exit when usb absent*/
	usb_present = bq->usb_present;
	if(!usb_present) {
		rc = bq2415x_exec_command(bq, BQ2415X_HIGH_IMPEDANCE_DISABLE);
		if (rc < 0) {
			pr_info("disalbe hiz mode fail\n");
		}
		return;
	}

	bq2415x_i2c_write(bq, BQ2415X_REG_SPECIAL, BQ2415X_SPECIAL_CONTROL);

	pr_info("usb current is %d\n",bq->iusb_limit);
	usb_ma = min(bq->iusb_limit, user_in_curr_limit);
	rc = bq2415x_set_current_limit(bq, usb_ma);
	if(rc < 0)
	{
		pr_info("current limit setting failed\n");
		return;
	}
	pr_info("charge_current = %d\n",usb_ma);
	if(!recharge_cv_lock){
		vbat = min(CHG_VOLT_DEFAULT, user_set_charge_voltage_mv);
		rc = bq2415x_set_battery_regulation_voltage(bq, vbat);
		if(rc < 0)
		{
			pr_info("current limit setting failed\n");
			return;
		}
	}

	usb_ma = min(MAX_CHARGE_CURRENT, user_set_charge_current_ma);
	/*Set output current for DCP charger*/
	rc = bq2415x_set_charge_current(bq, usb_ma);
	if(rc < 0){
		pr_err("set charge current failed\n");
		return;
	}
	rc= bq2415x_get_charge_current(bq);
	
	pr_err("get charge current %d\n",rc);
		/* enable charging otherwise high/cold temperature or high impedance */
		if((!bq->charge_disable)
			&& (!bq2415x_exec_command(bq, BQ2415X_HIGH_IMPEDANCE_STATUS))){
			rc = bq2415x_exec_command(bq, BQ2415X_CHARGER_ENABLE);
			if(rc < 0){
				pr_info("charge enable set failed\n");
				return;
			}
		}
	/* disable stat pin for turn off the led */
	rc = bq2415x_exec_command(bq, BQ2415X_STAT_PIN_DISABLE);
	if(rc < 0)
	{
		pr_info("disable pin failed\n");
		return;
	}

	power_supply_changed(&bq->batt_psy);
}

/**** power supply interface code ****/
static enum power_supply_property bq2415x_power_supply_props[] = {
	/* TODO: maybe add more power supply properties */
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_CHARGING_ENABLED,
	POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_CHARGE_TYPE,
    POWER_SUPPLY_PROP_VOLTAGE_MAX,
    POWER_SUPPLY_PROP_CURRENT_MAX,
    POWER_SUPPLY_PROP_CHARGE_COUNTER,
    POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN,
    POWER_SUPPLY_PROP_CYCLE_COUNT,
    POWER_SUPPLY_PROP_CHG_ITERM,
    POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX,
};

static int bq2415x_power_supply_get_property(struct power_supply *psy,
						 enum power_supply_property psp,
						 union power_supply_propval *val)
{
	struct bq2415x_device *bq = NULL;
	int ret = 0;

	if (!psy || !val) {
		pr_err("%s: Invalid param, fatal error\n", __func__);
		return -EINVAL;
	}

	bq = container_of(psy, struct bq2415x_device, batt_psy);
	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		bq2415x_update_charge_status(bq);
		val->intval = g_chg_status;
		pr_info("get g_chg_status = %d\n", g_chg_status);
		break;

	case POWER_SUPPLY_PROP_HEALTH:
		val->intval = bq2415x_get_battery_health(bq);
		break;
	case POWER_SUPPLY_PROP_FACTORY_DIAG:
		val->intval = !(factory_diag_flag);
		break;
	case POWER_SUPPLY_PROP_BATTERY_CHARGING_ENABLED:
		val->intval = !(bq->charge_disable);
		break;
	case POWER_SUPPLY_PROP_CHARGING_ENABLED:
		ret = bq2415x_exec_command(bq, BQ2415X_HIGH_IMPEDANCE_STATUS);
		if (ret < 0) {
			pr_err("%s: get charging enabled faield\n", __func__);
			break;
		}
		val->intval = !ret;
		ret = 0;
		break;
	case POWER_SUPPLY_PROP_HIZ_MODE:
		ret = bq2415x_exec_command(bq, BQ2415X_HIGH_IMPEDANCE_STATUS);
		if (ret < 0) {
			pr_err("%s: get hiz mode faield\n", __func__);
			break;
		}
		val->intval = ret;
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN:
			val->intval = CHG_FULL_DEFAULT;
		break;
	case POWER_SUPPLY_PROP_CHARGE_TYPE:
		val->intval = bq2415x_get_charge_type(bq);
		break;
	case POWER_SUPPLY_PROP_TEMP:
		val->intval = bq2415x_get_battery_temp(bq);
		break;
	case POWER_SUPPLY_PROP_INPUT_CURRENT_MAX:
		val->intval = user_in_curr_limit;
		break;
	case POWER_SUPPLY_PROP_INPUT_CURRENT_NOW:
		ret = bq2415x_exec_command(bq, BQ2415X_HIGH_IMPEDANCE_STATUS);
		if (ret < 0) {
			pr_err("%s: get hiz mode faield\n", __func__);
			val->intval = user_in_curr_limit;
			ret = 0;
			break;
		}
		if (ret) {
			val->intval = 0;
		} else {
			val->intval = user_in_curr_limit;
		}
		ret = 0;
		break;
	case POWER_SUPPLY_PROP_ALLOW_HVDCP:
		val->intval = 0;
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
	case POWER_SUPPLY_PROP_PRESENT:
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
	case POWER_SUPPLY_PROP_CURRENT_NOW:
	case POWER_SUPPLY_PROP_CAPACITY_LEVEL:
	case POWER_SUPPLY_PROP_CHARGE_FULL:
	case POWER_SUPPLY_PROP_TECHNOLOGY:
	case POWER_SUPPLY_PROP_RESISTANCE_ID:
	case POWER_SUPPLY_PROP_CHARGE_COUNTER:
		return bq2415x_get_batt_property(bq, psp, val);
    case POWER_SUPPLY_PROP_CYCLE_COUNT:
        return bq2415x_get_batt_property(bq,
                    POWER_SUPPLY_PROP_CHG_CYCLE_COUNT, val);
	case POWER_SUPPLY_PROP_CURRENT_MAX:
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX:
		val->intval = user_set_charge_current_ma * MA_TO_UA;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		val->intval = user_set_charge_voltage_mv * MV_TO_UV;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN:
		val->intval = CHG_VOLT_MAX * MV_TO_UV;
		break;
	case POWER_SUPPLY_PROP_CHG_ITERM:
		val->intval = bq2415x_get_termination_current(bq);
		break;
	case POWER_SUPPLY_PROP_SAFETY_TIMER_ENABLE:
		val->intval = 0;
		break;
	case POWER_SUPPLY_PROP_CHARGER_PRESENT:
		val->intval = bq->usb_present;
		break;
	default:
		ret = -EINVAL;
		break;
	}
	return ret;
}

static int bq2415x_charging_wake_lock_check(struct bq2415x_device *bq)
{
    int batt_real_full = false;
    int usb_present = false;

    if ( NULL == bq )
    {
        pr_err("%s: Invalid param, fatal error\n", __func__);
        return -EINVAL;
    }

    batt_real_full = bq2415x_get_battery_real_full_status(bq);
    usb_present = bq2415x_get_usb_present_status(bq);

    if (true == usb_present)
    {
        if ( (true == batt_real_full) \
            && (bq->charge_disable) )
        {
            if ( wake_lock_active(&bq->charging_wake_lock) )
            {
                pr_info("Charge full done, releasing charging wakelock\n");
                wake_unlock(&bq->charging_wake_lock);
            }
        }
        else
        {
            if ( !wake_lock_active(&bq->charging_wake_lock) )
            {
                pr_info("Usb present and not charge full, holding charging wakelock\n");
                wake_lock(&bq->charging_wake_lock);
            }
        }
    }
    else
    {
        if ( wake_lock_active(&bq->charging_wake_lock) )
        {
            pr_info("Usb absent, releasing charging wakelock\n");
            wake_unlock(&bq->charging_wake_lock);
        }
    }
   return 0;
}

static int bq2415x_charging_control(struct bq2415x_device *bq)
{
	int rc = 0;

	if ( NULL == bq ) {
		pr_err("%s: Invalid param, fatal error\n", __func__);
		return -EINVAL;
	}

	if(bq->charge_disable) {
		rc = bq2415x_exec_command(bq, BQ2415X_CHARGER_DISABLE);
	} else {
		rc = bq2415x_exec_command(bq, BQ2415X_CHARGER_ENABLE);
	}

	bq2415x_charging_wake_lock_check(bq);

	pr_info("set charge_disable=%d\n",bq->charge_disable);
	return rc;
}

#define CW_CHG_RECHARGE_DELAY        16000 //(jiffies + msecs_to_jiffies(16000))
#define CW_RECHARGE_TIME_COUNT       2
#define CW_RECHARGE_STEP_VOL_MV      20
int bq2415x_charging_recharge(struct bq2415x_device *bq)
{
	static int target_cv = 0; //now get target cv from register, I think I can get it from other better location
	int charge_voltage = 0;
	int ret = 0;
	int time = 0;
	
	pr_err("bq2415x_charging_recharge for test time = %d\n", bq->recharge_time);
	pr_err("bq2415x_charging_recharge for test start_cv = %d\n", bq2415x_get_battery_regulation_voltage(bq));
    time = bq->recharge_time;
	if(time == 0){
		target_cv = min(CHG_VOLT_DEFAULT, user_set_charge_voltage_mv);
		if(target_cv < 0){
			pr_err("bq2415x_get_battery_regulation_voltage error\n");
			return target_cv;
		}
	}else if(time > CW_RECHARGE_TIME_COUNT){
		pr_err("bq2415x_get_battery_regulation_voltage time count error\n");
		return -1;
	}else{
		;
	}
	
	charge_voltage = target_cv - ((CW_RECHARGE_TIME_COUNT - time) * CW_RECHARGE_STEP_VOL_MV);	
	ret = bq2415x_set_battery_regulation_voltage(bq, charge_voltage);
	if(ret < 0)
	{
		pr_err("bq2415x_set_battery_regulation_voltage failed\n");
		return ret;
	}
	if(time == 0){
		bq2415x_charging_control(bq);
	}
	if(time < CW_RECHARGE_TIME_COUNT){
		//mod_timer(&bq->recharge_timer_list, CW_CHG_RECHARGE_DELAY);
		pr_err("bq2415x_charging_recharge schedule_delayed_work\n");
		schedule_delayed_work(&bq->recharge_timer_work, msecs_to_jiffies(CW_CHG_RECHARGE_DELAY));
	}else{
		if( target_cv !=  bq2415x_get_battery_regulation_voltage(bq)){
			ret = bq2415x_set_battery_regulation_voltage(bq, target_cv);
			if(ret < 0)
			{
				pr_err("bq2415x_set_battery_regulation_voltage failed\n");
				return ret;
			}
		}
		target_cv = 0;
		bq->recharge_time = 0;
		recharge_cv_lock = 0;
	}
	pr_err("bq2415x_charging_recharge for test end_cv = %d\n", bq2415x_get_battery_regulation_voltage(bq));
	return 0;
}

static void bq2415x_charging_recharge_func(struct work_struct *work)
{
	struct bq2415x_device *bq = NULL;
	bq = container_of(work, struct bq2415x_device, recharge_timer_work.work);
	bq->recharge_time = bq->recharge_time + 1;
	pr_err("bq2415x_charging_recharge_func bq2415x_charging_recharge_func\n");
	bq2415x_charging_recharge(bq);
}

static int gauge_version_id = -1;
static int get_gauge_version_id()
{
	struct power_supply *bms_psy;
	union power_supply_propval val = {0, };
	int ret = 0;
	bms_psy = power_supply_get_by_name("bms");
    if (!bms_psy) {
        pr_err("%s: Cannot get bms power supply\n", __func__);
		return -1;
    }else{
		ret = bms_psy->get_property(bms_psy,
                    POWER_SUPPLY_PROP_PROFILE_ID, &val);
        if (ret < 0)
        {
            pr_err("%s: Get property from bms power supply fail\n", __func__);
			return -1;
        }
		pr_err("Chaman %d\n", val.intval);
		return val.intval;
	}	
	return -1;
}

static int bq2415x_power_supply_set_property(struct power_supply *psy,
				enum power_supply_property psp,
				const union power_supply_propval *val)
{
	struct bq2415x_device *bq = NULL;
	int rc = 0;

	if (!psy || !val) {
		pr_err("%s: Invalid param, fatal error\n", __func__);
		return -EINVAL;
	}

	bq = container_of(psy, struct bq2415x_device, batt_psy);
	switch (psp) {
	case POWER_SUPPLY_PROP_BATTERY_CHARGING_ENABLED:
		pr_err("recharge val->intval = %d\n",val->intval);
		if(gauge_version_id == -1){
			gauge_version_id = get_gauge_version_id();
		}
		pr_err("recharge gauge_version_id = %d\n", gauge_version_id);
		if((gauge_version_id == GAUGE_OLD_VERSION_ID) && (val->intval == TRUE) \
				&& (bq2415x_get_battery_ui_soc(bq) == FULL_CAPACITY) && (recharge_cv_lock == 0)){
			pr_info("recharge set battery charging_enabled value is %d\n",val->intval);
			recharge_cv_lock = 1;
			bq->charge_disable = !(val->intval);
			bq->recharge_time = 0;
			cancel_delayed_work_sync(&bq->recharge_timer_work);
			bq2415x_charging_recharge(bq);
		}else{
			bq->charge_disable = !(val->intval);
			pr_info("set battery charging_enabled value is %d\n",val->intval);
			bq2415x_charging_control(bq);
		}
		break;
	case POWER_SUPPLY_PROP_CHARGING_ENABLED:
		pr_info("set charging_enabled value is %d\n",val->intval);
		if (val->intval) {
			bq2415x_exec_command(bq, BQ2415X_HIGH_IMPEDANCE_DISABLE);
		} else {
			bq2415x_exec_command(bq, BQ2415X_HIGH_IMPEDANCE_ENABLE);
		}
		break;
	case POWER_SUPPLY_PROP_HIZ_MODE:
		pr_info("set hiz charging_enabled value is %d\n",val->intval);
		if (!val->intval) {
			bq2415x_exec_command(bq, BQ2415X_HIGH_IMPEDANCE_DISABLE);
		} else {
			bq2415x_exec_command(bq, BQ2415X_HIGH_IMPEDANCE_ENABLE);
		}
		break;
	case POWER_SUPPLY_PROP_FACTORY_DIAG:
		bq2415x_factory_diag(bq,val->intval);
		break;
	case POWER_SUPPLY_PROP_INPUT_CURRENT_MAX:
		if (val->intval <= 0) {
			user_in_curr_limit = CURRENT_LIMIT_1800MA;
		} else {
			user_in_curr_limit = val->intval;
		}
		set_charging_by_current_limit(bq);
		break;
	case POWER_SUPPLY_PROP_CURRENT_MAX:
	case POWER_SUPPLY_PROP_STATUS:
	case POWER_SUPPLY_PROP_COOL_TEMP:
	case POWER_SUPPLY_PROP_WARM_TEMP:
	case POWER_SUPPLY_PROP_VOLTAGE_MIN:
	case POWER_SUPPLY_PROP_SYSTEM_TEMP_LEVEL:
		pr_info("bq24157 no need to set this prop: %d\n",psp);
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX:
		user_set_charge_current_ma = (val->intval)/MA_TO_UA;/* Makesure in mA */
		set_charging_by_current_limit(bq);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		user_set_charge_voltage_mv = val->intval;
		set_charging_by_current_limit(bq);
		break;
	case POWER_SUPPLY_PROP_SAFETY_TIMER_ENABLE:
		break;
    case POWER_SUPPLY_PROP_CHG_ITERM:
        user_set_term_current_ma = val->intval;
        rc = bq2415x_set_termination_current(bq,
                            user_set_term_current_ma);
        break;
	default:
		rc = -EINVAL;
		break;
	}
	return rc;
}

static int bq2415x_property_is_writeable(struct power_supply *psy,
					enum power_supply_property psp)
{
	if (!psy) {
		pr_err("%s: Invalid param, fatal error\n", __func__);
		return 0;
	}

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
	case POWER_SUPPLY_PROP_CHARGING_ENABLED:
	case POWER_SUPPLY_PROP_COOL_TEMP:
	case POWER_SUPPLY_PROP_VOLTAGE_MIN:
	case POWER_SUPPLY_PROP_WARM_TEMP:
	case POWER_SUPPLY_PROP_SYSTEM_TEMP_LEVEL:
	case POWER_SUPPLY_PROP_CURRENT_MAX:

		return 1;
	default:
		break;
	}

	return 0;
}

static irqreturn_t bq2415x_charger_interrupt(int irq, void *dev_id)
{
	struct bq2415x_device *bq = NULL;
	u8 status = 0;
	int ret = 0;
	int no_present = 0;

	pr_err("recharge look lock recharge_cv_lock = %d\n", recharge_cv_lock);
	if(recharge_cv_lock == 1){
		//pr_err("recharge look lock recharge_cv_lock = %d\n", recharge_cv_lock);
		recharge_cv_lock = 0;
	}
	if (!dev_id) {
		pr_err("%s:invalid param, fatal error\n", __func__);
		return IRQ_HANDLED;
	}

	bq = dev_id;
	mutex_lock(&bq->irq_complete);
	bq->irq_waiting = true;
	if (!bq->resume_completed) {
		dev_dbg(bq->dev, "IRQ triggered before device-resume\n");
		if (!bq->irq_disabled) {
			disable_irq_nosync(irq);
			bq->irq_disabled = true;
		}
		mutex_unlock(&bq->irq_complete);
		return IRQ_HANDLED;
	}
	bq->irq_waiting = false;
	mutex_lock(&bq->data_lock);
	no_present = gpio_get_value(bq->gpio_int);

	/*if usb no present, further to get otg present status*/
	if (no_present)
		no_present = !bq2415x_get_otg_present(bq);

	mutex_unlock(&bq->data_lock);

	if(bq->usb_present&&no_present) {
		factory_diag_flag = 0;
		bq->usb_present = false;
		power_supply_set_present(bq->usb_psy, bq->usb_present);
		pr_err("usb removed, set usb present = %d\n", bq->usb_present);
	}
	else if (!bq->usb_present&&(!no_present)) {
		bq->usb_present = true;
		msleep(CDP_DETECT_DELAY);//for cdp detect
		power_supply_set_present(bq->usb_psy, bq->usb_present);	
		pr_err("usb plugged in, set usb present = %d\n", bq->usb_present);
	}
	mutex_unlock(&bq->irq_complete);
	power_supply_changed(&bq->batt_psy);
	bq2415x_charging_wake_lock_check(bq);
	return IRQ_HANDLED;
}


static void determine_initial_status(struct bq2415x_device *bq)
{
	if (!bq) {
		pr_err("%s: Invalid param, fatal error\n", __func__);
		return;
	}
	bq2415x_charger_interrupt(bq->client->irq, bq);
}

static void set_charging_by_current_limit(struct bq2415x_device *bq)
{
	if (!bq) {
		pr_err("%s: Invalid param, fatal error\n", __func__);
		return;
	}
	if(bq->iusb_limit <= USB_CURRENT_LIMIT){
			schedule_work(&bq->iusb_work);
	}else{
			schedule_delayed_work(&bq->lower_power_charger_work, 0);
	}
}
static void bq2415x_external_power_changed(struct power_supply *psy)
{
	struct bq2415x_device *bq = NULL;
	unsigned long flags = 0;
	int current_ma = 0;
	union power_supply_propval ret = {0,};

	if(!psy)
	{
	  pr_info("bq2415x_external_power_changed :psy is NULL\n");
	  return ;
	}

	bq = container_of(psy, struct bq2415x_device,batt_psy);
	if((!bq)|| (!bq->usb_psy))
	{
	  pr_info("bq is NULL\n");
	  return ;
	}
	
	bq->usb_psy->get_property(bq->usb_psy,
							  POWER_SUPPLY_PROP_CURRENT_MAX, &ret);
	current_ma = ret.intval / MA_TO_UA;

	if ( (true == bq->usb_present) && (0 == factory_diag_flag) \
		&& (current_ma <= USB_CURRENT_LIMIT_500MA) )
	{
		pr_info("get current_max is %dmA, enforce set to 500mA\n", current_ma);
		current_ma = USB_CURRENT_LIMIT_500MA;
	}
	
	pr_info("get iusb_limit0 :%d current_ma %d\n",bq->iusb_limit,current_ma);
		//current_ma = min(current_ma, hot_current);
	if(current_ma != bq->iusb_limit)
	{
		mutex_lock(&bq->current_change_lock);
        if(current_ma == CURRENT_LIMIT_2000MA)
        {
            current_ma = MAX_CHARGE_CURRENT;
        }
		bq->iusb_limit = current_ma;
		mutex_unlock(&bq->current_change_lock);
		pr_info("get iusb_limit :%d\n",bq->iusb_limit);
		set_charging_by_current_limit(bq);
	}
}
#define FAC_DIAG_CURRENT     90000
static int bq2415x_factory_diag(struct bq2415x_device *bq,int val)
{
    union power_supply_propval val_factory_diag = {0,};

	if (!bq) {
		pr_err("%s: Invalid para, fatal error\n", __func__);
		return -EINVAL;
	}

    factory_diag_flag = !val;
    /* if set discharging when PT, set 90mA, as lbc have no BATFET*/
    if (factory_diag_flag) {
        bq->usb_psy->get_property(bq->usb_psy,
               POWER_SUPPLY_PROP_CURRENT_MAX, &val_factory_diag);
        factory_diag_last_current_ma = val_factory_diag.intval;
        val_factory_diag.intval = FAC_DIAG_CURRENT;
        bq->usb_psy->set_property(bq->usb_psy,
        POWER_SUPPLY_PROP_CURRENT_MAX, &val_factory_diag);
        pr_info("set factory diag to %d\n",val_factory_diag.intval);
    } else {
        if (factory_diag_last_current_ma) {
            val_factory_diag.intval = factory_diag_last_current_ma;
            bq->usb_psy->set_property(bq->usb_psy,
                  POWER_SUPPLY_PROP_CURRENT_MAX, &val_factory_diag);
            pr_info("set factory diag recovery %d\n",val_factory_diag.intval);
        }
        factory_diag_last_current_ma = 0;
    }
	bq2415x_external_power_changed(&bq->batt_psy);

    return 0;
}

static char *bq2415x_supplied_to[] = {
	 "bms"
};

#define BQ_REVISION_LEN 8
static int bq2415x_power_supply_init(struct bq2415x_device *bq)
{
	int ret = 0;
	int chip = 0;
	char revstr[BQ_REVISION_LEN] = {0, };

	if (!bq) {
		pr_err("%s: invalid param, fatal error\n", __func__);
		return -EINVAL;
	}

	bq->batt_psy.name = "bk_battery";
	bq->batt_psy.type = POWER_SUPPLY_TYPE_UNKNOWN;
	bq->batt_psy.properties = bq2415x_power_supply_props;
	bq->batt_psy.num_properties = ARRAY_SIZE(bq2415x_power_supply_props);
	bq->batt_psy.get_property = bq2415x_power_supply_get_property;
	bq->batt_psy.set_property = bq2415x_power_supply_set_property;
	bq->batt_psy.property_is_writeable = bq2415x_property_is_writeable;
	bq->batt_psy.external_power_changed = bq2415x_external_power_changed;
	bq->batt_psy.supplied_to = bq2415x_supplied_to;
	bq->batt_psy.num_supplicants =ARRAY_SIZE(bq2415x_supplied_to);

	ret = bq2415x_detect_chip(bq);
	if (ret < 0)
		chip = BQUNKNOWN;
	else
		chip = ret;
 
	ret = bq2415x_detect_revision(bq);
	if (ret < 0)
		strncpy(revstr, "unknown",sizeof revstr);
	else
		snprintf(revstr,sizeof revstr, "1.%d", ret);
	bq->model = kasprintf(GFP_KERNEL,
				"chip %s, revision %s, vender code %.3d",
				bq2415x_chip_name[chip], revstr,
				bq2415x_get_vender_code(bq));
	if (!bq->model) {
		dev_err(bq->dev, "failed to allocate model name\n");
		return -ENOMEM;
	}
	ret = power_supply_register(bq->dev, &bq->batt_psy);
	if (ret) {
		kfree(bq->model);
		return ret;
	}
	return 0;
}
static int is_otg_enable(struct bq2415x_device *bq)
{
	int enabled = 0;
	if(NULL == bq) {	
		pr_err("is_otg_enable: bq is NULL\n");
		return -EINVAL;
	}
	enabled = bq2415x_exec_command(bq, BQ2415X_BOOST_STATUS);
	return (enabled == 1) ? 1 : 0;

}
static int bq2415x_otg_regulator_is_enable(struct regulator_dev *rdev)
{
	int enabled = 0;
	struct bq2415x_device *bq = NULL;

    if (!rdev) {
        pr_err("%s: invalid param, fatal error\n", __func__);
        return -EINVAL;
    }

    bq = rdev_get_drvdata(rdev);
	if(NULL == bq) {	
		pr_err("bq2415x_otg_regulator_is_enable: bq is NULL\n");
		return -EINVAL;
	}
	enabled = is_otg_enable(bq);
	return enabled;

}

static int bq24157_control_otg(struct bq2415x_device *bq,bool enable)
{
	int ret = 0;
	int mode = 0;

	if(NULL == bq) {	
		pr_err("bq24157_control_otg: bq is NULL\n");
		return 0;
	}	  
	
	mode = is_otg_enable(bq);
	if(enable){
		pr_info("bq2415x_set_mode: boost mode, enable=%d\n", enable);
		g_boost_mode_enable_flag = true;
		if(1 == mode)
			return 0;
		ret = bq2415x_set_mode(bq, BQ2415X_MODE_BOOST);
		if(ret < 0)
			return ret;
	}else{
		pr_info("bq2415x_set_mode: normal mode, enable=%d\n", enable);
		g_boost_mode_enable_flag = false;
		if(0 == mode)
			return 0;
		ret = bq2415x_set_mode(bq, BQ2415X_MODE_OFF);
		if(ret < 0)
			return ret;
	}
}

static int bq2415x_otg_regulator_enable(struct regulator_dev *rdev)
{
	int ret = 0;
	struct bq2415x_device *bq = NULL;;

	if(!rdev) {
		pr_err("%s: Invalid param, fatal error\n", __func__);
		return -EINVAL;
	} 

	bq = rdev_get_drvdata(rdev);
	if(NULL == bq) {
		pr_err("bq2415x_otg_regulator_enable: bq is NULL\n");
		return -EINVAL;
	} 

	ret = bq24157_control_otg(bq,true);
	if (ret) {
		pr_err("Couldn't enable OTG mode ret=%d\n", ret);
	} else {
		pr_info("bq2415x OTG mode Enabled!\n");
	}
	
	return ret;
}


static int bq2415x_otg_regulator_disable(struct regulator_dev *rdev)
{
	int ret = 0;
	struct bq2415x_device *bq = NULL;

	if (!rdev) {
		pr_err("%s: Invalid param, fatal error\n", __func__);
		return -EINVAL;
	}

	bq = rdev_get_drvdata(rdev);
	if(NULL == bq) {
		pr_err("bq2415x_otg_regulator_disable: bq is NULL\n");
		return -EINVAL;
	} 

	ret = bq24157_control_otg(bq,false);
	if (ret) {
		pr_err("Couldn't disable OTG mode, ret=%d\n", ret);
	} else {
		pr_info("bq2415x OTG mode Disabled\n");
	}
	
	return ret;
}


struct regulator_ops bq2415x_otg_reg_ops = {
	.enable		= bq2415x_otg_regulator_enable,
	.disable	= bq2415x_otg_regulator_disable,
	.is_enabled = bq2415x_otg_regulator_is_enable,
};

static int bq2415x_regulator_init(struct bq2415x_device *bq)
{
	int ret = 0;
	struct regulator_init_data *init_data = NULL;
	struct regulator_config cfg = {};

	if(NULL == bq) {
		pr_err("bq2415x_regulator_init: bq is NULL\n");
		return -EINVAL;
	} 

	init_data = of_get_regulator_init_data(bq->dev, bq->dev->of_node);
	if (!init_data) {
		dev_err(bq->dev, "Unable to allocate memory\n");
		return -ENOMEM;
	}

	if (init_data->constraints.name) {
		bq->otg_vreg.rdesc.owner = THIS_MODULE;
		bq->otg_vreg.rdesc.type = REGULATOR_VOLTAGE;
		bq->otg_vreg.rdesc.ops = &bq2415x_otg_reg_ops;
		bq->otg_vreg.rdesc.name = init_data->constraints.name;
		pr_info("regualtor name = %s\n", bq->otg_vreg.rdesc.name);

		cfg.dev = bq->dev;
		cfg.init_data = init_data;
		cfg.driver_data = bq;
		cfg.of_node = bq->dev->of_node;

		init_data->constraints.valid_ops_mask
			|= REGULATOR_CHANGE_STATUS;

		bq->otg_vreg.rdev = regulator_register(
					&bq->otg_vreg.rdesc, &cfg);
		if (IS_ERR(bq->otg_vreg.rdev)) {
			ret = PTR_ERR(bq->otg_vreg.rdev);
			bq->otg_vreg.rdev = NULL;
			if (ret != -EPROBE_DEFER)
				dev_err(bq->dev,
					"OTG reg failed, rc=%d\n", ret);
		}
	}

	return ret;
}

static void bq2415x_power_supply_exit(struct bq2415x_device *bq)
{
	if(NULL == bq) {
		pr_err("bq2415x_power_supply_exit: bq is NULL\n");
		return -EINVAL;
	} 

	bq->autotimer = 0;
	if (bq->automode > 0)
		bq->automode = 0;
	cancel_delayed_work_sync(&bq->work);
	power_supply_unregister(&bq->batt_psy);
	kfree(bq->model);
}

/**** additional sysfs entries for power supply interface ****/

/* show *_status entries */
static ssize_t bq2415x_sysfs_show_status(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	struct power_supply *psy = NULL;
	struct bq2415x_device *bq = NULL;
	enum bq2415x_command command = 0;
	int ret = 0;

	if((NULL == dev) ||(NULL == attr) ||(NULL == buf)) {
		pr_err("bq2415x_sysfs_show_status: input param is NULL\n");
		return -EINVAL;
	} 

	psy = dev_get_drvdata(dev);
	bq = container_of(psy, struct bq2415x_device, batt_psy);
	if(NULL == bq) {
		pr_err("bq2415x_sysfs_show_status: bq is NULL\n");
		return -EINVAL;
	} 

	if (strcmp(attr->attr.name, "otg_status") == 0)
		command = BQ2415X_OTG_STATUS;
	else if (strcmp(attr->attr.name, "charge_status") == 0)
		command = BQ2415X_CHARGE_STATUS;
	else if (strcmp(attr->attr.name, "boost_status") == 0)
		command = BQ2415X_BOOST_STATUS;
	else if (strcmp(attr->attr.name, "fault_status") == 0)
		command = BQ2415X_FAULT_STATUS;
	else
		return -EINVAL;

	ret = bq2415x_exec_command(bq, command);
	if (ret < 0)
		return ret;
	return snprintf(buf, PAGE_SIZE,"%d\n", ret);
}

/*
 * set timer entry:
 *	  auto - enable auto mode
 *	  off - disable auto mode
 *	  (other values) - reset chip timer
 */
static ssize_t bq2415x_sysfs_set_timer(struct device *dev,
					   struct device_attribute *attr,
					   const char *buf,
					   size_t count)
{
	struct power_supply *psy = NULL;
	struct bq2415x_device *bq = NULL;
	int ret = 0;

	if((NULL == dev) ||(NULL == attr) ||(NULL == buf)) {
		pr_err("bq2415x_sysfs_set_timer: input is NULL\n");
		return -EINVAL;
	} 

	psy = dev_get_drvdata(dev);
	bq = container_of(psy, struct bq2415x_device, batt_psy);
	if(NULL == bq) {
		pr_err("bq2415x_sysfs_set_timer: bq is NULL\n");
		return -EINVAL;
	} 

	if (strncmp(buf, "auto", 4) == 0)
		bq2415x_set_autotimer(bq, 1);
	else if (strncmp(buf, "off", 3) == 0)
		bq2415x_set_autotimer(bq, 0);
	else
		ret = bq2415x_exec_command(bq, BQ2415X_TIMER_RESET);

	if (ret < 0)
		return ret;
	return count;
}

/* show timer entry (auto or off) */
static ssize_t bq2415x_sysfs_show_timer(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct power_supply *psy = NULL;
	struct bq2415x_device *bq = NULL;

	if((NULL == dev) ||(NULL == attr) ||(NULL == buf)) {
		pr_err("bq2415x_sysfs_show_timer: input is NULL\n");
		return -EINVAL;
	} 

	psy = dev_get_drvdata(dev);
	bq = container_of(psy, struct bq2415x_device, batt_psy);
	if(NULL == bq) {
		pr_err("bq2415x_sysfs_show_timer: bq is NULL\n");
		return -EINVAL;
	}

	if (bq->timer_error)
		return snprintf(buf, PAGE_SIZE,"%s\n", bq->timer_error);

	if (bq->autotimer)
		return snprintf(buf, PAGE_SIZE,"auto\n");
	return snprintf(buf, PAGE_SIZE,"off\n");
}

/*
 * set mode entry:
 *	  auto - if automode is supported, enable it and set mode to reported
 *	  none - disable charger and boost mode
 *	  host - charging mode for host/hub chargers (current limit 500mA)
 *	  dedicated - charging mode for dedicated chargers (unlimited current limit)
 *	  boost - disable charger and enable boost mode
 */
static ssize_t bq2415x_sysfs_set_mode(struct device *dev,
					  struct device_attribute *attr,
					  const char *buf,
					  size_t count)
{
	struct power_supply *psy = NULL;
	struct bq2415x_device *bq = NULL;
	enum bq2415x_mode mode = 0;
	int ret = 0;

	if((NULL == dev) ||(NULL == attr) ||(NULL == buf)) {
		pr_err("bq2415x_sysfs_set_mode: input is NULL\n");
		return -EINVAL;
	} 

	psy = dev_get_drvdata(dev);
	bq = container_of(psy, struct bq2415x_device, batt_psy);
	if(NULL == bq) {
		pr_err("bq2415x_sysfs_set_mode: bq is NULL\n");
		return -EINVAL;
	}

	if (strncmp(buf, "auto", 4) == 0) {
		if (bq->automode < 0)
			return -ENOSYS;
		bq->automode = 1;
		mode = bq->reported_mode;
	} else if (strncmp(buf, "off", 3) == 0) {
		if (bq->automode > 0)
			bq->automode = 0;
		mode = BQ2415X_MODE_OFF;
	} else if (strncmp(buf, "none", 4) == 0) {
		if (bq->automode > 0)
			bq->automode = 0;
		mode = BQ2415X_MODE_NONE;
	} else if (strncmp(buf, "host", 4) == 0) {
		if (bq->automode > 0)
			bq->automode = 0;
		mode = BQ2415X_MODE_HOST_CHARGER;
	} else if (strncmp(buf, "dedicated", 9) == 0) {
		if (bq->automode > 0)
			bq->automode = 0;
		mode = BQ2415X_MODE_DEDICATED_CHARGER;
	} else if (strncmp(buf, "boost", 5) == 0) {
		if (bq->automode > 0)
			bq->automode = 0;
		mode = BQ2415X_MODE_BOOST;
	} else if (strncmp(buf, "reset", 5) == 0) {
		bq2415x_reset_chip(bq);
		bq2415x_set_defaults(bq);
		if (bq->automode <= 0)
			return count;
		bq->automode = 1;
		mode = bq->reported_mode;
	} else {
		return -EINVAL;
	}

	ret = bq2415x_set_mode(bq, mode);
	if (ret < 0)
		return ret;
	return count;
}

/* show mode entry (auto, none, host, dedicated or boost) */
static ssize_t bq2415x_sysfs_show_mode(struct device *dev,
					   struct device_attribute *attr,
					   char *buf)
{
	struct power_supply *psy = NULL;
	struct bq2415x_device *bq = NULL;
	ssize_t ret = 0;

	if((NULL == dev) ||(NULL == attr) ||(NULL == buf)) {
		pr_err("bq2415x_sysfs_show_mode: input is NULL\n");
		return -EINVAL;
	} 

	psy = dev_get_drvdata(dev);
	bq = container_of(psy, struct bq2415x_device, batt_psy);
	if(NULL == bq) {
		pr_err("bq2415x_sysfs_show_mode: bq is NULL\n");
		return -EINVAL;
	}

	if (bq->automode > 0)
		ret += snprintf(buf+ret, PAGE_SIZE, "auto (");

	switch (bq->mode) {
	case BQ2415X_MODE_OFF:
		ret += snprintf(buf+ret, PAGE_SIZE, "off");
		break;
	case BQ2415X_MODE_NONE:
		ret += snprintf(buf+ret, PAGE_SIZE, "none");
		break;
	case BQ2415X_MODE_HOST_CHARGER:
		ret += snprintf(buf+ret, PAGE_SIZE, "host");
		break;
	case BQ2415X_MODE_DEDICATED_CHARGER:
		ret += snprintf(buf+ret, PAGE_SIZE, "dedicated");
		break;
	case BQ2415X_MODE_BOOST:
		ret += snprintf(buf+ret, PAGE_SIZE, "boost");
		break;
	}

	if (bq->automode > 0)
		ret += snprintf(buf+ret, PAGE_SIZE, ")");

	ret += snprintf(buf+ret, PAGE_SIZE,"\n");
	return ret;
}

/* show reported_mode entry (none, host, dedicated or boost) */
static ssize_t bq2415x_sysfs_show_reported_mode(struct device *dev,
						struct device_attribute *attr,
						char *buf)
{
	struct power_supply *psy = NULL;
	struct bq2415x_device *bq = NULL;

	if((NULL == dev) ||(NULL == attr) ||(NULL == buf)) {
		pr_err("bq2415x_sysfs_show_mode: input is NULL\n");
		return -EINVAL;
	} 

	psy = dev_get_drvdata(dev);
	bq = container_of(psy, struct bq2415x_device, batt_psy);

	if(NULL == bq) {
		pr_err("bq2415x_sysfs_show_reported_mode: bq is NULL\n");
		return -EINVAL;
	}

	if (bq->automode < 0)
		return -EINVAL;

	switch (bq->reported_mode) {
	case BQ2415X_MODE_OFF:
		return snprintf(buf, PAGE_SIZE,"off\n");
	case BQ2415X_MODE_NONE:
		return snprintf(buf, PAGE_SIZE,"none\n");
	case BQ2415X_MODE_HOST_CHARGER:
		return snprintf(buf, PAGE_SIZE,"host\n");
	case BQ2415X_MODE_DEDICATED_CHARGER:
		return snprintf(buf, PAGE_SIZE,"dedicated\n");
	case BQ2415X_MODE_BOOST:
		return snprintf(buf, PAGE_SIZE,"boost\n");
	}

	return -EINVAL;
}

/* directly set raw value to chip register, format: 'register value' */
static ssize_t bq2415x_sysfs_set_registers(struct device *dev,
					   struct device_attribute *attr,
					   const char *buf,
					   size_t count)
{
	struct power_supply *psy = NULL;
	struct bq2415x_device *bq = NULL;
	ssize_t ret = 0;
	unsigned int reg = 0;
	unsigned int val = 0;

	if((NULL == dev) ||(NULL == attr) ||(NULL == buf)) {
		pr_err("bq2415x_sysfs_set_registers: input is NULL\n");
		return -EINVAL;
	} 

	psy = dev_get_drvdata(dev);
	bq = container_of(psy, struct bq2415x_device, batt_psy);
	if(NULL == bq) {
		pr_err("bq2415x_sysfs_set_registers: bq is NULL\n");
		return -EINVAL;
	}

	if (sscanf(buf, "%x %x", &reg, &val) != 2)
		return -EINVAL;

	if (reg > REG_MAX || val > MAX_NUMBER_BYTE)
		return -EINVAL;

	ret = bq2415x_i2c_write(bq, reg, val);
	if (ret < 0)
		return ret;
	return count;
}

/* print value of chip register, format: 'register=value' */
static ssize_t bq2415x_sysfs_print_reg(struct bq2415x_device *bq,
					   u8 reg,
					   char *buf)
{
	int ret = 0;

	if((NULL == buf) ||(NULL == bq)) {
		pr_err("bq2415x_sysfs_set_registers: bq is NULL\n");
		return -EINVAL;
	} 

	ret = bq2415x_i2c_read(bq, reg);

	if (ret < 0)
		return snprintf(buf, PAGE_SIZE, "%#.2x=error %d\n", reg, ret);	
	return snprintf(buf, PAGE_SIZE, "%#.2x ", ret);
}

/* show all raw values of chip register, format per line: 'register=value' */
static ssize_t bq2415x_sysfs_show_registers(struct device *dev,
						struct device_attribute *attr,
						char *buf)
{
	struct power_supply *psy = NULL;
	struct bq2415x_device *bq = NULL;
	ssize_t ret = 0;

	if((NULL == dev) ||(NULL == attr) ||(NULL == buf)) {
		pr_err("bq2415x_sysfs_show_registers: input is NULL\n");
		return -EINVAL;
	} 

	psy = dev_get_drvdata(dev);
	bq = container_of(psy, struct bq2415x_device, batt_psy);
	if(NULL == bq) {
		pr_err("bq2415x_sysfs_show_registers: bq is NULL\n");
		return -EINVAL;
	} 

	ret += bq2415x_sysfs_print_reg(bq, BQ2415X_REG_STATUS, buf+ret);
	ret += bq2415x_sysfs_print_reg(bq, BQ2415X_REG_CONTROL, buf+ret);
	ret += bq2415x_sysfs_print_reg(bq, BQ2415X_REG_VOLTAGE, buf+ret);
	ret += bq2415x_sysfs_print_reg(bq, BQ2415X_REG_VENDER, buf+ret);
	ret += bq2415x_sysfs_print_reg(bq, BQ2415X_REG_CURRENT, buf+ret);
	ret += snprintf(buf+ret, PAGE_SIZE,"\n");
	return ret;
}

/* set current and voltage limit entries (in mA or mV) */
static ssize_t bq2415x_sysfs_set_limit(struct device *dev,
					   struct device_attribute *attr,
					   const char *buf,
					   size_t count)
{
	struct power_supply *psy = NULL;
	struct bq2415x_device *bq = NULL;
	long val = 0;
	int ret = 0;

	if((NULL == dev) ||(NULL == attr) ||(NULL == buf)) {
		pr_err("bq2415x_sysfs_set_limit: input is NULL\n");
		return -EINVAL;
	} 

	psy = dev_get_drvdata(dev);
	bq = container_of(psy, struct bq2415x_device, batt_psy);
	if(NULL == bq) {
		pr_err("bq2415x_sysfs_set_limit: bq is NULL\n");
		return -EINVAL;
	}

	if (kstrtol(buf, 10, &val) < 0)
		return -EINVAL;

	if (strcmp(attr->attr.name, "current_limit") == 0)
		ret = bq2415x_set_current_limit(bq, val);
	else if (strcmp(attr->attr.name, "weak_battery_voltage") == 0)
		ret = bq2415x_set_weak_battery_voltage(bq, val);
	else if (strcmp(attr->attr.name, "battery_regulation_voltage") == 0)
		ret = bq2415x_set_battery_regulation_voltage(bq, val);
	else if (strcmp(attr->attr.name, "charge_current") == 0)
		ret = bq2415x_set_charge_current(bq, val);
	else if (strcmp(attr->attr.name, "termination_current") == 0)
		ret = bq2415x_set_termination_current(bq, val);
	else
		return -EINVAL;

	if (ret < 0)
		return ret;
	return count;
}

/* show current and voltage limit entries (in mA or mV) */
static ssize_t bq2415x_sysfs_show_limit(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct power_supply *psy = NULL;
	struct bq2415x_device *bq = NULL;
	int ret = 0;

	if((NULL == dev) ||(NULL == attr) ||(NULL == buf)) {
		pr_err("bq2415x_sysfs_show_limit: input is NULL\n");
		return -EINVAL;
	} 

	psy = dev_get_drvdata(dev);
	bq = container_of(psy, struct bq2415x_device, batt_psy);
	if(NULL == bq) {
		pr_err("bq2415x_sysfs_show_limit: bq is NULL\n");
		return -EINVAL;
	}

	if (strcmp(attr->attr.name, "current_limit") == 0)
		ret = bq2415x_get_current_limit(bq);
	else if (strcmp(attr->attr.name, "weak_battery_voltage") == 0)
		ret = bq2415x_get_weak_battery_voltage(bq);
	else if (strcmp(attr->attr.name, "battery_regulation_voltage") == 0)
		ret = bq2415x_get_battery_regulation_voltage(bq);
	else if (strcmp(attr->attr.name, "charge_current") == 0)
		ret = bq2415x_get_charge_current(bq);
	else if (strcmp(attr->attr.name, "termination_current") == 0)
		ret = bq2415x_get_termination_current(bq);
	else
		return -EINVAL;

	if (ret < 0)
		return ret;
	return snprintf(buf, PAGE_SIZE,"%d\n", ret);
}

/* set *_enable entries */
static ssize_t bq2415x_sysfs_set_enable(struct device *dev,
					struct device_attribute *attr,
					const char *buf,
					size_t count)
{
	struct power_supply *psy = NULL;
	struct bq2415x_device *bq = NULL;
	enum bq2415x_command command = 0;
	long val = 0;
	int ret = 0;

	if((NULL == dev) ||(NULL == attr) ||(NULL == buf)) {
		pr_err("bq2415x_sysfs_set_enable: input is NULL\n");
		return -EINVAL;
	} 

	psy = dev_get_drvdata(dev);
	bq = container_of(psy, struct bq2415x_device, batt_psy);
	if(NULL == bq) {
		pr_err("bq2415x_sysfs_set_enable: bq is NULL\n");
		return -EINVAL;
	}

	if (kstrtol(buf, INPUT_NUMBER_BASE, &val) < 0)
		return -EINVAL;

	if (strcmp(attr->attr.name, "charge_termination_enable") == 0)
		command = val ? BQ2415X_CHARGE_TERMINATION_ENABLE :
			BQ2415X_CHARGE_TERMINATION_DISABLE;
	else if (strcmp(attr->attr.name, "high_impedance_enable") == 0)
		command = val ? BQ2415X_HIGH_IMPEDANCE_ENABLE :
			BQ2415X_HIGH_IMPEDANCE_DISABLE;
	else if (strcmp(attr->attr.name, "otg_pin_enable") == 0)
		command = val ? BQ2415X_OTG_PIN_ENABLE :
			BQ2415X_OTG_PIN_DISABLE;
	else if (strcmp(attr->attr.name, "stat_pin_enable") == 0)
		command = val ? BQ2415X_STAT_PIN_ENABLE :
			BQ2415X_STAT_PIN_DISABLE;
	else
		return -EINVAL;

	ret = bq2415x_exec_command(bq, command);
	if (ret < 0)
		return ret;
	return count;
}

/* show *_enable entries */
static ssize_t bq2415x_sysfs_show_enable(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	struct power_supply *psy = NULL;
	struct bq2415x_device *bq = NULL;
	enum bq2415x_command command = 0;
	int ret = 0;

	if((NULL == dev) ||(NULL == attr) ||(NULL == buf)) {
		pr_err("bq2415x_sysfs_show_enable: input is NULL\n");
		return -EINVAL;
	}

	psy = dev_get_drvdata(dev);
	bq = container_of(psy, struct bq2415x_device, batt_psy);

	if(NULL == bq) {
		pr_err("bq2415x_sysfs_show_enable: bq is NULL\n");
		return -EINVAL;
	}

	if (strcmp(attr->attr.name, "charge_termination_enable") == 0)
		command = BQ2415X_CHARGE_TERMINATION_STATUS;
	else if (strcmp(attr->attr.name, "high_impedance_enable") == 0)
		command = BQ2415X_HIGH_IMPEDANCE_STATUS;
	else if (strcmp(attr->attr.name, "otg_pin_enable") == 0)
		command = BQ2415X_OTG_PIN_STATUS;
	else if (strcmp(attr->attr.name, "stat_pin_enable") == 0)
		command = BQ2415X_STAT_PIN_STATUS;
	else
		return -EINVAL;

	ret = bq2415x_exec_command(bq, command);
	if (ret < 0)
		return ret;
	return snprintf(buf, PAGE_SIZE, "%d\n", ret);
}

static DEVICE_ATTR(current_limit, S_IWUSR | S_IRUGO,
		bq2415x_sysfs_show_limit, bq2415x_sysfs_set_limit);
static DEVICE_ATTR(weak_battery_voltage, S_IWUSR | S_IRUGO,
		bq2415x_sysfs_show_limit, bq2415x_sysfs_set_limit);
static DEVICE_ATTR(battery_regulation_voltage, S_IWUSR | S_IRUGO,
		bq2415x_sysfs_show_limit, bq2415x_sysfs_set_limit);
static DEVICE_ATTR(charge_current, S_IWUSR | S_IRUGO,
		bq2415x_sysfs_show_limit, bq2415x_sysfs_set_limit);
static DEVICE_ATTR(termination_current, S_IWUSR | S_IRUGO,
		bq2415x_sysfs_show_limit, bq2415x_sysfs_set_limit);

static DEVICE_ATTR(charge_termination_enable, S_IWUSR | S_IRUGO,
		bq2415x_sysfs_show_enable, bq2415x_sysfs_set_enable);
static DEVICE_ATTR(high_impedance_enable, S_IWUSR | S_IRUGO,
		bq2415x_sysfs_show_enable, bq2415x_sysfs_set_enable);
static DEVICE_ATTR(otg_pin_enable, S_IWUSR | S_IRUGO,
		bq2415x_sysfs_show_enable, bq2415x_sysfs_set_enable);
static DEVICE_ATTR(stat_pin_enable, S_IWUSR | S_IRUGO,
		bq2415x_sysfs_show_enable, bq2415x_sysfs_set_enable);

static DEVICE_ATTR(reported_mode, S_IRUGO,
		bq2415x_sysfs_show_reported_mode, NULL);
static DEVICE_ATTR(mode, S_IWUSR | S_IRUGO,
		bq2415x_sysfs_show_mode, bq2415x_sysfs_set_mode);
static DEVICE_ATTR(timer, S_IWUSR | S_IRUGO,
		bq2415x_sysfs_show_timer, bq2415x_sysfs_set_timer);

static DEVICE_ATTR(registers, S_IWUSR | S_IRUGO,
		bq2415x_sysfs_show_registers, bq2415x_sysfs_set_registers);

static DEVICE_ATTR(otg_status, S_IRUGO, bq2415x_sysfs_show_status, NULL);
static DEVICE_ATTR(charge_status, S_IRUGO, bq2415x_sysfs_show_status, NULL);
static DEVICE_ATTR(boost_status, S_IRUGO, bq2415x_sysfs_show_status, NULL);
static DEVICE_ATTR(fault_status, S_IRUGO, bq2415x_sysfs_show_status, NULL);

static struct attribute *bq2415x_sysfs_attributes[] = {
	/*
	 * TODO: some (appropriate) of these attrs should be switched to
	 * use power supply class props.
	 */
	&dev_attr_current_limit.attr,
	&dev_attr_weak_battery_voltage.attr,
	&dev_attr_battery_regulation_voltage.attr,
	&dev_attr_charge_current.attr,
	&dev_attr_termination_current.attr,

	&dev_attr_charge_termination_enable.attr,
	&dev_attr_high_impedance_enable.attr,
	&dev_attr_otg_pin_enable.attr,
	&dev_attr_stat_pin_enable.attr,

	&dev_attr_reported_mode.attr,
	&dev_attr_mode.attr,
	&dev_attr_timer.attr,

	&dev_attr_registers.attr,

	&dev_attr_otg_status.attr,
	&dev_attr_charge_status.attr,
	&dev_attr_boost_status.attr,
	&dev_attr_fault_status.attr,
	NULL,
};

static const struct attribute_group bq2415x_sysfs_attr_group = {
	.name = "ti-charger-prop",
	.attrs = bq2415x_sysfs_attributes,
};

static int bq2415x_sysfs_init(struct bq2415x_device *bq)
{
	if (!bq) {
		pr_err("%s: Invalid param, fatal error\n", __func__);
		return -EINVAL;
	}
	return sysfs_create_group(&bq->batt_psy.dev->kobj,
			&bq2415x_sysfs_attr_group);
}

static void bq2415x_sysfs_exit(struct bq2415x_device *bq)
{
	if (!bq) {
		pr_err("%s: Invalid param, fatal error\n", __func__);
		return -EINVAL;
	}
	sysfs_remove_group(&bq->batt_psy.dev->kobj, &bq2415x_sysfs_attr_group);
}

/* main bq2415x probe function */
static int bq2415x_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	int ret = 0, num = 0;
	char *name = NULL;
	struct bq2415x_device *bq = NULL;
	struct device_node *np = NULL;
	struct bq2415x_platform_data *pdata = NULL;
	struct power_supply *usb_psy = NULL;
	struct power_supply *bms_psy = NULL;

	if (client == NULL || id == NULL) {
		pr_err("%s: invalid param, fatal error\n", __func__);
		return -EINVAL;
	}

	np = client->dev.of_node;
	pdata = client->dev.platform_data;

	usb_psy = power_supply_get_by_name("usb");
	if (!usb_psy) {
		dev_dbg(&client->dev, "USB supply not found, defer probe\n");
		return -EPROBE_DEFER;
	}

	bms_psy = power_supply_get_by_name("bms");
	if (!bms_psy) {
		dev_dbg(&client->dev, "bms supply not found, defer probe\n");
		return -EPROBE_DEFER;
	}

	if (!np && !pdata) {
		dev_err(&client->dev, "platform data missing\n");
		return -ENODEV;
	}

	/* Get new ID for the new device */
	mutex_lock(&bq2415x_id_mutex);
	num = idr_alloc(&bq2415x_id, client, 0, 0, GFP_KERNEL);
	mutex_unlock(&bq2415x_id_mutex);
	if (num < 0)
		return num;

	name = kasprintf(GFP_KERNEL, "%s-%d", id->name, num);
	if (!name) {
		dev_err(&client->dev, "failed to allocate device name\n");
		ret = -ENOMEM;
		goto error_1;
	}

	pr_info("bq2415x_probe name %s\n",name);
	bq = devm_kzalloc(&client->dev, sizeof(*bq), GFP_KERNEL);
	if (!bq) {
		dev_err(&client->dev, "failed to allocate device data\n");
		ret = -ENOMEM;
		goto error_2;
	}

	bq->client = client;
	bq->usb_psy = usb_psy;
	bq->bms_psy = bms_psy;
	bq->id = num;
	bq->dev = &client->dev;
	bq->chip = id->driver_data;
	bq->name = name;
	bq->mode = BQ2415X_MODE_OFF;
	bq->reported_mode = BQ2415X_MODE_OFF;
	bq->autotimer = 0;
	bq->automode = 0;
	bq->resume_completed = true;
	bq->irq_waiting = false;
	
	i2c_set_clientdata(client, bq);
	bq->gpio_int = of_get_named_gpio(bq->dev->of_node,"gpio_int",0);
	if(!gpio_is_valid(bq->gpio_int))
	{
	   pr_err("gpio_int is not valid\n");
	   ret = -EINVAL;
	   goto error_2;
	}
	ret = gpio_request(bq->gpio_int,"charger_int");
	if(ret)
	{
		pr_err("could not request gpio_int\n");
		goto error_2;
	}

	pr_info("bq2415x_probe 2 client->addr: %x irq %d\n",client->addr,client->irq);

	if (np) {
		ret = of_property_read_u32(np, "ti,current-limit",
				&bq->init_data.current_limit);
		if (ret)
			goto error_2;
		ret = of_property_read_u32(np, "ti,weak-battery-voltage",
				&bq->init_data.weak_battery_voltage);
		if (ret)
			goto error_2;
		ret = of_property_read_u32(np, "ti,battery-regulation-voltage",
				&bq->init_data.battery_regulation_voltage);
		if (ret)
			goto error_2;
		ret = of_property_read_u32(np, "ti,charge-current",
				&bq->init_data.charge_current);
		if (ret)
			goto error_2;
		ret = of_property_read_u32(np, "ti,termination-current",
				&bq->init_data.termination_current);
		if (ret)
			goto error_2;
		ret = of_property_read_u32(np, "ti,resistor-sense",
				&bq->init_data.resistor_sense);
		if (ret)
			goto error_2;
	} else {
		memcpy(&bq->init_data, pdata, sizeof(bq->init_data));
	}

	bq2415x_reset_chip(bq);

	/* determine the vendor */
	ret = bq2415x_i2c_read(bq, BQ2415X_REG_VENDER);
	if (ret < 0) {
		dev_err(bq->dev, "failed to get chip vendor, treat as TI chip\n");
		bq->vendor_code = VENDOR_CODE_TI;
	} else {
		bq->vendor_code = ret >> VENDOR_CODE_OFFSET;
		if (VENDOR_CODE_TI == bq->vendor_code) {
			dev_err(bq->dev, "Using ti chip\n");
		} else if (VENDOR_CODE_ONSEMI == bq->vendor_code) {
			dev_err(bq->dev, "Using onsemi chip\n");
		} else {
			dev_err(bq->dev, "Unknown chip vendor, treat as TI\n");
			bq->vendor_code = VENDOR_CODE_TI;
		}
	}

	ret = bq2415x_power_supply_init(bq);
	if (ret) {
		dev_err(bq->dev, "failed to register power supply: %d\n", ret);
		goto error_2;
	}

	ret = bq2415x_sysfs_init(bq);
	if (ret) {
		dev_err(bq->dev, "failed to create sysfs entries: %d\n", ret);
		goto error_3;
	}

	ret = bq2415x_set_defaults(bq);
	if (ret) {
		dev_err(bq->dev, "failed to set default values: %d\n", ret);
		goto error_4;
	}

	ret = bq2415x_regulator_init(bq);
	if (ret) {
		pr_err("Couldn't initialize bq2415x regulator ret=%d\n", ret);
		goto error_4;
	}

	if (bq->init_data.set_mode_hook) {
		if (bq->init_data.set_mode_hook(
				bq2415x_hook_function, bq)) {
			bq->automode = AUTO_MODE_ENABLED;
			bq2415x_set_mode(bq, bq->reported_mode);
			dev_info(bq->dev, "automode enabled\n");
		} else {
			bq->automode = AUTO_MODE_UNSUPPORT;
			dev_info(bq->dev, "automode failed\n");
		}
	} else {
		bq->automode = AUTO_MODE_UNSUPPORT;
		dev_info(bq->dev, "automode not supported\n");
	}

	wake_lock_init(&bq->charging_wake_lock, WAKE_LOCK_SUSPEND, "charging_wake_lock");
	mutex_init(&bq->data_lock);
	mutex_init(&bq->irq_complete);
	mutex_init(&bq->current_change_lock);
	INIT_DELAYED_WORK(&bq->work, bq2415x_timer_work);
	bq2415x_set_autotimer(bq, AUTO_TIMER_ENABLE);
	INIT_WORK(&bq->iusb_work,bq2415x_iusb_work);
	INIT_DELAYED_WORK(&bq->recharge_timer_work, bq2415x_charging_recharge_func);
	INIT_DELAYED_WORK(&bq->lower_power_charger_work,
			bq2415x_usb_low_power_work);
	determine_initial_status(bq);
	if (client->irq) {
		ret = devm_request_threaded_irq(&client->dev, client->irq, NULL,
				bq2415x_charger_interrupt,
				IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING |IRQF_ONESHOT,
				"bq2415x charger irq", bq);
		if (ret < 0) {
			pr_err("request irq for irq=%d failed, ret =%d\n", client->irq, ret);
			goto error_4;
		}
		enable_irq_wake(client->irq);
	}
#ifdef CONFIG_HLTHERM_RUNTEST
	high_temp = CONFIG_HLTHERM_TEMP/INPUT_NUMBER_BASE;
	pr_info("%s: CONFIG_HLTHERM_TEMP = %d  high_temp=%d\n", __func__, CONFIG_HLTHERM_TEMP,high_temp);
#endif
	pr_info("bq2415x probe ok\n");

	return 0;

error_4:
	bq2415x_sysfs_exit(bq);
	wake_lock_destroy(&bq->charging_wake_lock);
error_3:
	bq2415x_power_supply_exit(bq);
error_2:
	kfree(name);
error_1:
	mutex_lock(&bq2415x_id_mutex);
	idr_remove(&bq2415x_id, num);
	mutex_unlock(&bq2415x_id_mutex);

	return ret;
}

/* main bq2415x remove function */

static int bq2415x_remove(struct i2c_client *client)
{
	struct bq2415x_device *bq = NULL;

	if (client == NULL) {
		pr_err("%s: Invalid param, fatal error\n", __func__);
		return -EINVAL;
	}

	bq = i2c_get_clientdata(client);

	if(NULL == bq) {
		pr_err("bq2415x_remove: bq is NULL\n");
		return -EINVAL;
	}


	if (bq->init_data.set_mode_hook)
		bq->init_data.set_mode_hook(NULL, NULL);

	cancel_delayed_work_sync(&bq->lower_power_charger_work);
	regulator_unregister(bq->otg_vreg.rdev);
	bq2415x_sysfs_exit(bq);
	bq2415x_power_supply_exit(bq);

	bq2415x_reset_chip(bq);

	mutex_destroy(&bq->current_change_lock);
	mutex_lock(&bq2415x_id_mutex);
	idr_remove(&bq2415x_id, bq->id);
	mutex_unlock(&bq2415x_id_mutex);

	dev_info(bq->dev, "driver unregistered\n");

	kfree(bq->name);

	return 0;
}
static int bq2415x_suspend(struct device *dev)
{
	struct i2c_client *client = NULL;
	struct bq2415x_device *bq = NULL;

	if(NULL == dev) {
		pr_err("bq2415x_suspend: dev is NULL\n");
		return -EINVAL;
	}

	client = to_i2c_client(dev);
	bq = i2c_get_clientdata(client);

	if(NULL == bq) {
		pr_err("bq2415x_suspend: bq is NULL\n");
		return -EINVAL;
	}
	mutex_lock(&bq->irq_complete);
	bq->resume_completed = false;
	mutex_unlock(&bq->irq_complete);
	cancel_delayed_work_sync(&bq->work);

	pr_err("Suspend successfully!");
	return 0;
}
static inline bool is_device_suspended(struct bq2415x_device *bq)
{
	if(NULL == bq) {
		pr_err("is_device_suspended: bq is NULL\n");
		return -EINVAL;
	}

	return !bq->resume_completed;
}


static int bq2415x_suspend_noirq(struct device *dev)
{
	struct i2c_client *client = NULL;
	struct bq2415x_device *bq = NULL;

	if(NULL == dev) {
		pr_err("bq2415x_suspend_noirq: dev is NULL\n");
		return -EINVAL;
	}

	client = to_i2c_client(dev);
	bq = i2c_get_clientdata(client);

	if(NULL == bq) {
		pr_err("is_device_suspended: bq is NULL\n");
		return -EINVAL;
	}

	if (bq->irq_waiting) {
		pr_err_ratelimited("Aborting suspend, an interrupt was detected while suspending\n");
		return -EBUSY;
	}
	return 0;
}

static int bq2415x_resume(struct device *dev)
{
	struct i2c_client *client = NULL;
	struct bq2415x_device *bq = NULL;

	if(NULL == dev) {
		pr_err("bq2415x_resume: dev is NULL\n");
		return -EINVAL;
	}

	client = to_i2c_client(dev);
	bq = i2c_get_clientdata(client);

	if(NULL == bq) {
		pr_err("bq2415x_resume: bq is NULL\n");
		return -EINVAL;
	}

	mutex_lock(&bq->irq_complete);
	bq->resume_completed = true;
	if (bq->irq_waiting) {
		bq->irq_disabled = false;
		enable_irq(client->irq);
		mutex_unlock(&bq->irq_complete);
		bq2415x_charger_interrupt(client->irq, bq);
	} else {
		mutex_unlock(&bq->irq_complete);
	}
	
	schedule_delayed_work(&bq->work, 0);
	power_supply_changed(&bq->batt_psy);
    bq2415x_charging_wake_lock_check(bq);
	pr_err("Resume successfully!");
	return 0;
}

static const struct i2c_device_id bq2415x_i2c_id_table[] = {
	{ "bq2415x", BQUNKNOWN },
	{ "bq24150", BQ24150 },
	{ "bq24150a", BQ24150A },
	{ "bq24151", BQ24151 },
	{ "bq24151a", BQ24151A },
	{ "bq24152", BQ24152 },
	{ "bq24153", BQ24153 },
	{ "bq24153a", BQ24153A },
	{ "bq24155", BQ24155 },
	{ "bq24156", BQ24156 },
	{ "bq24156a", BQ24156A },
	{ "bq24157", BQ24157 },
	{ "bq24158", BQ24158 },
	{},

};
MODULE_DEVICE_TABLE(i2c, bq2415x_i2c_id_table);
static struct of_device_id bq2415x_charger_match_table[] =
{
   { .compatible = "ti,bq24157",},
   {},
};

static const struct dev_pm_ops bq2415x_pm_ops = {
	.resume		= bq2415x_resume,
	.suspend_noirq = bq2415x_suspend_noirq,
	.suspend	= bq2415x_suspend,
};


static struct i2c_driver bq2415x_driver = {
	.driver = {
		.name = "ti,bq24157",
		.pm   = &bq2415x_pm_ops,
		 .of_match_table = bq2415x_charger_match_table,
	},
	.probe = bq2415x_probe,
	.remove = bq2415x_remove,

	.id_table = bq2415x_i2c_id_table,
};
static int __init bq2415x_charger_init(void)
{
	return i2c_add_driver(&bq2415x_driver);
}
device_initcall_sync(bq2415x_charger_init);

static void __exit bq2415x_charger_exit(void)
{
	i2c_del_driver(&bq2415x_driver);
}
module_exit(bq2415x_charger_exit);


MODULE_AUTHOR("Pali Rohár <pali.rohar@gmail.com>");
MODULE_DESCRIPTION("bq2415x charger driver");
MODULE_LICENSE("GPL");
