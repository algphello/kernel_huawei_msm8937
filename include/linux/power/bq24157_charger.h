/*
 * bq2415x charger driver
 *
 * Copyright (C) 2011-2013  Pali Rohár <pali.rohar@gmail.com>
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

#ifndef BQ2415X_CHARGER_H
#define BQ2415X_CHARGER_H

/*
 * This is platform data for bq2415x chip. It contains default board
 * voltages and currents which can be also later configured via sysfs. If
 * value is -1 then default chip value (specified in datasheet) will be
 * used.
 *
 * Value resistor_sense is needed for for configuring charge and
 * termination current. It it is less or equal to zero, configuring charge
 * and termination current will not be possible.
 *
 * For automode support is needed to provide name of power supply device
 * in value notify_device. Device driver must immediately report property
 * POWER_SUPPLY_PROP_CURRENT_MAX when current changed.
 */

/* Supported modes with maximal current limit */
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regulator/machine.h>
#include <linux/wakelock.h>
enum bq2415x_command {
	BQ2415X_TIMER_RESET,
	BQ2415X_OTG_STATUS,
	BQ2415X_STAT_PIN_STATUS,
	BQ2415X_STAT_PIN_ENABLE,
	BQ2415X_STAT_PIN_DISABLE,
	BQ2415X_CHARGE_STATUS,
	BQ2415X_BOOST_STATUS,
	BQ2415X_FAULT_STATUS,

	BQ2415X_CHARGE_TERMINATION_STATUS,
	BQ2415X_CHARGE_TERMINATION_ENABLE,
	BQ2415X_CHARGE_TERMINATION_DISABLE,
	BQ2415X_CHARGER_STATUS,
	BQ2415X_CHARGER_ENABLE,
	BQ2415X_CHARGER_DISABLE,
	BQ2415X_HIGH_IMPEDANCE_STATUS,
	BQ2415X_HIGH_IMPEDANCE_ENABLE,
	BQ2415X_HIGH_IMPEDANCE_DISABLE,
	BQ2415X_BOOST_MODE_STATUS,
	BQ2415X_BOOST_MODE_ENABLE,
	BQ2415X_BOOST_MODE_DISABLE,

	BQ2415X_OTG_LEVEL,
	BQ2415X_OTG_ACTIVATE_HIGH,
	BQ2415X_OTG_ACTIVATE_LOW,
	BQ2415X_OTG_PIN_STATUS,
	BQ2415X_OTG_PIN_ENABLE,
	BQ2415X_OTG_PIN_DISABLE,

	BQ2415X_VENDER_CODE,
	BQ2415X_PART_NUMBER,
	BQ2415X_REVISION,
};

/* Supported modes with maximal current limit */
enum bq2415x_mode {
	BQ2415X_MODE_OFF,		/* offline mode (charger disabled) */
	BQ2415X_MODE_NONE,		/* unknown charger (100mA) */
	BQ2415X_MODE_HOST_CHARGER,	/* usb host/hub charger (500mA) */
	BQ2415X_MODE_DEDICATED_CHARGER, /* dedicated charger (unlimited) */
	BQ2415X_MODE_BOOST,		/* boost mode (charging disabled) */
};


enum bq2415x_chip {
	BQUNKNOWN,
	BQ24150,
	BQ24150A,
	BQ24151,
	BQ24151A,
	BQ24152,
	BQ24153,
	BQ24153A,
	BQ24155,
	BQ24156,
	BQ24156A,
	BQ24157,
	BQ24158,

};

static char *bq2415x_chip_name[] = {
	"unknown",
	"bq24150",
	"bq24150a",
	"bq24151",
	"bq24151a",
	"bq24152",
	"bq24153",
	"bq24153a",
	"bq24155",
	"bq24156",
	"bq24156a",
	"bq24157",
	"bq24158",

};


struct bq2415x_platform_data {
	int current_limit;		/* mA */
	int weak_battery_voltage;	/* mV */
	int battery_regulation_voltage;	/* mV */
	int charge_current;		/* mA */
	int termination_current;	/* mA */
	int resistor_sense;		/* m ohm */
	int (*set_mode_hook)(void (*hook)(enum bq2415x_mode mode, void *data),
			     void *data);
};
struct bq24152_otg_regulator
{
    struct regulator_desc    rdesc;
    struct regulator_dev    *rdev;
};

struct bq2415x_device {
	struct device *dev;
	struct bq2415x_platform_data init_data;
	struct power_supply batt_psy;
	struct i2c_client *client;
	struct delayed_work work;
	enum bq2415x_mode reported_mode;/* mode reported by hook function */
	enum bq2415x_mode mode;		/* current configured mode */
	enum bq2415x_chip chip;
	const char *timer_error;
	char *model;
	char *name;
	int autotimer;	/* 1 - if driver automatically reset timer, 0 - not */
	int automode;	/* 1 - enabled, 0 - disabled; -1 - not supported */
	int id;
	int iusb_limit;
	spinlock_t ibat_change_lock;
	struct power_supply *bms_psy;
	struct power_supply *usb_psy;
	struct work_struct iusb_work;
	struct delayed_work lower_power_charger_work;
	struct delayed_work recharge_timer_work;
	int charge_status;
	bool charge_disable;
	bool usb_present;
    struct bq24152_otg_regulator    otg_vreg;
    struct mutex    current_change_lock;
    struct wake_lock charging_wake_lock;/*charing wake_lock*/
	struct mutex data_lock;
	struct mutex irq_complete;
	bool irq_waiting;
	bool irq_disabled;
	bool resume_completed;
	int gpio_int;
	int vendor_code;
	int recharge_time;
};

#define BATTERY_TEMP_MAX   600

#endif
