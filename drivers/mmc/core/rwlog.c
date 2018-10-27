/*
 *  linux/drivers/mmc/core/rwlog.c
 *
 *  Copyright (C) 2003 Russell King, All Rights Reserved.
 *  Copyright 2007 Pierre Ossman
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  MMC sysfs/driver model support.
 */
#include <linux/debugfs.h>

#include <linux/mmc/card.h>
#include <linux/mmc/host.h>
#include "rwlog.h"

static struct dentry *dentry_mmclog;
u64 rwlog_enable_flag = 0;   /* 0 : Disable , 1: Enable */
u64 rwlog_index = 0;     /* device index, 0: for emmc */
int mmc_debug_mask = 0;

static unsigned long cmdq_read_blocks = 0;
static unsigned long cmdq_write_blocks = 0;

static int rwlog_enable_set(void *data, u64 val)
{
	rwlog_enable_flag = val;
	return 0;
}
static int rwlog_enable_get(void *data, u64 *val)
{
	*val = rwlog_enable_flag;
	return 0;
}
static int rwlog_index_set(void *data, u64 val)
{
	rwlog_index = val;
	return 0;
}
static int rwlog_index_get(void *data, u64 *val)
{
	*val = rwlog_index;
	return 0;
}
static int debug_mask_set(void *data, u64 val)
{
	mmc_debug_mask = (int)val;
	return 0;
}
static int debug_mask_get(void *data, u64 *val)
{
	*val = (u64)mmc_debug_mask;
	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(rwlog_enable_fops,rwlog_enable_get, rwlog_enable_set, "%llu\n");
DEFINE_SIMPLE_ATTRIBUTE(rwlog_index_fops,rwlog_index_get, rwlog_index_set, "%llu\n");
DEFINE_SIMPLE_ATTRIBUTE(debug_mask_fops,debug_mask_get, debug_mask_set, "%llu\n");

void rwlog_init(void)
{
	dentry_mmclog = debugfs_create_dir("hw_mmclog", NULL);
	if(dentry_mmclog ) {
		debugfs_create_file("rwlog_enable", S_IFREG|S_IRWXU|S_IRGRP|S_IROTH,
			dentry_mmclog, NULL, &rwlog_enable_fops);
		debugfs_create_file("rwlog_index", S_IFREG|S_IRWXU|S_IRGRP|S_IROTH,
			dentry_mmclog, NULL, &rwlog_index_fops);
		debugfs_create_file("debug_mask", S_IFREG|S_IRWXU|S_IRGRP|S_IROTH,
			dentry_mmclog, NULL, &debug_mask_fops);
	}
}

void mmc_start_request_rwlog(struct mmc_host *host, struct mmc_request *mrq)
{
	if(1 == rwlog_enable_flag) {
		if(mrq->cmd->opcode == MMC_WRITE_MULTIPLE_BLOCK
			|| mrq->cmd->opcode == MMC_WRITE_BLOCK
			|| mrq->cmd->opcode == MMC_READ_MULTIPLE_BLOCK
			|| mrq->cmd->opcode == MMC_READ_SINGLE_BLOCK) {
			/* only mmc rw log is output */
			if(rwlog_index == host->index) {
				if (mrq->data) {
					printk("%s:cmd=%d,mrq->data.blocks=%d,index=%d,arg=%x\n",__func__,
					(int)mrq->cmd->opcode, mrq->data->blocks, host->index, mrq->cmd->arg);
				}
			}
		}
	}
}

void mmc_start_cmdq_request_rwlog(struct mmc_host *host, struct mmc_request *mrq)
{
	if(1 == rwlog_enable_flag) {
		if (mrq->data) {
			if (mrq->data->flags & MMC_DATA_READ) {
				cmdq_read_blocks += mrq->data->blocks;
				printk("%s-lifetime:     blk_addr %d blksz %d blocks %d flags %08x tsac %lu ms nsac %d (r blocks %ld)\n",
				mmc_hostname(host), mrq->cmdq_req->blk_addr, mrq->data->blksz,
				mrq->data->blocks, mrq->data->flags,
				mrq->data->timeout_ns / NSEC_PER_MSEC,
				mrq->data->timeout_clks, cmdq_read_blocks);
			}
			if (mrq->data->flags & MMC_DATA_WRITE) {
				cmdq_write_blocks += mrq->data->blocks;
				printk("%s-lifetime:     blk_addr %d blksz %d blocks %d flags %08x tsac %lu ms nsac %d (w blocks %ld)\n",
				mmc_hostname(host), mrq->cmdq_req->blk_addr, mrq->data->blksz,
				mrq->data->blocks, mrq->data->flags,
				mrq->data->timeout_ns / NSEC_PER_MSEC,
				mrq->data->timeout_clks, cmdq_write_blocks);
			}
		}
		if (mrq->cmdq_req && mrq->cmdq_req->cmdq_req_flags & DCMD && mrq->cmd) {
			if (mrq->cmd->opcode == MMC_SWITCH
				|| mrq->cmd->opcode == MMC_ERASE_GROUP_START
				|| mrq->cmd->opcode == MMC_ERASE_GROUP_END
				|| mrq->cmd->opcode == MMC_ERASE) {
				printk("%s-lifetime:     cmd=%d,index=%d,arg=%x \n",
				mmc_hostname(host), (int)mrq->cmd->opcode, host->index, mrq->cmd->arg);
			}
		}
	}
}

