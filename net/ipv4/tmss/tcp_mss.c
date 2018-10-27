/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Implementation of the Transmission Control Protocol(TCP).
 *
 */

#include <linux/module.h>
#include <linux/gfp.h>
#include <net/tcp.h>

#ifdef CONFIG_HUAWEI_MSS_AUTO_CHANGE
#define TCP_MSS_REDUCE_SIZE (200)
#define TCP_MSS_MIN_SIZE    (1200)
void tcp_reduce_mss(struct inet_connection_sock *icsk, struct sock *sk)
{
    struct tcp_sock *tp = NULL;
    if(icsk && sk) {
        tp = tcp_sk(sk);
        if(tp && tp->mss_cache > TCP_MSS_MIN_SIZE && tp->rx_opt.mss_clamp > TCP_MSS_MIN_SIZE) {
            tp->rx_opt.mss_clamp = tp->mss_cache - TCP_MSS_REDUCE_SIZE;
            tcp_sync_mss(sk, icsk->icsk_pmtu_cookie);
        }
    }
}
#endif
