#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/utsname.h>
#include <linux/platform_device.h>
#include <linux/pm_qos.h>
#include <linux/of.h>
#include <linux/usb/android.h>
#include <linux/power_supply.h>

#define HW_USB_ZERO     0
#define HW_USB_SUCCESS  1
#define HW_BUF_LEN      10

static int release_wakelock;
static bool is_in_factory_mode;
static bool open_release_wakelock = false;


/**
 * release_wakelock include usb lock && charger lock,
 * so that system can enter deep sleep.
 */
ssize_t release_wakelock_show(struct device *dev,
                              struct device_attribute *attr,
                              char *buf)
{
    pr_info("[USB_DEBUG] %s is_in_factory_mode is %d, open_release_wakelock is %d.\n", __func__, is_in_factory_mode, open_release_wakelock);

    if ((NULL == dev) || (NULL == attr) || (NULL == buf)) {
        pr_info("[USB_DEBUG] Enter function: %s, input ptr is NULL!\n", __func__);
        return -EINVAL;
    }
    if (is_in_factory_mode || open_release_wakelock) {
        return snprintf(buf, PAGE_SIZE, "%d\n", release_wakelock);
    } else {
        return HW_USB_SUCCESS;
    }
}

ssize_t release_wakelock_store(struct device *pdev,
                               struct device_attribute *attr,
                               const char *buf, size_t size)
{
    int val = 0;
    int rc = 0;
    static struct power_supply *usb_psy = NULL;
    static struct power_supply *batt_psy = NULL;
    union power_supply_propval prop = {0,};
    union power_supply_propval ret = {0,};

    if ((NULL == pdev) || (NULL == attr) || (NULL == buf)) {
        pr_info("[USB_DEBUG] Enter function: %s, input ptr is NULL!\n", __func__);
        return -EINVAL;
    }
    /* The feature is only supported in factory mode */
    if (!is_in_factory_mode && !open_release_wakelock) {
        pr_info("[USB_DEBUG] %s not in factory mode or close release wakelock\n", __func__);
        return HW_USB_SUCCESS;
    }

    if (kstrtoint(buf, HW_BUF_LEN, &val)) {
        pr_info("[USB_DEBUG] %s invalid input\n", __func__);
        return -EINVAL;
    }

    if (!usb_psy || !usb_psy->get_property) {
        usb_psy = power_supply_get_by_name("usb");
        if (!usb_psy || !usb_psy->get_property) {
            pr_info("[USB_DEBUG] %s: USB supply not found\n", __func__);
            return -EPROBE_DEFER;
        }
    }

    if (!batt_psy || !batt_psy->get_property || !batt_psy->set_property) {
        batt_psy = power_supply_get_by_name("battery");
        if (!batt_psy || !batt_psy->get_property || !batt_psy->set_property) {
            pr_info("[USB_DEBUG] %s: battery supply not found\n", __func__);
            return -EPROBE_DEFER;
        }
    }

    /*
     * release_wakelock:1, set USB VBUS offline and notify charge to sleep
     * release_wakelock:0,
     * restore USB VBUS to online and notify charge to wake
     */
    release_wakelock = !!val;
    usb_psy->get_property(usb_psy, POWER_SUPPLY_PROP_PRESENT, &prop);
    pr_info("[USB_DEBUG] %s release = %d vbus present = %d\n", __func__,
            release_wakelock, prop.intval);
    if (release_wakelock == prop.intval) {
        power_supply_set_present(usb_psy, !release_wakelock);
        ret.intval = release_wakelock;
        rc = batt_psy->set_property(batt_psy, POWER_SUPPLY_PROP_RELEASE_WAKELOCK, &ret);
        if (rc) {
            pr_info("[USB_DEBUG] %s set_property failed rc =%d\n", __func__, rc);
            return -EINVAL;
        }
    }

    return size;
}

static DEVICE_ATTR(release_wakelock, S_IRUGO | S_IWUSR | S_IWGRP,
                release_wakelock_show, release_wakelock_store);

static int __init early_parse_factory_flag(char *p)
{
    if (p && !strncmp(p, "factory", strlen("factory"))) {
        is_in_factory_mode = true;
    }

#ifndef CONFIG_FINAL_RELEASE
    open_release_wakelock = true;
#endif
    return HW_USB_ZERO;
}

early_param("androidboot.huawei_swtype", early_parse_factory_flag);
