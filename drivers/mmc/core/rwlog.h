/*
 *  linux/drivers/mmc/core/rwlog.h
 *
 *  Copyright (C) 2003 Russell King, All Rights Reserved.
 *  Copyright 2007 Pierre Ossman
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef _MMC_CORE_RWLOG_H
#define _MMC_CORE_RWLOG_H

extern u64 rwlog_enable_flag;
extern u64 rwlog_index;

void rwlog_init(void);
void mmc_start_request_rwlog(struct mmc_host *host, struct mmc_request *mrq);
void mmc_start_cmdq_request_rwlog(struct mmc_host *host, struct mmc_request *mrq);

#endif
