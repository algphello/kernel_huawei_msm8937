/*
 *  linux/drivers/mmc/core/dsm_sdcard.c
 *
 *  Copyright (C) 2003 Russell King, All Rights Reserved.
 *  Copyright 2007 Pierre Ossman
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  MMC dsm_sdcard/driver model support.
 */

#include <linux/mmc/card.h>
#include <linux/mmc/host.h>
#include <linux/mmc/dsm_sdcard.h>

#include "sd_ops.h"

struct dsm_dev dsm_sdcard = {
	.name = "dsm_sdcard",
	.fops = NULL,
	.buff_size = 1024,
};
struct dsm_client *sdcard_dclient = NULL;
char g_dsm_log_sum[1024] = {0};

EXPORT_SYMBOL(sdcard_dclient);

u32 sd_manfid;
struct dsm_sdcard_cmd_log dsm_sdcard_cmd_logs[] =
{
	{"CMD8 : ",				0,		0},
	{"CMD55 : ",    		0,		0},
	{"ACMD41: ",			0,		0},
	{"CMD2_RESP0 : ",		0,		0},
	{"CMD2_RESP1 : ",		0,		0},
	{"CMD2_RESP2 : ",		0,		0},
	{"CMD2_RESP3 : ",		0,		0},
	{"CMD3 : ",				0,		0},
	{"CMD9_RESP0 : ",		0,		0},
	{"CMD9_RESP1 : ",		0,		0},
	{"CMD9_RESP2 : ",		0,		0},
	{"CMD9_RESP3 : ",		0,		0},
	{"CMD7 : ",				0,		0},
	{"Report Uevent : ",	0,		0},
};

/*
 * get the error cmd logs and previous cmds logs
*/
char *dsm_sdcard_get_log(int cmd,int err)
{
	int i;
	int ret = 0;
	int buff_size = sizeof(g_dsm_log_sum);
	char *dsm_log_buff = g_dsm_log_sum;

	memset(g_dsm_log_sum,0,buff_size);

	ret = snprintf(dsm_log_buff,buff_size,"Err : %d\n",err);
	dsm_log_buff += ret;
	buff_size -= ret;

	for(i = 0; i <= cmd; i++)
	{
		ret = snprintf(dsm_log_buff,buff_size,
		"%s%08x sdcard manufactory id is 0x%x\n",dsm_sdcard_cmd_logs[i].log,\
		dsm_sdcard_cmd_logs[i].value,dsm_sdcard_cmd_logs[i].manfid);
		if(ret > buff_size -1)
		{
			printk(KERN_ERR "Buff size is not enough\n");
			printk(KERN_ERR "%s",g_dsm_log_sum);
			return g_dsm_log_sum;
		}

		dsm_log_buff += ret;
		buff_size -= ret;
	}

	pr_debug("DSM_DEBUG %s",g_dsm_log_sum);

	return g_dsm_log_sum;

}
EXPORT_SYMBOL(dsm_sdcard_get_log);

/*
	In order to send dsm of sdcard, you need to insert the following function into 
	the specified function of the specified file.
*/

void sdcard_dsm_dclient_init(void)
{
	if (!sdcard_dclient) {
		sdcard_dclient = dsm_register_client(&dsm_sdcard);
	}
}

void sdcard_cmd9_resp_err_dsm(struct mmc_host *host, struct mmc_card *card, int err)
{
	int buff_len;
	char *log_buff;

	if(!strcmp(mmc_hostname(host), "mmc1"))
	{
		 dsm_sdcard_cmd_logs[DSM_SDCARD_CMD9_R0].value = card->raw_csd[0];
		 dsm_sdcard_cmd_logs[DSM_SDCARD_CMD9_R1].value = card->raw_csd[1];
		 dsm_sdcard_cmd_logs[DSM_SDCARD_CMD9_R2].value = card->raw_csd[2];
		 dsm_sdcard_cmd_logs[DSM_SDCARD_CMD9_R3].value = card->raw_csd[3];
	}

	if (err)
	{
		if(-ENOMEDIUM != err && -ETIMEDOUT != err
		&& !strcmp(mmc_hostname(host), "mmc1") && !dsm_client_ocuppy(sdcard_dclient))
		{
			log_buff = dsm_sdcard_get_log(DSM_SDCARD_CMD9_R3,err);
			buff_len = strlen(log_buff);
			dsm_client_copy(sdcard_dclient,log_buff,buff_len + 1);
			dsm_client_notify(sdcard_dclient, DSM_SDCARD_CMD9_RESP_ERR);
			pr_err("Err num: %d, %s\n",DSM_SDCARD_CMD9_RESP_ERR, "sdcard cmd9 response error.");
		}
	}
}

void sdcard_dsm_cmd_logs_init(struct mmc_host *host, u32 cid)
{
	int i=0;

	sd_manfid = cid>>24;
	if(!strcmp(mmc_hostname(host), "mmc1"))
	{
		for(i=0; i< DSM_SDCARD_CMD_MAX; i++)
		{
			dsm_sdcard_cmd_logs[i].manfid = sd_manfid;
		}
	}
}

void sdcard_dsm_cmd_logs_clear(struct mmc_host *host)
{
	int i;

	if(!strcmp(mmc_hostname(host), "mmc1"))
	{
		for(i=0; i< DSM_SDCARD_CMD_MAX; i++)
		{
			dsm_sdcard_cmd_logs[i].value = 0;
			dsm_sdcard_cmd_logs[i].manfid = 0;
		}
	}
}

void set_dsm_sdcard_cmd_log_value(struct mmc_card *card,
		struct mmc_host *host, u32 type, u32 value)
{
	if(NULL != card){
		if(MMC_TYPE_SD == card->type)
		{
			dsm_sdcard_cmd_logs[type].value = value;
		}
	}else if(NULL != host){
		if(!strcmp(mmc_hostname(host), "mmc1"))
		{
			dsm_sdcard_cmd_logs[type].value = value;
		}
	}else{}
}

void sdcard_no_uevent_report_dsm(struct mmc_card *card)
{
	int buff_len;
	char *log_buff;

	if(MMC_TYPE_SD == card->type && !dsm_client_ocuppy(sdcard_dclient))
	{
		log_buff = dsm_sdcard_get_log(DSM_SDCARD_REPORT_UEVENT,0);
		buff_len = strlen(log_buff);
		dsm_client_copy(sdcard_dclient,log_buff,buff_len + 1);
		dsm_client_notify(sdcard_dclient, DSM_SDCARD_NO_UEVENT_REPORT);
		pr_err("Err num: %d, %s\n",DSM_SDCARD_NO_UEVENT_REPORT, "sdcard add disk error.");
	}
}

void sdcard_cmd2_resp_err_dsm(struct mmc_host *host, struct mmc_command *cmd, int err)
{
	char *log_buff;
	int   buff_len;

	if(!strcmp(mmc_hostname(host), "mmc1"))
	{
		 dsm_sdcard_cmd_logs[DSM_SDCARD_CMD2_R0].value = cmd->resp[0];
		 dsm_sdcard_cmd_logs[DSM_SDCARD_CMD2_R1].value = cmd->resp[1];
		 dsm_sdcard_cmd_logs[DSM_SDCARD_CMD2_R2].value = cmd->resp[2];
		 dsm_sdcard_cmd_logs[DSM_SDCARD_CMD2_R3].value = cmd->resp[3];
	}

	if(err)
	{
		if(-ENOMEDIUM != err && -ETIMEDOUT != err
		&& !strcmp(mmc_hostname(host), "mmc1") && !dsm_client_ocuppy(sdcard_dclient))
		{
			log_buff = dsm_sdcard_get_log(DSM_SDCARD_CMD2_R3, err);
			buff_len = strlen(log_buff);
			dsm_client_copy(sdcard_dclient,log_buff,buff_len + 1);
			dsm_client_notify(sdcard_dclient, DSM_SDCARD_CMD2_RESP_ERR);
			pr_err("Err num: %d, %s\n",DSM_SDCARD_CMD2_RESP_ERR, "sdcard cmd2, get CID register error.");
		}
	}
}

void sdcard_cmd7_resp_err_dsm(struct mmc_host *host, struct mmc_command *cmd, int err)
{
	char *log_buff;
	int   buff_len;

	if(!strcmp(mmc_hostname(host), "mmc1"))
	{
		dsm_sdcard_cmd_logs[DSM_SDCARD_CMD7].value = cmd->resp[0];
	}

	if (err)
	{
		if(-ENOMEDIUM != err && -ETIMEDOUT != err
		&& !strcmp(mmc_hostname(host), "mmc1") && !dsm_client_ocuppy(sdcard_dclient))
		{
			log_buff = dsm_sdcard_get_log(DSM_SDCARD_CMD7,err);
			buff_len = strlen(log_buff);
			dsm_client_copy(sdcard_dclient,log_buff,buff_len + 1);
			dsm_client_notify(sdcard_dclient, DSM_SDCARD_CMD7_RESP_ERR);
			pr_err("Err num: %d, %s\n",DSM_SDCARD_CMD7_RESP_ERR, "sdcard cmd7, Select the card error, not for data transmission.");
		}
	}
}

int sdcard_cmd55_resp_err_dsm(struct mmc_host *host, struct mmc_command *cmd, int err)
{
	char *log_buff;
	int buff_len;

	if(!strcmp(mmc_hostname(host), "mmc1"))
	{
		 dsm_sdcard_cmd_logs[DSM_SDCARD_CMD55].value = cmd->resp[0];
	}
	if(!(cmd->resp[0] & R1_APP_CMD) ){
		err = -ENOMEDIUM;
	}

	if (err)
	{
		/* for sdcard using polling err = EILSEQ is not a really err.*/
		if(-ENOMEDIUM != err && -ETIMEDOUT != err && -EILSEQ != err
		&& !strcmp(mmc_hostname(host), "mmc1") && !dsm_client_ocuppy(sdcard_dclient))
		{
			log_buff = dsm_sdcard_get_log(DSM_SDCARD_CMD55,err);
			buff_len = strlen(log_buff);
			dsm_client_copy(sdcard_dclient,log_buff,buff_len + 1);
			dsm_client_notify(sdcard_dclient,DSM_SDCARD_CMD55_RESP_ERR);
			pr_err("Err num: %d, %s\n",DSM_SDCARD_CMD55_RESP_ERR, "sdcard cmd55 response error.");
		}
	}

	return err;
}

void sdcard_cmd41_resp_err_dsm(struct mmc_host *host, struct mmc_command *cmd, int err)
{
	int buff_len;
	char *log_buff;

	if(-ENOMEDIUM != err && -ETIMEDOUT != err
	&& !strcmp(mmc_hostname(host), "mmc1") && !dsm_client_ocuppy(sdcard_dclient))
	{

		dsm_sdcard_cmd_logs[DSM_SDCARD_ACMD41].value = cmd->resp[0];

		log_buff = dsm_sdcard_get_log(DSM_SDCARD_ACMD41,err);
		buff_len = strlen(log_buff);
		dsm_client_copy(sdcard_dclient,log_buff,buff_len + 1);
		dsm_client_notify(sdcard_dclient, DSM_SDCARD_ACMD41_RESP_ERR);
		pr_err("Err num: %d, %s\n",DSM_SDCARD_ACMD41_RESP_ERR, "sdcard cmd41, get CID reg error.");
	}
}

void sdcard_cmd3_resp_err_dsm(struct mmc_host *host, struct mmc_command *cmd, int err)
{
	int buff_len;
	char *log_buff;

	if(!strcmp(mmc_hostname(host), "mmc1"))
	{
		 dsm_sdcard_cmd_logs[DSM_SDCARD_CMD3].value = cmd->resp[0];
	}

	if (err)
	{
		if(-ENOMEDIUM != err && -ETIMEDOUT != err
		&& !strcmp(mmc_hostname(host), "mmc1") && !dsm_client_ocuppy(sdcard_dclient))
		{
			log_buff = dsm_sdcard_get_log(DSM_SDCARD_CMD3,err);
			buff_len = strlen(log_buff);
			dsm_client_copy(sdcard_dclient,log_buff,buff_len + 1);
			dsm_client_notify(sdcard_dclient, DSM_SDCARD_CMD3_RESP_ERR);
			pr_err("Err num: %d, %s\n",DSM_SDCARD_CMD3_RESP_ERR, "sdcard cmd3, get RCA address error");
		}
	}
}

void sdcard_cmd8_resp_err_dsm(struct mmc_host *host, struct mmc_command *cmd, int err)
{
	int buff_len;
	char *log_buff;

	if(!strcmp(mmc_hostname(host), "mmc1"))
	{
		dsm_sdcard_cmd_logs[DSM_SDCARD_CMD8].value = cmd->resp[0];
	}

	if (err)
	{
		/* for sdcard using polling err = EILSEQ is not a really err.*/
		if(-ENOMEDIUM != err && -ETIMEDOUT != err && -EILSEQ != err
		&& !strcmp(mmc_hostname(host), "mmc1") && !dsm_client_ocuppy(sdcard_dclient))

		{
			log_buff = dsm_sdcard_get_log(DSM_SDCARD_CMD8,err);
			buff_len = strlen(log_buff);
			dsm_client_copy(sdcard_dclient,log_buff,buff_len + 1);
			dsm_client_notify(sdcard_dclient, DSM_SDCARD_CMD8_RESP_ERR);
			pr_err("Err num: %d, %s\n",DSM_SDCARD_CMD8_RESP_ERR, "sdcard cmd8 response error");
		}
	}
}
