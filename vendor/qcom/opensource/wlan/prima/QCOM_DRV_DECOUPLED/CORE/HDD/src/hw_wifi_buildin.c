#ifdef CONFIG_HUAWEI_WIFI_BUILTIN
#include <linux/types.h>
#include "wlan_hdd_trace.h"
#include "vos_types.h"
#include "vos_trace.h"
#include <linux/semaphore.h>
#include <linux/ctype.h>
#include <linux/delay.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>



/*for mac set*/
#define MAC_PARAMENT_MAX_LEN    50
char mac_param[MAC_PARAMENT_MAX_LEN] = {0};
//module_param_string(mac_param, mac_param, MAC_PARAMENT_MAX_LEN, 0644);
#define MOD_DEBUG_PARAMENT_LEN   17
char module_debug_param[MOD_DEBUG_PARAMENT_LEN + 1] = {0};
//module_param_string(module_debug_param, module_debug_param, MOD_DEBUG_PARAMENT_LEN, 0644);


struct mutex wifi_enable_write_mutex;
#define WIFI_BUILT_IN_PROC_DIR "wifi_built_in"
#define WIFI_START_PROC_FILE   "wifi_start"
#define WIFI_MAC_ADDR_HW_PROC_FILE  "mac_addr_hw"
#define WIFI_DEBUG_LEVEL_HW_PROC_FILE  "debug_level_hw"
#define MAX_USER_COMMAND_HW_SIZE 64
#define CMD_START_DRIVER_HW "start"
#define CMD_STOP_DRIVER_HW "stop"
#define CMD_FTM_HW "ftm"





extern void hdd_set_conparam ( v_UINT_t newParam );
extern tVOS_CON_MODE hdd_get_conparam ( void );


extern int hdd_driver_init(void);
extern int hdd_driver_exit(void);
extern int wlan_hdd_inited;



/**---------------------------------------------------------------------------

  \brief kickstart_driver_huawei

   This is the driver entry point
   - delayed driver initialization when driver is statically linked
   - invoked when wifi_enable parameter is modified from userspace to signal
     initializing the WLAN driver or when con_mode is modified from userspace
     to signal a switch in operating mode
  \param new_con_mode - which operating mode write by userspace to control writer
  \return - 0 for success, non zero for failure

  --------------------------------------------------------------------------*/
static int kickstart_driver_huawei(v_UINT_t new_con_mode)
{
   int ret_status = 0;

   if (!wlan_hdd_inited) {
      hdd_set_conparam(new_con_mode);
      ret_status = hdd_driver_init();
   } else {
       if(hdd_get_conparam() != new_con_mode){
           hdd_driver_exit();

           msleep(200);
           hdd_set_conparam(new_con_mode);
           ret_status = hdd_driver_init();
       }
   }

   wlan_hdd_inited = ret_status ? 0 : 1;

   return ret_status;
}


/*
  Function:       wifi_driver_control_write
  Description:    wifi driver load or unload control function
  Calls:          write the /proc/wifi_enable/wifi_start file
  Input:
    filp:  which file be writerrn
    buf:  what be wrttern to filp
    count: the len of content in buf
    off:  off length
  Return:        the length been writern to the file
*/
static ssize_t wifi_driver_control_write(struct file *filp, const char __user *buf, size_t count, loff_t *off)
{
    char cmd[MAX_USER_COMMAND_HW_SIZE + 1] = {0};
    int ret_status;

    if (count > MAX_USER_COMMAND_HW_SIZE)
    {
        VOS_TRACE(VOS_MODULE_ID_HDD, VOS_TRACE_LEVEL_ERROR,
                  "%s: Command length %d is larger than %d bytes. ",
                  __func__, (int)count,MAX_USER_COMMAND_HW_SIZE);

        return -EINVAL;
    }

    /* Get command from user */
    if (copy_from_user(cmd, buf, count)) {
        VOS_TRACE(VOS_MODULE_ID_HDD, VOS_TRACE_LEVEL_ERROR,
                      "%s: copy_from_user error.",
                      __func__);
        return -EFAULT;
    }

    cmd[count] = '\0';

    VOS_TRACE(VOS_MODULE_ID_HDD, VOS_TRACE_LEVEL_ERROR,
                  "%s: Command is %s . Command len is %d",
                  __func__, cmd, (int)strlen(cmd));

    mutex_lock(&wifi_enable_write_mutex);

    if (!strncmp(cmd,CMD_START_DRIVER_HW,strlen(CMD_START_DRIVER_HW))) {
        VOS_TRACE(VOS_MODULE_ID_HDD, VOS_TRACE_LEVEL_ERROR,
                      "%s: command is load driver.",
                      __func__);

        ret_status = kickstart_driver_huawei((v_UINT_t)VOS_STA_MODE);
        if (0 != ret_status) {
            VOS_TRACE(VOS_MODULE_ID_HDD, VOS_TRACE_LEVEL_ERROR,
                      "%s: command is load driver.",
                      __func__);
            mutex_unlock(&wifi_enable_write_mutex);
            return -EFAULT;
        }
    } else if (!strncmp(cmd,CMD_STOP_DRIVER_HW,strlen(CMD_STOP_DRIVER_HW))) {
        VOS_TRACE(VOS_MODULE_ID_HDD, VOS_TRACE_LEVEL_ERROR,
                       "%s: command is unload driver.",
                       __func__);

        if (wlan_hdd_inited) {
            hdd_driver_exit();
            wlan_hdd_inited = 0;
        }
    } else if (!strncmp(cmd,CMD_FTM_HW,strlen(CMD_FTM_HW))) {
        VOS_TRACE(VOS_MODULE_ID_HDD, VOS_TRACE_LEVEL_ERROR,
                       "%s: command is load driver for FTM.",
                       __func__);
        ret_status = kickstart_driver_huawei((v_UINT_t)VOS_FTM_MODE);
        if(0 != ret_status){
            VOS_TRACE(VOS_MODULE_ID_HDD, VOS_TRACE_LEVEL_ERROR,
                      "%s: command is load driver.",
                      __func__);
            mutex_unlock(&wifi_enable_write_mutex);
            return -EFAULT;
        }
    } else {
        VOS_TRACE(VOS_MODULE_ID_HDD, VOS_TRACE_LEVEL_ERROR,
                      "%s: wrong command is %s .",
                      __func__, cmd);
        mutex_unlock(&wifi_enable_write_mutex);
        return -EINVAL;
    }

    mutex_unlock(&wifi_enable_write_mutex);

    return count;

}

static ssize_t mac_addr_hw_write(struct file *filp, const char __user *buf, size_t count, loff_t *off)
{

    char cmd[MAX_USER_COMMAND_HW_SIZE + 1] = {0};

    if (count > MAX_USER_COMMAND_HW_SIZE) {
        VOS_TRACE(VOS_MODULE_ID_HDD, VOS_TRACE_LEVEL_ERROR,
                  "%s: Command length %d is larger than %d bytes. ",
                  __func__, (int)count,MAX_USER_COMMAND_HW_SIZE);

        return -EINVAL;
    }

    VOS_TRACE(VOS_MODULE_ID_HDD, VOS_TRACE_LEVEL_ERROR,
                  "%s: buf length is %d, max count is %d bytes. ",
                  __func__, (int)count, MAX_USER_COMMAND_HW_SIZE);

    mutex_lock(&wifi_enable_write_mutex);
    // TODO:  is need to free cmd
    /* Get command from user */
    if (copy_from_user(cmd, buf, count)) {
        VOS_TRACE(VOS_MODULE_ID_HDD, VOS_TRACE_LEVEL_ERROR,
                      "%s: copy_from_user error.",
                      __func__);
        mutex_unlock(&wifi_enable_write_mutex);
        return -EFAULT;
    }

    cmd[count] = '\0';

#ifdef CONFIG_HUAWEI_WIFI
    VOS_TRACE(VOS_MODULE_ID_HDD, VOS_TRACE_LEVEL_ERROR,
                 "%s: Command is mac address . Command len is %d",
                 __func__, (int)strlen(cmd));
#else
    VOS_TRACE(VOS_MODULE_ID_HDD, VOS_TRACE_LEVEL_ERROR,
                  "%s: Command is %s . Command len is %d",
                  __func__, cmd, (int)strlen(cmd));
#endif
    memcpy(mac_param,cmd,(MAC_PARAMENT_MAX_LEN-1));

    mutex_unlock(&wifi_enable_write_mutex);

    return count;
}

static ssize_t debug_level_hw_write(struct file *filp, const char __user *buf, size_t count, loff_t *off)
{

    char cmd[MOD_DEBUG_PARAMENT_LEN + 1] = {0};

    if (count > MOD_DEBUG_PARAMENT_LEN) {
        VOS_TRACE(VOS_MODULE_ID_HDD, VOS_TRACE_LEVEL_ERROR,
                  "%s: Command length %d is larger than %d bytes. ",
                  __func__, (int)count,MOD_DEBUG_PARAMENT_LEN);

        return -EINVAL;
    }

    VOS_TRACE(VOS_MODULE_ID_HDD, VOS_TRACE_LEVEL_ERROR,
                  "%s: buf length is %d, max count is %d bytes. ",
                  __func__, (int)count, MOD_DEBUG_PARAMENT_LEN);

    mutex_lock(&wifi_enable_write_mutex);

    /* Get command from user */
    if (copy_from_user(cmd, buf, count)) {
        VOS_TRACE(VOS_MODULE_ID_HDD, VOS_TRACE_LEVEL_ERROR,
                      "%s: copy_from_user error.",
                      __func__);
        mutex_unlock(&wifi_enable_write_mutex);
        return -EFAULT;
    }

    cmd[count] = '\0';

    VOS_TRACE(VOS_MODULE_ID_HDD, VOS_TRACE_LEVEL_ERROR,
                  "%s: Command is %s . Command len is %d",
                  __func__, cmd, (int)strlen(cmd));

    memcpy(module_debug_param, cmd, MOD_DEBUG_PARAMENT_LEN+1);

    mutex_unlock(&wifi_enable_write_mutex);

    return count;
}


static const struct file_operations wifi_proc_start_file_oper = {
    .owner = THIS_MODULE,
    .write = wifi_driver_control_write,
};

static const struct file_operations mac_addr_hw_file_oper = {
    .owner = THIS_MODULE,
    .write = mac_addr_hw_write,
};

static const struct file_operations debug_level_hw_file_oper = {
    .owner = THIS_MODULE,
    .write = debug_level_hw_write,
};

int __init hw_buildin_module_init ( void)
{
    int ret = 0;
    struct proc_dir_entry *wifi_start_dir = NULL;
    struct proc_dir_entry *wifi_start_file = NULL;
    struct proc_dir_entry *mac_addr_hw_file = NULL;
    struct proc_dir_entry *debug_level_hw_file = NULL;

    /* Driver initialization is delayed to change wifi_start_file */
    pr_info("wlan: loading driver build-in\n");

    wifi_start_dir = proc_mkdir(WIFI_BUILT_IN_PROC_DIR, NULL);
    if (!wifi_start_dir) {
        ret = -ENOMEM;
        pr_info("wlan: loading driver build-in proc_mkdir error = %d\n", ret);
        return ret;
    }

    wifi_start_file = proc_create(WIFI_START_PROC_FILE, S_IWUSR|S_IWGRP, wifi_start_dir, &wifi_proc_start_file_oper);
    if (!wifi_start_file) {
        ret = -ENOMEM;
        pr_info("wlan: loading driver build-in proc_create wifi_start_file error = %d\n", ret);
        return ret;
    }

    mac_addr_hw_file = proc_create(WIFI_MAC_ADDR_HW_PROC_FILE,S_IWUSR|S_IWGRP, wifi_start_dir,&mac_addr_hw_file_oper);
    if (!mac_addr_hw_file) {
        ret = -ENOMEM;
        pr_info("wlan: loading driver build-in proc_create mac_addr_hw_file error = %d\n", ret);
    }

    debug_level_hw_file = proc_create(WIFI_DEBUG_LEVEL_HW_PROC_FILE,S_IWUSR|S_IWGRP, wifi_start_dir,&debug_level_hw_file_oper);
    if (!debug_level_hw_file) {
        ret = -ENOMEM;
        pr_info("wlan: loading driver build-in proc_create debug_level_hw_file error = %d\n", ret);
    }

    mutex_init(&wifi_enable_write_mutex);
	return 0;
}

#endif    //#ifdef CONFIG_HUAWEI_WIFI_BUILTIN

