/* Copyright (c) 2009-2016, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#define pr_fmt(fmt) "%s:%d " fmt, __func__, __LINE__

#include <linux/module.h>
#include <linux/of_gpio.h>
#include "msm_flash.h"
#include "msm_cci.h"
#include "msm_camera_io_util.h"
#include "aw36413_lib.h"
#include "sy7806e_lib.h"

#undef CDBG
#define CDBG(fmt, args...) pr_err(fmt, ##args)

DEFINE_MSM_MUTEX(msm_flash_mutex);

static struct v4l2_file_operations msm_flash_v4l2_subdev_fops;

struct msm_flash_ctrl_t main_flash_ctrl;
struct msm_flash_ctrl_t sub_flash_ctrl;
static const struct of_device_id hw_flash_i2c_dt_match[] = {
	{.compatible = "hw,camera-flash"},
	{}
};

static const struct i2c_device_id hw_flash_i2c_id[] = {
	{"hw,camera-flash", (kernel_ulong_t)&main_flash_ctrl},
	{ }
};

static const struct of_device_id hw_sub_flash_i2c_dt_match[] = {
	{.compatible = "hw,sub-camera-flash"},
	{}
};

static const struct i2c_device_id hw_sub_flash_i2c_id[] = {
	{"hw,sub-camera-flash", (kernel_ulong_t)&sub_flash_ctrl},
	{ }
};
static struct i2c_flash_info_t i2c_device_table[] = {
	{
		.name = "aw36413",
		.slave_addr = 0xd6,
		.addr_type = MSM_CAMERA_I2C_BYTE_ADDR,
		.id_reg = 0x00,
		.expect_id = 0x36,
		.id_mask = 0x00,
		.max_current = 0x07,
		.flash_driver_type = FLASH_DRIVER_I2C_MAJOR,
	},
	{
		.name = "sy7806e",
		.slave_addr = 0xC6,
		.addr_type = MSM_CAMERA_I2C_BYTE_ADDR,
		.id_reg = 0x0C,
		.expect_id = 0x1C,
		.id_mask = 0x00,
		.max_current = 0x07,
		.flash_driver_type = FLASH_DRIVER_I2C_MAJOR,
	},
};
static struct i2c_flash_info_t i2c_device_sub_table[] = {
	{
		.name = "aw36413",
		.slave_addr = 0xd6,
		.addr_type = MSM_CAMERA_I2C_BYTE_ADDR,
		.id_reg = 0x00,
		.expect_id = 0x36,
		.id_mask = 0x00,
		.max_current = 0x07,
		.flash_driver_type = FLASH_DRIVER_I2C_MINOR,
	},
	{
		.name = "sy7806e",
		.slave_addr = 0xC6,
		.addr_type = MSM_CAMERA_I2C_BYTE_ADDR,
		.id_reg = 0x0C,
		.expect_id = 0x1C,
		.id_mask = 0x00,
		.max_current = 0x07,
		.flash_driver_type = FLASH_DRIVER_I2C_MINOR,
	},
};
enum msm_falsh_power_statue {
    MSM_CAMERA_LED_TORCH_POWER_NORMAL = 108,//stand for not limit flash
    MSM_CAMERA_LED_TORCH_POWER_LOW, //stand for limit flash
};
enum msm_flash_lock_status{
    MSM_FLASH_STATUS_UNLOCK,
    MSM_FLASH_STATUS_LOCKED,
};

#define  FLASH_LIGHT_TWO_ID  10 //MMI turn on 2 LED

static enum msm_flash_lock_status msm_flash_status = MSM_FLASH_STATUS_UNLOCK;

static struct msm_flash_table msm_i2c_flash_aw36413_table;
static struct msm_flash_table msm_i2c_flash_sy7806e_table;
static struct msm_flash_table msm_i2c_flash_sub_aw36413_table;
static struct msm_flash_table msm_i2c_flash_sub_sy7806e_table;

static struct msm_flash_table *flash_table[] = {
	&msm_i2c_flash_aw36413_table,
	&msm_i2c_flash_sy7806e_table,
	&msm_i2c_flash_sub_aw36413_table,
	&msm_i2c_flash_sub_sy7806e_table,
};

static struct msm_camera_i2c_fn_t msm_flash_qup_func_tbl = {
	.i2c_read = msm_camera_qup_i2c_read,
	.i2c_read_seq = msm_camera_qup_i2c_read_seq,
	.i2c_write = msm_camera_qup_i2c_write,
	.i2c_write_table = msm_camera_qup_i2c_write_table,
	.i2c_write_seq_table = msm_camera_qup_i2c_write_seq_table,
	.i2c_write_table_w_microdelay =
		msm_camera_qup_i2c_write_table_w_microdelay,
};

static void hw_led_torch_brightness_set(struct led_classdev *led_cdev,
				enum led_brightness value)
{
	struct msm_flash_func_t *func_tbl = NULL;
	struct msm_flash_ctrl_t *fctrl = &main_flash_ctrl;
	enum msm_flash_driver_type flash_driver_type = fctrl->flash_driver_type;

	pr_info("%s enter, value:%d,flash_driver_type:%d\n", __func__,value,flash_driver_type);

	func_tbl = fctrl->func_tbl;
	if(!func_tbl){
		pr_err("%s NULL func_tbl\n", __func__);
		return;
	}
	if (MSM_CAMERA_LED_TORCH_POWER_NORMAL == (enum msm_falsh_power_statue)value){
		msm_flash_status = MSM_FLASH_STATUS_UNLOCK;
		pr_info("power saving unlock the flash \n");
	}
	else if (MSM_CAMERA_LED_TORCH_POWER_LOW == (enum msm_falsh_power_statue)value){
		if (func_tbl->camera_flash_off)
			func_tbl->camera_flash_off(fctrl, NULL);
		if (func_tbl->camera_flash_release)
			func_tbl->camera_flash_release(fctrl);
		msm_flash_status = MSM_FLASH_STATUS_LOCKED;
	}
	else if (LED_OFF == value){
		if(func_tbl->camera_flash_off)
			func_tbl->camera_flash_off(fctrl, NULL);
		if (func_tbl->camera_flash_release)
			func_tbl->camera_flash_release(fctrl);
	}
	else if ((value > LED_OFF) && (value <= FLASH_LIGHT_TWO_ID)) {
		if (func_tbl->camera_flash_torch)
			func_tbl->camera_flash_torch(fctrl, NULL);
	} else {
		pr_err("error brightness level! \n");
	}
	CDBG("%s exit\n", __func__);
};

static void hw_sub_led_torch_brightness_set(struct led_classdev *led_cdev,
				enum led_brightness value)
{
	struct msm_flash_func_t *func_tbl = NULL;
	struct msm_flash_ctrl_t *fctrl = &sub_flash_ctrl;

	func_tbl = fctrl->func_tbl;
	if(!func_tbl){
		pr_err("%s NULL func_tbl\n", __func__);
		return;
	}
	if (MSM_CAMERA_LED_TORCH_POWER_NORMAL == (enum msm_falsh_power_statue)value){
		msm_flash_status = MSM_FLASH_STATUS_UNLOCK;
		pr_info("power saving unlock the flash \n");
	}
	else if (MSM_CAMERA_LED_TORCH_POWER_LOW == (enum msm_falsh_power_statue)value){
		if (func_tbl->camera_flash_off)
			func_tbl->camera_flash_off(fctrl, NULL);
		if (func_tbl->camera_flash_release)
			func_tbl->camera_flash_release(fctrl);
		msm_flash_status = MSM_FLASH_STATUS_LOCKED;
	}
	else if (LED_OFF == value){
		if(func_tbl->camera_flash_off)
			func_tbl->camera_flash_off(fctrl, NULL);
		if (func_tbl->camera_flash_release)
			func_tbl->camera_flash_release(fctrl);
	}
	else if ((value > LED_OFF) && (value <= FLASH_LIGHT_TWO_ID)) {
		if(func_tbl->camera_flash_torch)
			func_tbl->camera_flash_torch(fctrl, NULL);
	} else {
		pr_err("error brightness level! \n");
	}
	CDBG("%s exit\n", __func__);
};


static struct led_classdev hw_i2c_torch_led = {
	.name		= "torch-light0",
	.brightness_set	= hw_led_torch_brightness_set,
	.brightness	= LED_OFF,
};

static struct led_classdev hw_i2c_sub_torch_led = {
	.name		= "torch-light1",
	.brightness_set	= hw_sub_led_torch_brightness_set,
	.brightness	= LED_OFF,
};

static int32_t hw_i2c_torch_create_classdev(struct device *dev,
				void *data)
{
	int rc;
	CDBG("%s enter\n", __func__);
	hw_led_torch_brightness_set(&hw_i2c_torch_led, LED_OFF);
	rc = led_classdev_register(dev, &hw_i2c_torch_led);
	if (rc) {
		pr_err("Failed to register led dev. rc = %d\n", rc);
		return rc;
	}
	return 0;
};

static int32_t hw_i2c_sub_torch_create_classdev(struct device *dev,
				void *data)
{
	int rc;
	CDBG("%s enter\n", __func__);
	hw_sub_led_torch_brightness_set(&hw_i2c_sub_torch_led, LED_OFF);
	rc = led_classdev_register(dev, &hw_i2c_sub_torch_led);
	if (rc) {
		pr_err("Failed to register led dev. rc = %d\n", rc);
		return rc;
	}
	return 0;
};

static int32_t msm_flash_get_subdev_id(
	struct msm_flash_ctrl_t *flash_ctrl, void *arg)
{
	uint32_t *subdev_id = (uint32_t *)arg;
	CDBG("Enter\n");
	if (!subdev_id) {
		pr_err("failed\n");
		return -EINVAL;
	}
	if (flash_ctrl->flash_device_type == MSM_CAMERA_PLATFORM_DEVICE)
		*subdev_id = flash_ctrl->pdev->id;
	else
		*subdev_id = flash_ctrl->subdev_id;

	CDBG("subdev_id %d\n", *subdev_id);
	CDBG("Exit\n");
	return 0;
}

static int32_t msm_flash_init(
	struct msm_flash_ctrl_t *flash_ctrl,
	struct msm_flash_cfg_data_t *flash_data)
{
	uint32_t i = 0;
	int32_t rc = -EFAULT;
	enum msm_flash_driver_type flash_driver_type = FLASH_DRIVER_DEFAULT;

	CDBG("Enter");

	if (flash_ctrl->flash_state == MSM_CAMERA_FLASH_INIT) {
		pr_err("%s:%d Invalid flash state = %d",
			__func__, __LINE__, flash_ctrl->flash_state);
		return 0;
	}

	if (flash_data->cfg.flash_init_info->flash_driver_type ==
		FLASH_DRIVER_DEFAULT) {
		flash_driver_type = flash_ctrl->flash_driver_type;
		for (i = 0; i < MAX_LED_TRIGGERS; i++) {
			flash_data->flash_current[i] =
				flash_ctrl->flash_max_current[i];
			flash_data->flash_duration[i] =
				flash_ctrl->flash_max_duration[i];
		}
	} else if (flash_data->cfg.flash_init_info->flash_driver_type ==
		flash_ctrl->flash_driver_type) {
		flash_driver_type = flash_ctrl->flash_driver_type;
		for (i = 0; i < MAX_LED_TRIGGERS; i++) {
			flash_ctrl->flash_max_current[i] =
				flash_data->flash_current[i];
			flash_ctrl->flash_max_duration[i] =
					flash_data->flash_duration[i];
		}
	}

	if (flash_driver_type == FLASH_DRIVER_DEFAULT) {
		pr_err("%s:%d invalid flash_driver_type", __func__, __LINE__);
		return -EINVAL;
	}

	if(flash_driver_type == FLASH_DRIVER_I2C_MAJOR && main_flash_ctrl.func_tbl!=NULL){
		flash_ctrl->func_tbl = main_flash_ctrl.func_tbl;
	} else if (flash_driver_type == FLASH_DRIVER_I2C_MINOR && sub_flash_ctrl.func_tbl!=NULL) {
		flash_ctrl->func_tbl = sub_flash_ctrl.func_tbl;
	}

	if (flash_ctrl->func_tbl->camera_flash_init) {
		rc = flash_ctrl->func_tbl->camera_flash_init(
				flash_ctrl, flash_data);
		if (rc < 0) {
			pr_err("%s:%d camera_flash_init failed rc = %d",
				__func__, __LINE__, rc);
			return rc;
		}
	}

	flash_ctrl->flash_state = MSM_CAMERA_FLASH_INIT;

	CDBG("Exit");
	return 0;
}

static int32_t msm_flash_config(struct msm_flash_ctrl_t *flash_ctrl,
	void __user *argp)
{
	int32_t rc = 0;
	struct msm_flash_cfg_data_t *flash_data =
		(struct msm_flash_cfg_data_t *) argp;

	mutex_lock(flash_ctrl->flash_mutex);

	CDBG("Enter %s type %d\n", __func__, flash_data->cfg_type);

	switch (flash_data->cfg_type) {
	case CFG_FLASH_INIT:
		rc = msm_flash_init(flash_ctrl, flash_data);
		break;
	case CFG_FLASH_RELEASE:
		if (flash_ctrl->flash_state == MSM_CAMERA_FLASH_INIT){
			rc = flash_ctrl->func_tbl->camera_flash_off(
				flash_ctrl, flash_data);
			if(flash_ctrl->func_tbl->camera_flash_release) {
				rc = flash_ctrl->func_tbl->camera_flash_release(
					flash_ctrl);
				if (!rc)
					flash_ctrl->flash_state = MSM_CAMERA_FLASH_RELEASE;
			}
		}
		break;
	case CFG_FLASH_OFF:
		if (flash_ctrl->flash_state == MSM_CAMERA_FLASH_INIT)
			rc = flash_ctrl->func_tbl->camera_flash_off(
				flash_ctrl, flash_data);
		break;
	case CFG_FLASH_LOW:
		if (flash_ctrl->flash_state == MSM_CAMERA_FLASH_INIT)
			rc = flash_ctrl->func_tbl->camera_flash_low(
				flash_ctrl, flash_data);
		break;
	case CFG_FLASH_HIGH:
		if (flash_ctrl->flash_state == MSM_CAMERA_FLASH_INIT)
			rc = flash_ctrl->func_tbl->camera_flash_high(
				flash_ctrl, flash_data);
		break;
	case CFG_FLASH_TORCH:
		if (flash_ctrl->flash_state == MSM_CAMERA_FLASH_INIT)
		{
			if(flash_ctrl->func_tbl->camera_flash_torch)
			{
				rc = flash_ctrl->func_tbl->camera_flash_torch(
					flash_ctrl, flash_data);
			}
			else {
				rc = flash_ctrl->func_tbl->camera_flash_low(
					flash_ctrl, flash_data);
			}
		}
		break;
	default:
		rc = -EFAULT;
		break;
	}

	mutex_unlock(flash_ctrl->flash_mutex);

	CDBG("Exit %s type %d\n", __func__, flash_data->cfg_type);

	return rc;
}

static long msm_flash_subdev_ioctl(struct v4l2_subdev *sd,
	unsigned int cmd, void *arg)
{
	struct msm_flash_ctrl_t *fctrl = NULL;
	void __user *argp = (void __user *)arg;

	CDBG("Enter\n");

	if (!sd) {
		pr_err("sd NULL\n");
		return -EINVAL;
	}
	fctrl = v4l2_get_subdevdata(sd);
	if (!fctrl) {
		pr_err("fctrl NULL\n");
		return -EINVAL;
	}
	switch (cmd) {
	case VIDIOC_MSM_SENSOR_GET_SUBDEV_ID:
		return msm_flash_get_subdev_id(fctrl, argp);
	case VIDIOC_MSM_FLASH_CFG:
		return msm_flash_config(fctrl, argp);
	case MSM_SD_NOTIFY_FREEZE:
		return 0;
	case MSM_SD_UNNOTIFY_FREEZE:
		return 0;
	case MSM_SD_SHUTDOWN:
		if (!fctrl->func_tbl) {
			pr_err("fctrl->func_tbl NULL\n");
			return -EINVAL;
		} else {
			if(fctrl->func_tbl->camera_flash_release)
				return fctrl->func_tbl->camera_flash_release(fctrl);
		}
	default:
		pr_err_ratelimited("invalid cmd %d\n", cmd);
		return -ENOIOCTLCMD;
	}
	CDBG("Exit\n");
}

static struct v4l2_subdev_core_ops msm_flash_subdev_core_ops = {
	.ioctl = msm_flash_subdev_ioctl,
};

static struct v4l2_subdev_ops msm_flash_subdev_ops = {
	.core = &msm_flash_subdev_core_ops,
};

static const struct v4l2_subdev_internal_ops msm_flash_internal_ops;


static int32_t msm_flash_get_dt_data(struct device_node *of_node,
	struct msm_flash_ctrl_t *fctrl)
{
	int32_t rc = 0;
	enum flash_type flashtype;
	CDBG("called\n");
	if (!of_node ||!fctrl ) {
		pr_err("%s:NULL ptr. of_node =%pK, fctrl=%pK\n",__func__, of_node, fctrl);
		return -EINVAL;
	}

	/* Read the sub device */
	if( fctrl->pdev ){
		rc = of_property_read_u32(of_node, "cell-index", &fctrl->pdev->id);
		if (rc < 0) {
			pr_err("failed rc %d\n", rc);
			return rc;
		}
		CDBG("subdev id %d\n", fctrl->pdev->id);

	}
	rc = of_property_read_u32(of_node, "cell-index", &fctrl->subdev_id);
	if (rc < 0) {
		pr_err("failed rc %d\n", rc);
		return rc;
	}
	CDBG("subdev id %d\n", fctrl->subdev_id);

	fctrl->flash_driver_type = FLASH_DRIVER_DEFAULT;

	rc = of_property_read_u32(of_node, "qcom,flash-type", &flashtype);
	if (rc < 0) {
		pr_err("qcom,flash-type read failed\n");
	}
/*
	rc = of_property_read_u32(of_node, "qcom,cci-master",
		&fctrl->cci_i2c_master);
	CDBG("%s qcom,cci-master %d, rc %d\n", __func__, fctrl->cci_i2c_master,
		rc);
	if (rc < 0) {
		fctrl->cci_i2c_master = MASTER_0;
		rc = 0;
	} else {
		fctrl->flash_driver_type = FLASH_DRIVER_I2C;
	}
*/
	/* Read the gpio information from device tree */
	rc = msm_sensor_driver_get_gpio_data(
		&(fctrl->power_info.gpio_conf), of_node);
	if (rc < 0) {
		pr_err("%s:%d msm_sensor_driver_get_gpio_data failed rc %d\n",
			__func__, __LINE__, rc);
		return rc;
	}
/*
	if (fctrl->flash_driver_type == FLASH_DRIVER_DEFAULT)
		fctrl->flash_driver_type = FLASH_DRIVER_PMIC;
		CDBG("%s:%d fctrl->flash_driver_type = %d\n", __func__, __LINE__,
		fctrl->flash_driver_type);
*/
	return rc;
}

#ifdef CONFIG_COMPAT
static long msm_flash_subdev_do_ioctl(
	struct file *file, unsigned int cmd, void *arg)
{
	int32_t i = 0;
	int32_t rc = 0;
	struct video_device *vdev;
	struct v4l2_subdev *sd;
	struct msm_flash_cfg_data_t32 *u32;
	struct msm_flash_cfg_data_t flash_data;
	struct msm_flash_init_info_t32 flash_init_info32;
	struct msm_flash_init_info_t flash_init_info;

	CDBG("Enter");

	if (!file || !arg) {
		pr_err("%s:failed NULL parameter\n", __func__);
		return -EINVAL;
	}
	vdev = video_devdata(file);
	sd = vdev_to_v4l2_subdev(vdev);
	u32 = (struct msm_flash_cfg_data_t32 *)arg;

	flash_data.cfg_type = u32->cfg_type;
	for (i = 0; i < MAX_LED_TRIGGERS; i++) {
		flash_data.flash_current[i] = u32->flash_current[i];
		flash_data.flash_duration[i] = u32->flash_duration[i];
	}
	switch (cmd) {
	case VIDIOC_MSM_FLASH_CFG32:
		cmd = VIDIOC_MSM_FLASH_CFG;
		switch (flash_data.cfg_type) {
		case CFG_FLASH_OFF:
		case CFG_FLASH_LOW:
		case CFG_FLASH_HIGH:
			flash_data.cfg.settings = compat_ptr(u32->cfg.settings);
			break;
		case CFG_FLASH_INIT:
			flash_data.cfg.flash_init_info = &flash_init_info;
			if (copy_from_user(&flash_init_info32,
				(void *)compat_ptr(u32->cfg.flash_init_info),
				sizeof(struct msm_flash_init_info_t32))) {
				pr_err("%s copy_from_user failed %d\n",
					__func__, __LINE__);
				return -EFAULT;
			}
			flash_init_info.flash_driver_type =
				flash_init_info32.flash_driver_type;
			flash_init_info.slave_addr =
				flash_init_info32.slave_addr;
			flash_init_info.i2c_freq_mode =
				flash_init_info32.i2c_freq_mode;
			flash_init_info.settings =
				compat_ptr(flash_init_info32.settings);
			flash_init_info.power_setting_array =
				compat_ptr(
				flash_init_info32.power_setting_array);
			break;
		default:
			break;
		}
		break;
	default:
		return msm_flash_subdev_ioctl(sd, cmd, arg);
	}

	rc =  msm_flash_subdev_ioctl(sd, cmd, &flash_data);
	for (i = 0; i < MAX_LED_TRIGGERS; i++) {
		u32->flash_current[i] = flash_data.flash_current[i];
		u32->flash_duration[i] = flash_data.flash_duration[i];
	}

	CDBG("Exit");
	return rc;
}

static long msm_flash_subdev_fops_ioctl(struct file *file,
	unsigned int cmd, unsigned long arg)
{
	return video_usercopy(file, cmd, arg, msm_flash_subdev_do_ioctl);
}
#endif
static int hw_flash_i2c_match_id(struct msm_flash_ctrl_t *fctrl,
	 struct i2c_flash_info_t *flash_info)
{
	int32_t rc = 0, i = 0;
	struct msm_flash_func_t* flash_func = NULL;
	enum msm_flash_driver_type driver_type = flash_info->flash_driver_type;

	if(!fctrl||!flash_info){
		pr_err("%s: NULL ptr. fctrl:%pK, flash_info:%pK\n",__func__, fctrl, flash_info);
		return -EINVAL;
	}

	if( driver_type >= FLASH_DRIVER_DEFAULT){
		pr_err("default led driver, no need to match id\n");
		return 0;
	}
	fctrl->func_tbl = NULL;
	for (i = 0; i < ARRAY_SIZE(flash_table); i++) {
		if (driver_type == flash_table[i]->flash_driver_type) {
			flash_func = &flash_table[i]->func_tbl;
			if(flash_func && flash_func->camera_flash_match_id){
				rc = flash_func->camera_flash_match_id(fctrl, flash_info);
				if(!rc){
					fctrl->func_tbl = flash_func;
					break;
				}
			}
		}
	}

	return rc;
}
static int hw_camera_flash_i2c_probe(struct i2c_client *client,
	const struct i2c_device_id *id)
{
	int32_t rc = 0;
	struct msm_flash_ctrl_t *flash_ctrl = NULL;
	int i = 0;
	int size = ARRAY_SIZE(i2c_device_table);
	struct i2c_flash_info_t *flash_info = i2c_device_table;

	CDBG("%s Enter\n", __func__);

	if ( NULL == client ) {
		pr_err("hw_camera_flash_i2c_probe: client is null\n");
		return -EINVAL;
	}
	if( NULL == id ){
		pr_err("hw_camera_flash_i2c_probe, id is NULL");
		id = hw_flash_i2c_id;
	}
	flash_ctrl =  (struct msm_flash_ctrl_t *)(id->driver_data);
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		pr_err("i2c_check_functionality failed\n");
		return -EINVAL;
	}
	rc = msm_flash_get_dt_data(client->dev.of_node, flash_ctrl);
	if (rc < 0) {
		pr_err("%s:%d msm_flash_get_dt_data failed\n",
			__func__, __LINE__);
		return -EINVAL;
	}
	if(NULL !=flash_ctrl->power_info.gpio_conf )
	{
		rc = msm_camera_request_gpio_table(
			flash_ctrl->power_info.gpio_conf->cam_gpio_req_tbl,
			flash_ctrl->power_info.gpio_conf->cam_gpio_req_tbl_size, 1);
		if (rc < 0) {
			pr_err("%s: request gpio failed\n", __func__);
			return rc;
		}
	}
	else
	{
		return -EINVAL;
	}
	flash_ctrl->flash_state = MSM_CAMERA_FLASH_RELEASE;
	flash_ctrl->power_info.dev = &client->dev;
	flash_ctrl->flash_device_type = MSM_CAMERA_I2C_DEVICE;
	flash_ctrl->flash_mutex = &msm_flash_mutex;
	flash_ctrl->flash_i2c_client.i2c_func_tbl = &msm_flash_qup_func_tbl;
	flash_ctrl->flash_i2c_client.client = client;
	for( i = 0; i < size; i++){
		flash_ctrl->flash_i2c_client.client->addr = flash_info[i].slave_addr;
		flash_ctrl->flash_i2c_client.addr_type =  flash_info[i].addr_type;
		rc = hw_flash_i2c_match_id(flash_ctrl,  &flash_info[i]);
		if( rc < 0 ){
			pr_err("%s: %d probe %s failed \n",__func__, __LINE__,  flash_info[i].name);
			continue;
		}
		CDBG("probe %s successful, rc = %d\n", flash_info[i].name,rc);
		flash_ctrl->flash_driver_type =  flash_info[i].flash_driver_type;
		break;
	}

	if( i >= size){
		pr_err("no vaild flash device\n");
		return -ENODEV;
	}

	CDBG("%s: %d match id SUCCESS\n",__func__, __LINE__);
	v4l2_subdev_init(&flash_ctrl->msm_sd.sd, &msm_flash_subdev_ops);
	v4l2_set_subdevdata(&flash_ctrl->msm_sd.sd, flash_ctrl);

	flash_ctrl->msm_sd.sd.internal_ops = &msm_flash_internal_ops;
	flash_ctrl->msm_sd.sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	snprintf(flash_ctrl->msm_sd.sd.name,
		ARRAY_SIZE(flash_ctrl->msm_sd.sd.name),
		"msm_camera_flash");
	media_entity_init(&flash_ctrl->msm_sd.sd.entity, 0, NULL, 0);
	flash_ctrl->msm_sd.sd.entity.type = MEDIA_ENT_T_V4L2_SUBDEV;
	flash_ctrl->msm_sd.sd.entity.group_id = MSM_CAMERA_SUBDEV_FLASH;
	flash_ctrl->msm_sd.close_seq = MSM_SD_CLOSE_2ND_CATEGORY | 0x1;
	msm_sd_register(&flash_ctrl->msm_sd);

	CDBG("%s:%d flash sd name = %s", __func__, __LINE__,
		flash_ctrl->msm_sd.sd.entity.name);
	msm_flash_v4l2_subdev_fops = v4l2_subdev_fops;
#ifdef CONFIG_COMPAT
	msm_flash_v4l2_subdev_fops.compat_ioctl32 =
		msm_flash_subdev_fops_ioctl;
#endif
	flash_ctrl->msm_sd.sd.devnode->fops = &msm_flash_v4l2_subdev_fops;

	rc = hw_i2c_torch_create_classdev(&(client->dev),NULL);
	if(rc < 0){
		pr_err("%s: %d create torch failed\n", __func__, __LINE__);
	}

	CDBG("probe success\n");
	return rc;
}

static int hw_sub_camera_flash_i2c_probe(struct i2c_client *client,
	const struct i2c_device_id *id)
{
	int32_t rc = 0;
	struct msm_flash_ctrl_t *flash_ctrl = NULL;
	int i = 0;
	int size = ARRAY_SIZE(i2c_device_sub_table);
	struct i2c_flash_info_t *flash_info = i2c_device_sub_table;

	if( NULL == id ){
		pr_err("hw_camera_flash_i2c_probe, id is NULL");
			id = hw_sub_flash_i2c_id;;
	}
	flash_ctrl =  (struct msm_flash_ctrl_t *)(id->driver_data);
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		pr_err("i2c_check_functionality failed\n");
		return -EINVAL;
	}
	rc = msm_flash_get_dt_data(client->dev.of_node, flash_ctrl);
	if (rc < 0) {
		pr_err("%s:%d msm_flash_get_dt_data failed\n",
			__func__, __LINE__);
		return -EINVAL;
	}
	if(NULL !=flash_ctrl->power_info.gpio_conf ){
		rc = msm_camera_request_gpio_table(
			flash_ctrl->power_info.gpio_conf->cam_gpio_req_tbl,
			flash_ctrl->power_info.gpio_conf->cam_gpio_req_tbl_size, 1);
		if(rc < 0)
		{
			pr_info("%s: request gpio failed\n", __func__);
		}
	}
	else{
		return -EINVAL;
	}
	flash_ctrl->flash_state = MSM_CAMERA_FLASH_RELEASE;
	flash_ctrl->power_info.dev = &client->dev;
	flash_ctrl->flash_device_type = MSM_CAMERA_I2C_DEVICE;
	flash_ctrl->flash_mutex = &msm_flash_mutex;
	flash_ctrl->flash_i2c_client.i2c_func_tbl = &msm_flash_qup_func_tbl;
	flash_ctrl->flash_i2c_client.client = client;
	for( i = 0; i < size; i++){
		flash_ctrl->flash_i2c_client.client->addr = flash_info[i].slave_addr;
		flash_ctrl->flash_i2c_client.addr_type =  flash_info[i].addr_type;
		rc = hw_flash_i2c_match_id(flash_ctrl,  &flash_info[i]);
		if( rc < 0 ){
			pr_err("%s: %d probe %s failed \n",__func__, __LINE__,  flash_info[i].name);
			continue;
		}
		CDBG("probe %s successful, rc = %d\n", flash_info[i].name,rc);
		flash_ctrl->flash_driver_type =  flash_info[i].flash_driver_type;
		break;
	}

	if( i >= size){
		pr_err("no vaild flash device\n");
		return -ENODEV;
	}

	CDBG("%s: %d match id SUCCESS\n",__func__, __LINE__);
	v4l2_subdev_init(&flash_ctrl->msm_sd.sd, &msm_flash_subdev_ops);
	v4l2_set_subdevdata(&flash_ctrl->msm_sd.sd, flash_ctrl);

	flash_ctrl->msm_sd.sd.internal_ops = &msm_flash_internal_ops;
	flash_ctrl->msm_sd.sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	snprintf(flash_ctrl->msm_sd.sd.name,
		ARRAY_SIZE(flash_ctrl->msm_sd.sd.name),
		"msm_camera_flash");
	media_entity_init(&flash_ctrl->msm_sd.sd.entity, 0, NULL, 0);
	flash_ctrl->msm_sd.sd.entity.type = MEDIA_ENT_T_V4L2_SUBDEV;
	flash_ctrl->msm_sd.sd.entity.group_id = MSM_CAMERA_SUBDEV_FLASH;
	flash_ctrl->msm_sd.close_seq = MSM_SD_CLOSE_2ND_CATEGORY | 0x1;
	msm_sd_register(&flash_ctrl->msm_sd);

	CDBG("%s:%d flash sd name = %s", __func__, __LINE__,
		flash_ctrl->msm_sd.sd.entity.name);
	msm_flash_v4l2_subdev_fops = v4l2_subdev_fops;
#ifdef CONFIG_COMPAT
	msm_flash_v4l2_subdev_fops.compat_ioctl32 =
		msm_flash_subdev_fops_ioctl;
#endif
	flash_ctrl->msm_sd.sd.devnode->fops = &msm_flash_v4l2_subdev_fops;

	rc = hw_i2c_sub_torch_create_classdev(&(client->dev),NULL);
	if(rc < 0){
		pr_err("%s: %d create torch failed\n", __func__, __LINE__);
	}

	CDBG("probe success\n");
	return rc;
}
static int hw_camera_flash_i2c_remove(struct i2c_client *client)
{
	int rc = 0 ;
	struct msm_camera_power_ctrl_t *power_info = NULL;

	CDBG("%s entry\n", __func__);

	power_info = &main_flash_ctrl.power_info;
	if(power_info->gpio_conf !=NULL)
	{
		rc = msm_camera_request_gpio_table(
			power_info->gpio_conf->cam_gpio_req_tbl,
			power_info->gpio_conf->cam_gpio_req_tbl_size, 0);
		if (rc < 0) {
			pr_err("%s: request gpio failed\n", __func__);
		}
	}

	return rc;
}
static int hw_sub_camera_flash_i2c_remove(struct i2c_client *client)
{
	int rc = 0 ;
	struct msm_camera_power_ctrl_t *power_info = NULL;

	CDBG("%s entry\n", __func__);

	power_info = &sub_flash_ctrl.power_info;
	if(power_info->gpio_conf !=NULL)
	{
		rc = msm_camera_request_gpio_table(
			power_info->gpio_conf->cam_gpio_req_tbl,
			power_info->gpio_conf->cam_gpio_req_tbl_size, 0);
		if (rc < 0) {
			pr_err("%s: request gpio failed\n", __func__);
			rc =0;
		}
	}
	return rc;
}


MODULE_DEVICE_TABLE(of, hw_flash_i2c_dt_match);
static struct i2c_driver hw_flash_i2c_driver = {
	.id_table = hw_flash_i2c_id,
	.probe  = hw_camera_flash_i2c_probe,
	.remove = hw_camera_flash_i2c_remove,
	.driver = {
		.name = "hw,camera-flash",
		.owner = THIS_MODULE,
		.of_match_table = hw_flash_i2c_dt_match,
	},
};

MODULE_DEVICE_TABLE(of, hw_sub_flash_i2c_dt_match);
static struct i2c_driver hw_sub_flash_i2c_driver = {
	.id_table = hw_sub_flash_i2c_id,
	.probe  = hw_sub_camera_flash_i2c_probe,
	.remove = hw_sub_camera_flash_i2c_remove,
	.driver = {
		.name = "hw,sub-camera-flash",
		.owner = THIS_MODULE,
		.of_match_table = hw_sub_flash_i2c_dt_match,
	},
};

static int __init msm_flash_init_module(void)
{
	int32_t rc = 0;
	CDBG("Enter\n");
	rc = i2c_add_driver(&hw_flash_i2c_driver);
	if(rc)
		pr_err("i2c probe  hw camera flash failed\n");

	rc = i2c_add_driver(&hw_sub_flash_i2c_driver);
	if (rc)
		pr_err("i2c probe for hw sub camera flash failed\n");

	return rc;
}

static void __exit msm_flash_exit_module(void)
{
	i2c_del_driver(&hw_flash_i2c_driver);
	i2c_del_driver(&hw_sub_flash_i2c_driver);
	return;
}

static struct msm_flash_table msm_i2c_flash_aw36413_table = {
	.flash_driver_type = FLASH_DRIVER_I2C_MAJOR,
	.func_tbl = {
		.camera_flash_init = hw_flash_i2c_aw36413_init,
		.camera_flash_release = hw_flash_i2c_aw36413_release,
		.camera_flash_off = hw_flash_i2c_aw36413_off,
		.camera_flash_low = hw_flash_i2c_aw36413_low,
		.camera_flash_high = hw_flash_i2c_aw36413_high,
		.camera_flash_match_id = hw_flash_i2c_aw36413_match_id,
		.camera_flash_torch = hw_flash_i2c_aw36413_torch,
	},
};
static struct msm_flash_table msm_i2c_flash_sub_aw36413_table = {
	.flash_driver_type = FLASH_DRIVER_I2C_MINOR,
	.func_tbl = {
		.camera_flash_init = hw_flash_i2c_aw36413_init,
		.camera_flash_release = hw_flash_i2c_aw36413_release,
		.camera_flash_off = hw_sub_flash_i2c_aw36413_off,
		.camera_flash_low = hw_sub_flash_i2c_aw36413_low,
		.camera_flash_high = hw_sub_flash_i2c_aw36413_high,
		.camera_flash_match_id = hw_flash_i2c_aw36413_match_id,
		.camera_flash_torch = hw_sub_flash_i2c_aw36413_torch,
	},
};
static struct msm_flash_table msm_i2c_flash_sy7806e_table = {
	.flash_driver_type = FLASH_DRIVER_I2C_MAJOR,
	.func_tbl = {
		.camera_flash_init = hw_flash_i2c_sy7806e_init,
		.camera_flash_release = hw_flash_i2c_sy7806e_release,
		.camera_flash_off = hw_flash_i2c_sy7806e_off,
		.camera_flash_low = hw_flash_i2c_sy7806e_low,
		.camera_flash_high = hw_flash_i2c_sy7806e_high,
		.camera_flash_match_id = hw_flash_i2c_sy7806e_match_id,
		.camera_flash_torch = hw_flash_i2c_sy7806e_torch,
	},
};
static struct msm_flash_table msm_i2c_flash_sub_sy7806e_table = {
	.flash_driver_type = FLASH_DRIVER_I2C_MINOR,
	.func_tbl = {
		.camera_flash_init = hw_flash_i2c_sy7806e_init,
		.camera_flash_release = hw_flash_i2c_sy7806e_release,
		.camera_flash_off = hw_sub_flash_i2c_sy7806e_off,
		.camera_flash_low = hw_sub_flash_i2c_sy7806e_low,
		.camera_flash_high = hw_sub_flash_i2c_sy7806e_high,
		.camera_flash_match_id = hw_flash_i2c_sy7806e_match_id,
		.camera_flash_torch = hw_sub_flash_i2c_sy7806e_torch,
	},
};
module_init(msm_flash_init_module);
module_exit(msm_flash_exit_module);
MODULE_DESCRIPTION("MSM FLASH");
MODULE_LICENSE("GPL v2");
