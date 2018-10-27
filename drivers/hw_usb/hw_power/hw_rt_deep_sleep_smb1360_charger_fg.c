#ifdef CONFIG_HUAWEI_USB
#ifdef CONFIG_SMB1360_CHARGER_FG

#define pr_fmt(fmt) "SMB:%s: " fmt, __func__

#include <linux/i2c.h>
#include <linux/debugfs.h>
#include <linux/gpio.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/math64.h>
#include <linux/slab.h>
#include <linux/power_supply.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regulator/machine.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/bitops.h>
#include <linux/qpnp/qpnp-adc.h>
#include <linux/completion.h>
#include <linux/pm_wakeup.h>
#include <linux/power/huawei_charger.h>

static void smb1360_stay_awake(struct smb1360_wakeup_source *source, enum wakeup_src wk_src);
static void smb1360_relax(struct smb1360_wakeup_source *source, enum wakeup_src wk_src);

#define RELEASE_WAKELOCK		1
#define RESTORE_WAKELOCK		0

static void smb1360_release_wakelock(struct smb1360_chip *chip, int flag)
{
    pr_info( "release wakelock flag = %d\n", flag);

	if (NULL == chip) {
        pr_info("%s, input ptr is NULL!\n", __func__);
        return ;
    }

    if (flag == RELEASE_WAKELOCK) {
        chip->release_wakelock_flag = true;
        device_wakeup_disable(chip->dev);
        /* Clear the awake flag to allow sleep for factory deep sleep test */
        smb1360_relax(&chip->smb1360_ws, WAKEUP_SRC_CHARGING_CHECK);
    } else if (flag == RESTORE_WAKELOCK) {
        chip->release_wakelock_flag = false;
        device_wakeup_enable(chip->dev);
        /* Stay awake when restore from factory deep sleep test */
        smb1360_stay_awake(&chip->smb1360_ws, WAKEUP_SRC_CHARGING_CHECK);
    } else {
        chip->release_wakelock_flag = false;
        pr_err("invallid input: flag = %d\n", flag);
    }
}

#endif
#endif