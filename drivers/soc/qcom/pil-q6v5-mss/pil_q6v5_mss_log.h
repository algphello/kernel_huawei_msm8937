/*
 * Copyright (C) 2013 Huawei Device Co.Ltd
 * License terms: GNU General Public License (GPL) version 2
 *
 */

#ifndef PIL_Q6V5_MSS_LOG
#define PIL_Q6V5_MSS_LOG

void save_modem_reset_log(char reason[]);
int create_modem_log_queue(void);
void destroy_modem_log_queue(void);

#endif
