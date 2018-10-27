#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/device.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/semaphore.h>
#include <linux/uaccess.h>
#include <linux/miscdevice.h>
#include <sound/tas2560_smartamp.h>

/* Holds the Packet data required for processing */
struct tas_dsp_pkt {
	u8 slave_id;
	u8 book;
	u8 page;
	u8 offset;
	u8 data[TAS_PAYLOAD_SIZE * 4];
};

static int smartamp_params_ctrl(uint8_t *input, u8 dir, u8 count)
{
	u32 length = count / 4;
	u32 paramid = 0;
	u32 index;
	int ret = 0;
	int special_index = 0;
	struct tas_dsp_pkt *ppacket;

	ppacket = (struct tas_dsp_pkt*)kmalloc (sizeof(struct tas_dsp_pkt), GFP_KERNEL);
	if (!ppacket) {
		pr_err ("TAS2560-ALGO:%s pperror kmalloc failed!", __func__);
		return -ENOMEM;
	}

	memset(ppacket, 0, sizeof(struct tas_dsp_pkt));
	ret = copy_from_user (ppacket, input, sizeof(struct tas_dsp_pkt));
	if (ret){
		pr_err("TAS2560-ALGO:%s Error copying from user\n", __func__);
		kfree (ppacket);
		return -EFAULT;
	}

	index = (ppacket->page - 1) * 30 + (ppacket->offset - 8) / 4;
	special_index = AFE_SA_IS_SPL_IDX(index);
	pr_err("TAS2560-ALGO:%s index = %d", __func__, index);
	if (special_index == 0) {
		if ((index < 0 || index > MAX_DSP_PARAM_INDEX)) {
			pr_err("TAS2560-ALGO:%s invalid index !\n", __func__);
			kfree(ppacket);
			return -1;
		}
	}
	pr_err("TAS2560-ALGO:%s Valid Index. special = %s\n", __func__, special_index ? "Yes" : "No");

	if (ppacket->slave_id == SLAVE1 )
		paramid = (paramid | (index ) | (length << 16) | (1 << 24));
	else if (ppacket->slave_id == SLAVE2 )
		paramid = (paramid | (index ) | (length << 16) | (2 << 24));
	else
		pr_err("TAS2560-ALGO:%s Wrong slaveid = %x\n", __func__, ppacket->slave_id);

	/*
	 * Note: In any case calculated paramid should not match with
	 * AFE_PARAM_ID_ENABLE and AFE_PARAM_ID_SMARTAMP_DEFAULT
	 */
	if (paramid == CAPI_V2_SP_TX_CFG_1_PARAM_ID	||
		paramid == CAPI_V2_PARAM_ID_SP_TX_ENABLE ||
		paramid == CAPI_V2_SP_RX_CFG_1_PARAM_ID ||
		paramid == CAPI_V2_PARAM_ID_SP_RX_ENABLE )
	{
		pr_err("TAS2560-ALGO:%s %s Slave 0x%x params failed, paramid mismatch\n", __func__,
				dir == TAS_GET_PARAM ? "get" : "set", ppacket->slave_id);
		kfree(ppacket);
		return -1;
	}
              
	ret = afe_smartamp_algo_ctrl(ppacket->data, paramid,
			dir, length * 4, AFE_SMARTAMP_MODULE);
	if (ret)
		pr_err("TAS2560-ALGO:%s %s Slave 0x%x params failed from afe, ret=%x\n", __func__,
				dir == TAS_GET_PARAM ? "get" : "set", ppacket->slave_id, ret);
	else
		pr_info("TAS2560-ALGO:%s Algo control returned %d\n", __func__, ret);

	if (dir == TAS_GET_PARAM) {
		ret = copy_to_user(input, ppacket, sizeof(struct tas_dsp_pkt));
		if (ret) {
			pr_err("TAS2560-ALGO:%s Error copying to user after DSP", __func__);
			ret = -EFAULT;
		}
	}
 
	kfree(ppacket);
	return ret;
}

static int tas_calib_open(struct inode *inode, struct file *fd)
{
	return 0;
}

static ssize_t tas_calib_write(struct file *file,
		const char __user *buffer, size_t count, loff_t *offp)
{
	int rc = 0;
	rc = smartamp_params_ctrl((uint8_t *)buffer, TAS_SET_PARAM, count);
	return rc;
}

static ssize_t tas_calib_read(struct file *file, char __user *buffer,
		size_t count, loff_t *ptr)
{
	int rc;
	rc = smartamp_params_ctrl((uint8_t *)buffer, TAS_GET_PARAM, count);
	if (rc < 0)
		count = rc;
	return count;
}

static long tas_calib_ioctl(struct file *filp, uint cmd, ulong arg)
{
	return 0;
}

static int tas_calib_release(struct inode *inode, struct file *fd)
{
	return 0;
}

const struct file_operations tas_calib_fops = {
	.owner			= THIS_MODULE,
	.open			= tas_calib_open,
	.write			= tas_calib_write,
	.read			= tas_calib_read,
	.release		= tas_calib_release,
	.unlocked_ioctl	= tas_calib_ioctl,
};

static struct miscdevice tas_calib_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "tas_calib",
	.fops = &tas_calib_fops,
};

static int __init tas_calib_init(void)
{
	int rc;
	pr_info("TAS2560-ALGO:%s",__func__);
	rc = misc_register(&tas_calib_misc);
	if (rc)
		pr_err("TAS2560-ALGO:%s register calib misc failed\n", __func__);
	return rc;
}

static void __exit tas_calib_exit(void)
{
	misc_deregister(&tas_calib_misc);
}

module_init(tas_calib_init);
module_exit(tas_calib_exit);

MODULE_AUTHOR("Texas Intruments");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("DSP Interface Driver");
