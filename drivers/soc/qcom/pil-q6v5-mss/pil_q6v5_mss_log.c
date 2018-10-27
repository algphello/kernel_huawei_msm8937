/*
 * Copyright (C) 2013 Huawei Device Co.Ltd
 * License terms: GNU General Public License (GPL) version 2
 *
 */

#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <soc/qcom/smem.h>
#include<linux/aio.h>
#include <linux/uaccess.h>
#include <soc/qcom/subsystem_restart.h>
#include "pil_q6v5_mss_ultils.h"
#include "pil_q6v5_mss_log.h"

#define DATA_LOG_DIR    "/data/log/modem_crash/"
#define MODEM_EXCEPTION "modem_exception"
#define MODEM_CRASH_LOG "modem_crash_log"
#define MODEM_F3_TRACE      "modem_f3_trace"
#define HUAWEI_STR "huawei"
#define HUAWEI_REQUEST_STR "cm_hw_request_modem_reset"
#define SSR_REASON_LEN  (130U)    //should same as MAX_SSR_REASON_LEN in pil-q6v5-mss.c
#define ERR_DATA_MAX_SIZE (0x4000U) //16K
#define DIAG_F3_TRACE_BUFFER_SIZE (0x4000U)  //16K
#define MAX_PATH_LEN (255)

/* define work data sturct*/
struct work_data{
    struct work_struct log_modem_work; //WORK
    struct workqueue_struct *log_modem_work_queue; //WORK QUEUE
    char reset_reason[SSR_REASON_LEN];
    char crash_log_valid;
    char crash_log[ERR_DATA_MAX_SIZE];
    char f3_trace_log_valid;
    char f3_trace_log[DIAG_F3_TRACE_BUFFER_SIZE + 4];
};

//struct work_struct log_modem_work; //used to fill the temp work
static struct work_data *g_work_data = NULL;
static DEFINE_MUTEX(lock);

/**
 *  name: the name of this command
 *  msg: concrete command string to write to /dev/log/exception
 *  return: on success return the bytes writed successfully, on error return -1
 *
*/
static int store_exception(char *path, char* name, char* msg, int len)
{
    mm_segment_t oldfs;
    struct file *file;
    struct iovec vec;
    int ret = 0;
    char log_path[MAX_PATH_LEN] = {0};

    if(NULL == path || NULL == name || NULL == msg)
    {
        pr_err("store_exception: the arguments invalidate\n");
        return -1;
    }

    snprintf(log_path, sizeof(log_path), "%s%s", path, name); 
    pr_info("store_exception: log_path:%s  msg:%s\n",log_path, msg);
    oldfs = get_fs();
    set_fs(KERNEL_DS);

    file = filp_open(log_path, O_CREAT|O_RDWR, 0664);
    if(!file || IS_ERR(file))
    {
        pr_err("store_exception: Failed to access\n");
        set_fs(oldfs);
        return -1;
    }

    mutex_lock(&lock);
    vfs_truncate(&file->f_path,  0);
    vec.iov_base = msg;
    vec.iov_len = len;
    ret = vfs_writev(file, &vec, 1, &file->f_pos);
    mutex_unlock(&lock);

    if(ret < 0)
    {
        pr_err("store_exception: Failed to write to /dev/log/\n");
        filp_close(file, NULL);
        set_fs(oldfs);
        return -1;
    }

    pr_info("store_exception: success\n");
    filp_close(file, NULL);
    set_fs(oldfs);
    return ret;
}

/*the queue work handle, store the modem reason to the exception file*/
static void log_modem_work_func(struct work_struct *work)
{
    struct work_data *work_data_self = container_of(work, struct work_data, log_modem_work);
    if(NULL == work_data_self)
    {
        pr_err("[log_modem_reset]work_data_self is NULL!\n");
        return;
    }

    store_exception(DATA_LOG_DIR, MODEM_EXCEPTION, work_data_self->reset_reason, strlen(work_data_self->reset_reason));
    if(g_work_data->crash_log_valid){
        store_exception(DATA_LOG_DIR, MODEM_CRASH_LOG, work_data_self->crash_log, sizeof(work_data_self->crash_log));
        g_work_data->crash_log_valid = false;
    }

    if(g_work_data->f3_trace_log_valid){
        store_exception(DATA_LOG_DIR, MODEM_F3_TRACE, work_data_self->f3_trace_log, sizeof(work_data_self->f3_trace_log));
        g_work_data->f3_trace_log_valid = false;
    }

    pr_info("[log_modem_reset]log_modem_reset_work after write exception inode work_data_self->reset_reason=%s \n",work_data_self->reset_reason);
    g_work_data->reset_reason[0] = '\0';
}

static int insert_modem_log_queue(void)
{
    if(NULL == g_work_data->log_modem_work_queue){
        pr_err("[log_modem_reset]log_modem_reset_work_queue is NULL!\n");
        return;
    }

    pr_info("[log_modem_reset]modem reset reason inserted the log_modem_reset_queue \n");
    queue_work(g_work_data->log_modem_work_queue, &(g_work_data->log_modem_work));
    return;
}

static void save_modem_err_log(int type)
{
    u32 size;
    char *modem_log;

    modem_log = smem_get_entry_no_rlock(type, &size, 0,
                    SMEM_ANY_HOST_FLAG);
    if (!modem_log || !size) {
        pr_err("[log_modem_reset]log_modem_log %d failure reason: (unknown, smem_get_entry_no_rlock failed).\n", type);
        return;
    }

    if (!modem_log[0]) {
        pr_err("[log_modem_reset]log_modem_log %d failure reason: (unknown, empty string found).\n", type);
        return;
    }

    if(SMEM_ERR_F3_TRACE_LOG == type){
        memcpy(g_work_data->f3_trace_log, modem_log, DIAG_F3_TRACE_BUFFER_SIZE - 1);
        g_work_data->f3_trace_log_valid = true;
    }
	
	if(SMEM_ERR_CRASH_LOG == type){
        memcpy(g_work_data->crash_log, modem_log, ERR_DATA_MAX_SIZE - 1);
        g_work_data->crash_log_valid = true;
    }

    modem_log[0] = '\0';
    wmb();
}

void save_modem_reset_log(char reason[])
{
	int is_workqueue_pending = 0;

	if(strstr(reason, HUAWEI_STR) || strstr(reason, HUAWEI_REQUEST_STR)){
        pr_err("reset modem subsystem by huawei\n");
        subsystem_restart_requested = 1;
		return;
    }

	is_workqueue_pending = work_pending(&g_work_data->log_modem_work);
	if(is_workqueue_pending)
	{
		pr_err("log modem reset work queue is pending, ignore current ones\n");
		return;
	}
	
    strncpy(g_work_data->reset_reason,reason,SSR_REASON_LEN - 1);
	
	save_modem_err_log(SMEM_ERR_F3_TRACE_LOG);
    save_modem_err_log(SMEM_ERR_CRASH_LOG);

	insert_modem_log_queue();
	pr_info("[log_modem_reset]put done \n");
    return 0;
}


/*creat the queue that log the modem reset reason*/
int create_modem_log_queue(void)
{
    int error = 0;
	int work_data_size =0;

	work_data_size = sizeof(struct work_data);

    g_work_data = kzalloc(work_data_size,GFP_KERNEL);
    if (NULL == g_work_data) {
        error = -ENOMEM;
        pr_err("[log_modem_reset]work_data_temp is NULL, Don't log this!\n");
        return error;
    }

	memset(g_work_data, 0, work_data_size);

    INIT_WORK(&(g_work_data->log_modem_work), log_modem_work_func);

    g_work_data->log_modem_work_queue = create_singlethread_workqueue("log_modem_reset");
    if (NULL == g_work_data->log_modem_work_queue ) {
        error = -ENOMEM;
        pr_err("[log_modem_reset]log modem reset queue created failed!\n");
		kfree(g_work_data);
        return error;
    }

    pr_info("[log_modem_reset]log modem reset queue created success \n");
    return error;
}

void destroy_modem_log_queue(void)
{
    if(g_work_data)
    {
         kfree(g_work_data);
		 g_work_data = NULL;
    }
}
