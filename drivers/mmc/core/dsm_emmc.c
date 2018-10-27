/*
 *  linux/drivers/mmc/core/dsm_emmc.c
 *
 *  Copyright (C) 2003 Russell King, All Rights Reserved.
 *  Copyright 2007 Pierre Ossman
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  MMC dsm_emmc/driver model support.
 */
#include <linux/blkdev.h>

#include <linux/mmc/card.h>
#include <linux/mmc/host.h>
#include <linux/mmc/dsm_emmc.h>

#include "../card/queue.h"
#include "../host/sdhci.h"

#include "mmc_ops.h"

/*device index, 0: for emmc, 1: for SDcard*/
u64 device_index = 0;
char * mmc_dev_name="mmcblk0";
struct dsm_dev dsm_emmc = {
	.name = "dsm_emmc",
	.fops = NULL,
	.buff_size = EMMC_DSM_BUFFER_SIZE,
};
struct dsm_client *emmc_dclient = NULL;

/*the buffer which transffering to device radar*/
struct emmc_dsm_log g_emmc_dsm_log;

EXPORT_SYMBOL(emmc_dclient);

unsigned int emmc_dsm_real_upload_size=0;
static unsigned int g_last_msg_code=0; /*last sent error code*/
static unsigned int g_last_msg_count=0; /*last sent the code count*/
#define ERR_MAX_COUNT 10
/*
 * first send the err msg to /dev/exception node.
 * if it produces lots of reduplicated msg, will just record the times
 * for every error, it's better to set max times
 * @code: error number
 * @err_msg: error message
 * @return: 0:don't report, 1: report
 */
static int dsm_emmc_process_log(int code, char *err_msg)
{
	int ret=0;
	/*the MAX times of erevy err code*/
	static int emmc_pre_eol_max_count = ERR_MAX_COUNT;
	static int emmc_life_time_err_max_count = ERR_MAX_COUNT;
#ifdef CONFIG_HUAWEI_EMMC_DSM_TIMES_LIMIT
	static int system_w_err_max_count = ERR_MAX_COUNT;
	static int erase_err_max_count = ERR_MAX_COUNT;
	static int send_cxd_err_max_count = ERR_MAX_COUNT;
	static int emmc_read_err_max_count = ERR_MAX_COUNT;
	static int emmc_write_err_max_count = ERR_MAX_COUNT;
	static int emmc_tuning_err_max_count = ERR_MAX_COUNT;
	static int emmc_set_width_err_max_count = ERR_MAX_COUNT;
	static int vdet_err_max_count = ERR_MAX_COUNT;
	static int emmc_packed_command_err_max_count = ERR_MAX_COUNT;
	static char emmc_rsp_err_max_count = ERR_MAX_COUNT;
	static char emmc_host_timeout_max_count = ERR_MAX_COUNT;
	static char emmc_host_err_max_count = ERR_MAX_COUNT;
	static char emmc_urgent_bkops_max_count = ERR_MAX_COUNT;
	static char emmc_dyncap_needed_max_count = ERR_MAX_COUNT;
	static char emmc_syspool_exhausted_max_count = ERR_MAX_COUNT;
	static char emmc_cache_err_max_count = ERR_MAX_COUNT;
	static char emmc_cache_timeout_max_count = ERR_MAX_COUNT;
#endif

	/*filter: if it has the same msg code with last, record err code&count*/
	if (g_last_msg_code == code) {
		ret = 0;
		g_last_msg_count++;
	} else {
		g_last_msg_code = code;
		g_last_msg_count = 0;
		ret = 1;
	}

	/*restrict count of every error, note:deplicated msg donesn't its count*/
	if(1 == ret){
		switch (code){
			case DSM_EMMC_LIFE_TIME_EST_ERR:
				if(0 < emmc_life_time_err_max_count){
					emmc_life_time_err_max_count--;
					ret = 1;
				}else{
					ret = 0;
				}
				break;
			case DSM_EMMC_PRE_EOL_INFO_ERR:
				if(0 < emmc_pre_eol_max_count){
					emmc_pre_eol_max_count--;
					ret = 1;
				}else{
					ret = 0;
				}
				break;

#ifdef CONFIG_HUAWEI_EMMC_DSM_TIMES_LIMIT
			case DSM_SYSTEM_W_ERR:
				if(0 < system_w_err_max_count){
					system_w_err_max_count--;
					ret = 1;
				}else{
					ret = 0;
				}
				break;
			case DSM_EMMC_ERASE_ERR:
				if(0 < erase_err_max_count){
					erase_err_max_count--;
					ret = 1;
				}else{
					ret = 0;
				}
				break;
			case DSM_EMMC_SEND_CXD_ERR:
				if(0 < send_cxd_err_max_count){
					send_cxd_err_max_count--;
					ret = 1;
				}else{
					ret = 0;
				}
				break;
			case DSM_EMMC_READ_ERR:
				if(0 < emmc_read_err_max_count){
					emmc_read_err_max_count--;
					ret = 1;
				}else{
					ret = 0;
				}
				break;
			case DSM_EMMC_WRITE_ERR:
				if(0 < emmc_write_err_max_count){
					emmc_write_err_max_count--;
					ret = 1;
				}else{
					ret = 0;
				}
				break;
			case DSM_EMMC_SET_BUS_WIDTH_ERR:
				if(0 < emmc_set_width_err_max_count){
					emmc_set_width_err_max_count--;
					ret = 1;
				}else{
					ret = 0;
				}
				break;
			case DSM_EMMC_VDET_ERR:
				if(0 < vdet_err_max_count){
					vdet_err_max_count--;
					ret = 1;
				}else{
					ret = 0;
				}
				break;

			case DSM_EMMC_TUNING_ERROR:
				if(0 < emmc_tuning_err_max_count){
					emmc_tuning_err_max_count--;
					ret = 1;
				}else{
					ret = 0;
				}
				break;
			case DSM_EMMC_PACKED_FAILURE:
				if(0 < emmc_packed_command_err_max_count){
					emmc_packed_command_err_max_count--;
					ret = 1;
				}else{
					ret = 0;
				}
				break;
			case DSM_EMMC_RSP_ERR:
				if(0 < emmc_rsp_err_max_count){
					emmc_rsp_err_max_count--;
					ret = 1;
				}else{
					ret = 0;
				}
				break;
			case DSM_EMMC_HOST_TIMEOUT_ERR:
				if(0 < emmc_host_timeout_max_count){
					emmc_host_timeout_max_count--;
					ret = 1;
				}else{
					ret = 0;
				}
				break;
			case DSM_EMMC_HOST_ERR:
				if(0 < emmc_host_err_max_count){
					emmc_host_err_max_count--;
					ret = 1;
				}else{
					ret = 0;
				}
				break;
			case DSM_EMMC_URGENT_BKOPS:
				if(0 < emmc_urgent_bkops_max_count){
					emmc_urgent_bkops_max_count--;
					ret = 1;
				}else{
					ret = 0;
				}
				break;
			case DSM_EMMC_DYNCAP_NEEDED:
				if(0 < emmc_dyncap_needed_max_count){
					emmc_dyncap_needed_max_count--;
					ret = 1;
				}else{
					ret = 0;
				}
				break;
			case DSM_EMMC_SYSPOOL_EXHAUSTED:
				if(0 < emmc_syspool_exhausted_max_count){
					emmc_syspool_exhausted_max_count--;
					ret = 1;
				}else{
					ret = 0;
				}
				break;
			case DSM_EMMC_CACHE_TIMEOUT:
				if(0 < emmc_cache_timeout_max_count){
					emmc_cache_timeout_max_count--;
					ret = 1;
				}else{
					ret = 0;
				}
				break;
			case DSM_EMMC_CACHE_ERR:
				if(0 < emmc_cache_err_max_count){
					emmc_cache_err_max_count--;
					ret = 1;
				}else{
					ret = 0;
				}
				break;
#endif
			default:
				ret = 0;
				break;
		}
	}

	return ret;
}

/*
 * Put error message into buffer.
 * @code: error number
 * @err_msg: error message
 * @return: 0:no buffer to report, 1: report
 */
int dsm_emmc_get_log(void *card, int code, char *err_msg)
{
	int ret = 0;
	int buff_size = sizeof(g_emmc_dsm_log.emmc_dsm_log);
	char *dsm_log_buff = g_emmc_dsm_log.emmc_dsm_log;
	struct mmc_card * card_dev=(struct mmc_card * )card;
	unsigned int last_msg_code = g_last_msg_code;
	unsigned int last_msg_count = g_last_msg_count;

	int i=1;
	u8 *ext_csd=NULL;

	if(dsm_emmc_process_log(code, err_msg)){
		/*clear global buffer*/
		memset(g_emmc_dsm_log.emmc_dsm_log,0,buff_size);
		/*print duplicated code and its count */
		if((0 < last_msg_count) && (0 == g_last_msg_count)){
			ret = snprintf(dsm_log_buff,buff_size,"last Err num: %d, the times: %d\n",last_msg_code, last_msg_count + 1);
			dsm_log_buff += ret;
			buff_size -= ret;
			last_msg_code = 0;
			last_msg_count = 0;
		}

		ret = snprintf(dsm_log_buff,buff_size,"Err num: %d, %s\n",code, err_msg);
		dsm_log_buff += ret;
		buff_size -= ret;
		pr_err("Err num: %d, %s\n",code, err_msg);

		if(NULL != card_dev){
			/*print card CID info*/
			if(sizeof(struct mmc_cid) < buff_size){
				ret = snprintf(dsm_log_buff,buff_size,
					"Card's cid:%08x%08x%08x%08x\n\n", card_dev->raw_cid[0], card_dev->raw_cid[1],
					card_dev->raw_cid[2], card_dev->raw_cid[3]);
				dsm_log_buff += ret;
				buff_size -= ret;
				pr_err("Card's cid:%08x%08x%08x%08x\n\n", card_dev->raw_cid[0], card_dev->raw_cid[1],
					card_dev->raw_cid[2], card_dev->raw_cid[3]);
			}else{
				printk(KERN_ERR "%s:g_emmc_dsm_log Buff size is not enough\n", __FUNCTION__);
				printk(KERN_ERR "%s:eMMC error message is: %s\n", __FUNCTION__, err_msg);
			}

			/*print card ios info*/
			if(sizeof(card_dev->host->ios)< buff_size){
				if(NULL != card_dev->host){
					ret = snprintf(dsm_log_buff,buff_size,
						"Card's ios.clock:%uHz, ios.old_rate:%uHz, ios.power_mode:%u, ios.timing:%u, ios.bus_mode:%u, ios.bus_width:%u\n",
						card_dev->host->ios.clock, card_dev->host->ios.old_rate, card_dev->host->ios.power_mode, card_dev->host->ios.timing,
						card_dev->host->ios.bus_mode, card_dev->host->ios.bus_width);
					dsm_log_buff += ret;
					buff_size -= ret;
					pr_err("Card's ios.clock:%uHz, ios.old_rate:%uHz, ios.power_mode:%u, ios.timing:%u, ios.bus_mode:%u, ios.bus_width:%u\n",
						card_dev->host->ios.clock, card_dev->host->ios.old_rate, card_dev->host->ios.power_mode, card_dev->host->ios.timing,
						card_dev->host->ios.bus_mode, card_dev->host->ios.bus_width);
				}
			}else{
				printk(KERN_ERR "%s:g_emmc_dsm_log Buff size is not enough\n", __FUNCTION__);
				printk(KERN_ERR "%s:eMMC error message is: %s\n", __FUNCTION__, err_msg);
			}

			/*print card ext_csd info*/
			if(EMMC_EXT_CSD_LENGHT < buff_size){
				if(NULL != card_dev->cached_ext_csd){
					ret = snprintf(dsm_log_buff,buff_size,"eMMC ext_csd(Note: just static slice data is believable):\n");
					dsm_log_buff += ret;
					buff_size -= ret;

					ext_csd = card_dev->cached_ext_csd;
					for (i = 0; i < EMMC_EXT_CSD_LENGHT; i++){
						ret = snprintf(dsm_log_buff,buff_size,"%02x", ext_csd[i]);
						dsm_log_buff += ret;
						buff_size -= ret;
					}
					ret = snprintf(dsm_log_buff,buff_size,"\n\n");
					dsm_log_buff += ret;
					buff_size -= ret;
				}
			}else{
				printk(KERN_ERR "%s:g_emmc_dsm_log Buff size is not enough\n", __FUNCTION__);
				printk(KERN_ERR "%s:eMMC error message is: %s\n", __FUNCTION__, err_msg);
			}
		}
		/*get size of used buffer*/
		emmc_dsm_real_upload_size = sizeof(g_emmc_dsm_log.emmc_dsm_log) - buff_size;

		pr_debug("DSM_DEBUG %s\n",g_emmc_dsm_log.emmc_dsm_log);

		return 1;
	}else{
		printk("%s:Err num: %d, %s\n",__FUNCTION__, code, err_msg);
		if(NULL != card_dev){
			pr_err("Card's cid:%08x%08x%08x%08x\n\n", card_dev->raw_cid[0], card_dev->raw_cid[1],
					card_dev->raw_cid[2], card_dev->raw_cid[3]);
			pr_err("Card's ios.clock:%uHz, ios.old_rate:%uHz, ios.power_mode:%u, ios.timing:%u, ios.bus_mode:%u, ios.bus_width:%u\n",
					card_dev->host->ios.clock, card_dev->host->ios.old_rate, card_dev->host->ios.power_mode, card_dev->host->ios.timing,
					card_dev->host->ios.bus_mode, card_dev->host->ios.bus_width);
		}
	}

	return 0;
}
EXPORT_SYMBOL(dsm_emmc_get_log);

#define MMC_RSP_R1_ERROR_MASK (R1_OUT_OF_RANGE | R1_ADDRESS_ERROR | R1_BLOCK_LEN_ERROR | R1_ERASE_SEQ_ERROR | \
								  R1_ERASE_PARAM | R1_WP_VIOLATION | R1_LOCK_UNLOCK_FAILED | \
								  R1_COM_CRC_ERROR | R1_ILLEGAL_COMMAND | R1_CARD_ECC_FAILED | R1_CC_ERROR | \
								  R1_ERROR | R1_CID_CSD_OVERWRITE | \
								  R1_WP_ERASE_SKIP | R1_ERASE_RESET | \
								  R1_SWITCH_ERROR)

#define MMC_RSP_R1_ERROR_NON_CRC_MASK (MMC_RSP_R1_ERROR_MASK & (~(R1_COM_CRC_ERROR | R1_ILLEGAL_COMMAND)))

static inline int mmc_dsm_request_response_error_filter(struct mmc_request *mrq) {
	return (((mrq)->cmd->opcode == MMC_SEND_TUNING_BLOCK_HS200) ||
			(((mrq)->cmd->opcode == MMC_SWITCH) && ((mrq)->cmd->resp[0] & (R1_COM_CRC_ERROR | R1_ILLEGAL_COMMAND)) && (((mrq)->cmd->resp[0] & MMC_RSP_R1_ERROR_NON_CRC_MASK) == 0)));
}

static mmc_dsm_request_response_error_check(struct mmc_host *host, struct mmc_request *mrq, void (*mmc_wait_data_done)(struct mmc_request *)) {
	int do_event = 0;
	if (mrq->sbc) {
		if (mrq->sbc->resp[0] & (MMC_RSP_R1_ERROR_MASK | R1_EXCEPTION_EVENT)) {
			if (host->index == device_index) {
				if (mrq->sbc->resp[0] & MMC_RSP_R1_ERROR_MASK) {
					struct mmc_card *card;
					card = host->card;
					DSM_EMMC_LOG(card, DSM_EMMC_RSP_ERR,
						"cmd: %d, requesting status %#x\n",
						mrq->sbc->opcode, mrq->sbc->resp[0]);
				}
				do_event = mrq->sbc->resp[0] & R1_EXCEPTION_EVENT;
			}
		}
	}
	if (mrq->cmd && ((mmc_resp_type(mrq->cmd) == MMC_RSP_R1) || (mmc_resp_type(mrq->cmd) == MMC_RSP_R1B))) {
		if (mrq->cmd->resp[0] & (MMC_RSP_R1_ERROR_MASK | R1_EXCEPTION_EVENT)) {
			if (host->index == device_index) {
				if ((mrq->cmd->resp[0] & MMC_RSP_R1_ERROR_MASK) && !mmc_dsm_request_response_error_filter(mrq)) {
					struct mmc_card *card;
					card = host->card;
					DSM_EMMC_LOG(card, DSM_EMMC_RSP_ERR,
						"cmd: %d, requesting status %#x\n",
						mrq->cmd->opcode, mrq->cmd->resp[0]);
				}
				do_event = mrq->cmd->resp[0] & R1_EXCEPTION_EVENT;
			}
		}
	}
	if (mrq->stop) {
		if (mrq->stop->resp[0] & (MMC_RSP_R1_ERROR_MASK | R1_EXCEPTION_EVENT)) {
			if (host->index == 0) {
				if (mrq->stop->resp[0] & MMC_RSP_R1_ERROR_MASK) {
					struct mmc_card *card;
					card = host->card;
					DSM_EMMC_LOG(card, DSM_EMMC_RSP_ERR,
						"cmd: %d, requesting status %#x\n",
						mrq->stop->opcode, mrq->stop->resp[0]);
				}
				do_event = mrq->stop->resp[0] & R1_EXCEPTION_EVENT;
			}
		}
	}

	if (unlikely(do_event)) {
		if(mrq->done == mmc_wait_data_done) {
			struct mmc_queue_req *mq_mrq = container_of(mrq, struct mmc_queue_req, brq.mrq);
			do_event = !mmc_packed_cmd(mq_mrq->cmd_type);
		} else {
			do_event = 0;
		}
		if (do_event) {
			int err;
			u8 *ext_csd;
			ext_csd = kzalloc(512, GFP_KERNEL);
			if (!ext_csd)
				return;
			err = mmc_send_ext_csd(host->card, ext_csd);
			if (err)
				goto free;
			if(ext_csd[EXT_CSD_EXP_EVENTS_STATUS] &
				EXT_CSD_DYNCAP_NEEDED) {
				DSM_EMMC_LOG(NULL, DSM_EMMC_DYNCAP_NEEDED,
					"DYNCAP_NEEDED [58]: %d, the device may degrade in performance and eventually become non-functional\n",
					ext_csd[58]);
			}
			if(ext_csd[EXT_CSD_EXP_EVENTS_STATUS] &
				EXT_CSD_SYSPOOL_EXHAUSTED) {
				DSM_EMMC_LOG(NULL, DSM_EMMC_SYSPOOL_EXHAUSTED,
					"SYSPOOL_EXHAUSTED, System resources pool exhausted\n");
			}
free:
			kfree(ext_csd);
		}
	}

}

/*
	In order to send dsm of emmc, you need to insert the following function into
	the specified function of the specified file.
*/
void emmc_get_life_time(struct mmc_card *card, u8 *ext_csd)
{
	/* eMMC v5.0 or later */
	if(!strcmp(mmc_hostname(card->host), "mmc0")){
		if (card->ext_csd.rev >= 7) {
			card->ext_csd.pre_eol_info = ext_csd[EXT_CSD_PRE_EOL_INFO];
			card->ext_csd.device_life_time_est_typ_a = ext_csd[EXT_CSD_DEVICE_LIFE_TIME_EST_TYP_A];
			card->ext_csd.device_life_time_est_typ_b = ext_csd[EXT_CSD_DEVICE_LIFE_TIME_EST_TYP_B];
		}
	}
}

void emmc_life_time_dsm(struct mmc_card *card)
{
	/* eMMC v5.0 or later */
	if(!strcmp(mmc_hostname(card->host), "mmc0")){
		if (card->ext_csd.rev >= 7) {
			if( card->ext_csd.device_life_time_est_typ_a >= DEVICE_LIFE_TRIGGER_LEVEL ||
				card->ext_csd.device_life_time_est_typ_b >= DEVICE_LIFE_TRIGGER_LEVEL)
			{
					DSM_EMMC_LOG(card, DSM_EMMC_LIFE_TIME_EST_ERR,
						"%s:eMMC life time has problem, device_life_time_est_typ_a[268]:%d, device_life_time_est_typ_b{269]:%d\n",
						__FUNCTION__, card->ext_csd.device_life_time_est_typ_a, card->ext_csd.device_life_time_est_typ_b);
			}

			if(card->ext_csd.pre_eol_info == EXT_CSD_PRE_EOL_INFO_WARNING ||
				card->ext_csd.pre_eol_info == EXT_CSD_PRE_EOL_INFO_URGENT)
			{
				DSM_EMMC_LOG(card, DSM_EMMC_PRE_EOL_INFO_ERR,
					"%s:eMMC average reserved blocks has problem, PRE_EOL_INFO[267]:%d\n", __FUNCTION__,
					card->ext_csd.pre_eol_info);
			}
		}
	}
}

void emmc_async_resp_err_dsm(struct mmc_host *host, void (*mmc_wait_data_done)(struct mmc_request *))
{
	if(host->card && mmc_card_mmc(host->card)){
		if(host->areq && host->areq->mrq){
		    mmc_dsm_request_response_error_check(host, host->areq->mrq, mmc_wait_data_done);
		}
	}
}

void emmc_sync_resp_err_dsm(struct mmc_host *host, struct mmc_request *mrq,
		void (*mmc_wait_data_done)(struct mmc_request *))
{
	if(host != NULL && host->card != NULL){
		if(mmc_card_mmc(host->card)){
			mmc_dsm_request_response_error_check(host, mrq, mmc_wait_data_done);
		}
	}
}

void emmc_erase_err_dsm(struct mmc_card *card, struct mmc_command *cmd, int err)
{
	if(card->host->index == device_index){
		DSM_EMMC_LOG(card, DSM_EMMC_ERASE_ERR,
			"%s:error %d requesting status %#x\n", __FUNCTION__,
			err, cmd->resp[0]);
	}
	else
		pr_err("error %d requesting status %#x\n",
			err, cmd->resp[0]);
}

void cache_flush_timeout_dsm(struct mmc_card *card)
{
	if(mmc_card_mmc(card)){
		DSM_EMMC_LOG(card, DSM_EMMC_CACHE_TIMEOUT,
			"%s: cache flush timeout\n",
			mmc_hostname(card->host));
	}
	else{
		pr_err("%s: cache flush timeout\n",
			mmc_hostname(card->host));
	}
}
void mmc_interrupt_hpi_failed_dsm(struct mmc_card *card, int err)
{
	if(mmc_card_mmc(card)){
		DSM_EMMC_LOG(card, DSM_EMMC_CACHE_ERR,
			"%s: mmc_interrupt_hpi() failed (%d)\n",
			mmc_hostname(card->host), err);
	}
	else{
		pr_err("%s: mmc_interrupt_hpi() failed (%d)\n",
			mmc_hostname(card->host), err);
	}
}
void cache_flush_err_dsm(struct mmc_card *card, int err)
{
	if(mmc_card_mmc(card)){
		DSM_EMMC_LOG(card, DSM_EMMC_CACHE_ERR,
			"%s: cache flush error %d\n",
			mmc_hostname(card->host), err);
	}
	else{
		pr_err("%s: cache flush error %d\n",
				mmc_hostname(card->host), err);
	}
}

void other_excep_event_dsm(u8 *ext_csd, struct mmc_card *card, struct request *req)
{
	if(ext_csd[EXT_CSD_EXP_EVENTS_STATUS] &
		EXT_CSD_DYNCAP_NEEDED) {
		if(mmc_card_mmc(card)){
			DSM_EMMC_LOG(card, DSM_EMMC_DYNCAP_NEEDED,
				"%s: DYNCAP_NEEDED [58]: %d, the device may degrade in performance and eventually become non-functional\n",
				req->rq_disk->disk_name, ext_csd[58]);
		}
	}
	if(ext_csd[EXT_CSD_EXP_EVENTS_STATUS] &
		EXT_CSD_SYSPOOL_EXHAUSTED) {
		if(mmc_card_mmc(card)){
			DSM_EMMC_LOG(card, DSM_EMMC_SYSPOOL_EXHAUSTED,
				"%s: SYSPOOL_EXHAUSTED, System resources pool exhausted\n",
				req->rq_disk->disk_name);
		}
	}
}

void emmc_read_write_err_dsm(struct mmc_card *card, int is_read, enum mmc_blk_status status)
{
	if(card->host->index == device_index){
		if(is_read){
			DSM_EMMC_LOG(card, DSM_EMMC_READ_ERR,
				"%s: Read error, mmc blk status: %d \n", __FUNCTION__, status);
		}else{
			DSM_EMMC_LOG(card, DSM_EMMC_WRITE_ERR,
				"%s: Write error, mmc blk status: %d \n", __FUNCTION__, status);
		}
	}
}

void emmc_dsm_dclient_init(void)
{
	if (!emmc_dclient) {
		emmc_dclient = dsm_register_client(&dsm_emmc);
	}
	spin_lock_init(&g_emmc_dsm_log.lock);
}
