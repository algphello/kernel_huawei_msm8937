#ifdef CONFIG_HUAWEI_WIFI

#include "vos_types.h"
#include "vos_trace.h"
#include "vos_status.h"
#include <linux/wireless.h>
#include <net/cfg80211.h>
#include <linux/inetdevice.h>
#include <net/addrconf.h>
#include <linux/rtnetlink.h>
//#include "sapApi.h"
#include <linux/semaphore.h>
#include <linux/ctype.h>


#define MAC_ADDRESS_FILE "/data/misc/wifi/macwifi"
#define WLAN_VALID_SIZE      17
#define NV_WLAN_VALID_SIZE   12
#define WLAN_MAC_LEN         6
#define NULL    0


static int char2_byte( char* strori, char* outbuf )
{
	int i = 0;
	int temp = 0;
	int sum = 0;
	char *ptemp;
	char tempchar[20]={0};

	ptemp = tempchar;

	for (i = 0; i< WLAN_VALID_SIZE;i++){
		if(strori[i]!=':'){
			*ptemp = strori[i];
			 ptemp++;
		}
	}

	for( i = 0; i < NV_WLAN_VALID_SIZE; i++ ){

		switch (tempchar[i]) {
			case '0' ... '9':
				temp = tempchar[i] - '0';
				break;
			case 'a' ... 'f':
				temp = tempchar[i] - 'a' + 10;
				break;
			case 'A' ... 'F':
				temp = tempchar[i] - 'A' + 10;
				break;
			default:
				return 0;
		}
		sum += temp;
		if( i % 2 == 0 ){
			outbuf[i/2] |= temp << 4;
		}
		else{
			outbuf[i/2] |= temp;
		}
	}
	return sum;
}

int wcn_get_mac_address(unsigned char *buf)
{
	struct file* filp = NULL;
	mm_segment_t old_fs;
	int result = VOS_STATUS_E_FAILURE;
	int sum = 0;
	char buf1[20] = {0};
	char buf2[6] = {0};
    pr_err("enter  read_from_mac_file: \n");

	if (NULL == buf) {
		pr_err("buf is NULL");
		return VOS_STATUS_E_FAILURE;
	}

	filp = filp_open(MAC_ADDRESS_FILE, O_RDONLY,0);
	if(IS_ERR(filp)){
		pr_err("open mac address file error\n");
		return VOS_STATUS_E_FAILURE;
	}

	old_fs = get_fs();
	set_fs(KERNEL_DS);
	filp->f_pos = 0;
	result = vfs_read(filp,buf1,WLAN_VALID_SIZE,&filp->f_pos);
	if(WLAN_VALID_SIZE != result){
		pr_err("read mac address file error\n");
		set_fs(old_fs);
		filp_close(filp,NULL);
		return VOS_STATUS_E_FAILURE;
	}

	sum = char2_byte(buf1,buf2);
	if (0 != sum){
		pr_err("get MAC from file: mac=%02x:%02x:**:**:%02x:%02x\n",buf2[0],buf2[1],buf2[4],buf2[5]);
		memcpy(buf,buf2,WLAN_MAC_LEN);
	}else{
		set_fs(old_fs);
		filp_close(filp,NULL);
		return VOS_STATUS_E_FAILURE;
	}

	set_fs(old_fs);
	filp_close(filp,NULL);

	return VOS_STATUS_SUCCESS;
}

EXPORT_SYMBOL(wcn_get_mac_address);
/*EXPORT_SYMBOL(wcn_get_mac_address);»áÒýÆð±àÒë¶Î´íÎó*/

#endif

