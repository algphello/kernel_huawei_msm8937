#ifdef CONFIG_HUAWEI_USB
#ifdef CONFIG_QPNP_SMBCHARGER

static void smbchg_stay_awake(struct smbchg_chip *chip, int reason);
static void smbchg_relax(struct smbchg_chip *chip, int reason);

#define RELEASE_WAKELOCK		1
#define RESTORE_WAKELOCK		0

static void smbchg_release_wakelock(struct smbchg_chip *chip, int flag)
{
    pr_info("release wakelock flag = %d\n", flag);

    if (NULL == chip) {
        pr_info("%s, input ptr is NULL!\n", __func__);
        return ;
    }

    if (flag == RELEASE_WAKELOCK) {
        chip->release_wakelock_flag = true;
        device_wakeup_disable(chip->dev);
        /* Clear the awake flag to allow sleep for factory deep sleep test */
        smbchg_relax(chip, PM_CHARGING_CHECK);
    } else if (flag == RESTORE_WAKELOCK) {
        chip->release_wakelock_flag = false;
        device_wakeup_enable(chip->dev);
        /* Stay awake when restore from factory deep sleep test */
        smbchg_stay_awake(chip, PM_CHARGING_CHECK);
    } else {
        chip->release_wakelock_flag = false;
        pr_err("invallid input: flag = %d\n", flag);
    }
}

#endif
#endif