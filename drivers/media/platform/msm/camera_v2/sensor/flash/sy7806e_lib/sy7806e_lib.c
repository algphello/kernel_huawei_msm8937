#include <linux/gpio.h>
#include "sy7806e_lib.h"

#define CONFIG_MSMB_CAMERA_DEBUG
#ifdef CONFIG_MSMB_CAMERA_DEBUG
#define SY7806E_DBG(fmt, args...) pr_info(fmt, ##args)
#else
#define SY7806E_DBG(fmt, args...)
#endif

#define FLASH_CHIP_ID_REG 0x0C
#define FLASH_CHIP_ID	0x1C
#define FLASH_FLAG1_REGISTER 0x0a
#define FLASH_FLAG2_REGISTER 0x0b
#define FLASH_ENABLE_REG 0x01

#define TORCH_MIN_CURRENT 3
#define TORCH_MAX_CURRENT 350
/*
static struct msm_camera_i2c_reg_array sy7806e_init_array[] = {
	{0x01, 0x00,0x00},//enable reg 0x01 ,mode bit 3:2 torch 10,flash 11,bit 1 led2,bit 0 led1
};
*/
static struct msm_camera_i2c_reg_array sy7806e_low_array[] = {
	{0x05, 0x3F,0x00},//LED1 0x3f=186ma
	//{0x01, 0x09,0x00},
};

static struct msm_camera_i2c_reg_array sy7806e_sub_low_array[] = {
	{0x03, 0x3F,0x00},//set reg 0x03 bit 7 is 0
	{0x05, 0x3F,0x00},//set reg 0x05 bit 7 is 0
	//{0x06, 0x22,0x00},//LED2 0x22=100ma
	//{0x01, 0x0a,0x00},
};

static struct msm_camera_i2c_reg_array sy7806e_high_array[] = {
	{0x03, 0x3F,0x00},//LED1 0x3F=750ma
	{0x08, 0x1A,0x00},//flash duration time 0x1A=600ms
	{0x01, 0x0D,0x00},
};

static struct msm_camera_i2c_reg_array sy7806e_sub_high_array[] = {
	{0x03, 0x3F,0x00},//set reg 0x03 bit 7 is 0
	{0x05, 0x3F,0x00},//set reg 0x05 bit 7 is 0
	{0x04, 0x11,0x00},//LED2 0x11=209ma
	{0x08, 0x1A,0x00},//flash duration time 0x1A=600ms
	{0x01, 0x0E,0x00},
};

static struct msm_camera_i2c_reg_array sy7806e_torch_array[] = {
	{0x05, 0x3F, 0x00},//LED1 0x3f=186ma
	{0x01, 0x09, 0x00},
};

static struct msm_camera_i2c_reg_array sy7806e_sub_torch_array[] = {
	{0x03, 0x3F,0x00},//set reg 0x03 bit 7 is 0
	{0x05, 0x3F,0x00},//set reg 0x05 bit 7 is 0
	{0x06, 0x22,0x00},//LED2 0x22=100ma
	{0x01, 0x0a,0x00},
};

static struct msm_camera_i2c_reg_setting sy7806e_low_setting = {
	.reg_setting = sy7806e_low_array,
	.size = ARRAY_SIZE(sy7806e_low_array),
	.addr_type = MSM_CAMERA_I2C_BYTE_ADDR,
	.data_type = MSM_CAMERA_I2C_BYTE_DATA,
	.delay = 0,
};

static struct msm_camera_i2c_reg_setting sy7806e_sub_low_setting = {
	.reg_setting = sy7806e_sub_low_array,
	.size = ARRAY_SIZE(sy7806e_sub_low_array),
	.addr_type = MSM_CAMERA_I2C_BYTE_ADDR,
	.data_type = MSM_CAMERA_I2C_BYTE_DATA,
	.delay = 0,
};

static struct msm_camera_i2c_reg_setting sy7806e_high_setting = {
	.reg_setting = sy7806e_high_array,
	.size = ARRAY_SIZE(sy7806e_high_array),
	.addr_type = MSM_CAMERA_I2C_BYTE_ADDR,
	.data_type = MSM_CAMERA_I2C_BYTE_DATA,
	.delay = 0,
};

static struct msm_camera_i2c_reg_setting sy7806e_sub_high_setting = {
	.reg_setting = sy7806e_sub_high_array,
	.size = ARRAY_SIZE(sy7806e_sub_high_array),
	.addr_type = MSM_CAMERA_I2C_BYTE_ADDR,
	.data_type = MSM_CAMERA_I2C_BYTE_DATA,
	.delay = 0,
};

static struct msm_camera_i2c_reg_setting sy7806e_torch_setting = {
	.reg_setting = sy7806e_torch_array,
	.size = ARRAY_SIZE(sy7806e_torch_array),
	.addr_type = MSM_CAMERA_I2C_BYTE_ADDR,
	.data_type = MSM_CAMERA_I2C_BYTE_DATA,
	.delay = 0,
};

static struct msm_camera_i2c_reg_setting sy7806e_sub_torch_setting = {
	.reg_setting = sy7806e_sub_torch_array,
	.size = ARRAY_SIZE(sy7806e_sub_torch_array),
	.addr_type = MSM_CAMERA_I2C_BYTE_ADDR,
	.data_type = MSM_CAMERA_I2C_BYTE_DATA,
	.delay = 0,
};
static int32_t hw_flash_i2c_sy7806e_check_flag(struct msm_flash_ctrl_t *flash_ctrl)
{
	int rc = 0;
	uint16_t reg_value=0;

	SY7806E_DBG("%s entry\n", __func__);

	if (!flash_ctrl) {
		pr_err("%s:%d fctrl NULL\n", __func__, __LINE__);
		return -EINVAL;
	}

	if (flash_ctrl->flash_i2c_client.i2c_func_tbl) {
		rc = flash_ctrl->flash_i2c_client.i2c_func_tbl->i2c_read(
		&flash_ctrl->flash_i2c_client,
			FLASH_FLAG1_REGISTER,&reg_value, MSM_CAMERA_I2C_BYTE_DATA);
		if (rc < 0) {
			pr_err("%s:%d failed\n", __func__, __LINE__);
		}
		rc = flash_ctrl->flash_i2c_client.i2c_func_tbl->i2c_read(
			&flash_ctrl->flash_i2c_client,
			FLASH_FLAG2_REGISTER,&reg_value, MSM_CAMERA_I2C_BYTE_DATA);
		if (rc < 0) {
			pr_err("%s:%d failed\n", __func__, __LINE__);
		}
	} else {
		pr_err("%s:%d flash_i2c_client NULL\n", __func__, __LINE__);
		return -EINVAL;
	}

	return 0;

}

int32_t hw_flash_i2c_sy7806e_init(struct msm_flash_ctrl_t *flash_ctrl,
		struct msm_flash_cfg_data_t *flash_data)
{
	int rc = 0;

	SY7806E_DBG("%s:%d called\n", __func__, __LINE__);

	if (!flash_ctrl) {
		pr_err("%s:%d fctrl NULL\n", __func__, __LINE__);
		return -EINVAL;
	}

	if (NULL != flash_ctrl->power_info.gpio_conf && NULL != flash_ctrl->power_info.gpio_conf->gpio_num_info) {
		gpio_set_value_cansleep(flash_ctrl->power_info.gpio_conf->gpio_num_info->gpio_num[SENSOR_GPIO_FL_EN],1);
		msleep(2);

		hw_flash_i2c_sy7806e_check_flag(flash_ctrl);
	} else {
		pr_err("%s,gpio is NULL\n", __func__);
		return -EINVAL;
	}
	return 0;
}

int32_t hw_flash_i2c_sy7806e_release(struct msm_flash_ctrl_t *flash_ctrl)
{
	int rc = 0;
	uint16_t reg_val = 0;
	SY7806E_DBG("%s:%d called\n", __func__, __LINE__);

	if (!flash_ctrl) {
		pr_err("%s:%d fctrl NULL\n", __func__, __LINE__);
		return -EINVAL;
	}

	if (NULL != flash_ctrl->power_info.gpio_conf && NULL != flash_ctrl->power_info.gpio_conf->gpio_num_info) {

		if(flash_ctrl->flash_i2c_client.i2c_func_tbl){
			rc = flash_ctrl->flash_i2c_client.i2c_func_tbl->i2c_read(
					&flash_ctrl->flash_i2c_client,FLASH_ENABLE_REG,&reg_val, MSM_CAMERA_I2C_BYTE_DATA);//read 0x01 state
			if (rc < 0){
				pr_err("%s:%d failed\n", __func__, __LINE__);
				return rc;
			}
			SY7806E_DBG("%s:reg_val:[0x%x],(reg_val&0x03):[0x%x]\n",__func__, reg_val,(reg_val&0x03));
			if (!(reg_val&0x03)){//led1 led2 off
				gpio_set_value_cansleep(flash_ctrl->power_info.gpio_conf->gpio_num_info->gpio_num[SENSOR_GPIO_FL_EN],0);
			}
		}

	} else {
		pr_err("%s,gpio is NULL\n", __func__);
		return -EINVAL;
	}
	return 0;
}
int32_t hw_flash_i2c_sy7806e_low(struct msm_flash_ctrl_t *flash_ctrl,
		struct msm_flash_cfg_data_t *flash_data)
{
	int rc = 0;
	unsigned short reg_data = 0x3F;//default
	uint16_t reg_val = 0;
	SY7806E_DBG("%s:%d called\n", __func__, __LINE__);

	if (!flash_ctrl) {
		pr_err("%s:%d fctrl NULL\n", __func__, __LINE__);
		return -EINVAL;
	}
	if (NULL != flash_ctrl->power_info.gpio_conf && NULL != flash_ctrl->power_info.gpio_conf->gpio_num_info) {
		gpio_set_value_cansleep(flash_ctrl->power_info.gpio_conf->gpio_num_info->gpio_num[SENSOR_GPIO_FL_EN],1);
		msleep(2);

		if(flash_ctrl->flash_i2c_client.i2c_func_tbl){

			if (NULL != flash_data && flash_data->flash_current[0] > TORCH_MIN_CURRENT
				&& flash_data->flash_current[0] < TORCH_MAX_CURRENT) {
				reg_data = (flash_data->flash_current[0]*100 - 180)/280;//ITorch(mA)= Brightness Code*2.8mA+1.8mA
			}

			sy7806e_low_setting.reg_setting[0].reg_data = reg_data;

			rc = flash_ctrl->flash_i2c_client.i2c_func_tbl->i2c_write_table(
				&flash_ctrl->flash_i2c_client,
				&sy7806e_low_setting);
			if (rc < 0){
				pr_err("%s:%d failed\n", __func__, __LINE__);
				return rc;
			}
			rc = flash_ctrl->flash_i2c_client.i2c_func_tbl->i2c_read(
					&flash_ctrl->flash_i2c_client,FLASH_ENABLE_REG,&reg_val, MSM_CAMERA_I2C_BYTE_DATA);//read 0x01 state
			if (rc < 0){
				pr_err("%s:%d failed\n", __func__, __LINE__);
				return rc;
			}
			SY7806E_DBG("%s:reg_val:[0x%x],(reg_val&0x0F)|0x09:[0x%x]\n",__func__, reg_val,(reg_val&0x0F)|0x09);
			rc = flash_ctrl->flash_i2c_client.i2c_func_tbl->i2c_write(
				&flash_ctrl->flash_i2c_client,FLASH_ENABLE_REG,(reg_val&0x0F)|0x09,MSM_CAMERA_I2C_BYTE_DATA);//reg 0x01 = 0x0a;enable led2
			if (rc < 0){
				pr_err("%s:%d failed\n", __func__, __LINE__);
				return rc;
			}
		}
	} else {
		pr_err("%s,gpio is NULL\n", __func__);
		return -EINVAL;
	}
	return 0;
}

int32_t hw_sub_flash_i2c_sy7806e_low(struct msm_flash_ctrl_t *flash_ctrl,
		struct msm_flash_cfg_data_t *flash_data)
{
	int rc = 0;
	uint16_t reg_data = 0;
	uint16_t reg_val = 0;
	SY7806E_DBG("%s:%d called\n", __func__, __LINE__);

	if (!flash_ctrl) {
		pr_err("%s:%d fctrl NULL\n", __func__, __LINE__);
		return -EINVAL;
	}

	if (NULL != flash_ctrl->power_info.gpio_conf && NULL != flash_ctrl->power_info.gpio_conf->gpio_num_info) {
		gpio_set_value_cansleep(flash_ctrl->power_info.gpio_conf->gpio_num_info->gpio_num[SENSOR_GPIO_FL_EN],1);
		msleep(2);

		if(flash_ctrl->flash_i2c_client.i2c_func_tbl){
			rc = flash_ctrl->flash_i2c_client.i2c_func_tbl->i2c_write_table(
				&flash_ctrl->flash_i2c_client,
				&sy7806e_sub_low_setting);
			if (rc < 0){
				pr_err("%s:%d failed\n", __func__, __LINE__);
				return rc;
			}
			if (NULL != flash_data) {
				reg_data = (flash_data->flash_current[0]*100 - 180)/280;//ITorch2(mA)= Brightness Code*2.8mA+1.8mA
				if (reg_data > 127){
					reg_data = 0x0d;//0x0d = 40ma
				}
			} else {
				reg_data = 0x23;//0x23 = 97ma
			}
			SY7806E_DBG("reg_data:[%d]\n",reg_data);

			rc = flash_ctrl->flash_i2c_client.i2c_func_tbl->i2c_write(
				&flash_ctrl->flash_i2c_client,0x06,reg_data,MSM_CAMERA_I2C_BYTE_DATA);//reg = 0x06;set current
			if (rc < 0){
				pr_err("%s:%d failed\n", __func__, __LINE__);
				return rc;
			}
			rc = flash_ctrl->flash_i2c_client.i2c_func_tbl->i2c_read(
					&flash_ctrl->flash_i2c_client,FLASH_ENABLE_REG,&reg_val, MSM_CAMERA_I2C_BYTE_DATA);//read 0x01 state
			if (rc < 0){
				pr_err("%s:%d failed\n", __func__, __LINE__);
				return rc;
			}
			SY7806E_DBG("%s:reg_val:[0x%x],(reg_val&0x0F)|0x0a:[0x%x]\n",__func__, reg_val,(reg_val&0x0F)|0x0a);
			rc = flash_ctrl->flash_i2c_client.i2c_func_tbl->i2c_write(
				&flash_ctrl->flash_i2c_client,FLASH_ENABLE_REG,(reg_val&0x0F)|0x0a,MSM_CAMERA_I2C_BYTE_DATA);//reg 0x01 = 0x0a;enable led2
			if (rc < 0){
				pr_err("%s:%d failed\n", __func__, __LINE__);
				return rc;
			}
		}
	} else {
		pr_err("%s,gpio is NULL\n", __func__);
		return -EINVAL;
	}

	return 0;
}


int32_t hw_flash_i2c_sy7806e_high(struct msm_flash_ctrl_t *flash_ctrl,
		struct msm_flash_cfg_data_t *flash_data)
{
	int rc = 0;
	SY7806E_DBG("%s:%d called\n", __func__, __LINE__);

	if (!flash_ctrl) {
		pr_err("%s:%d fctrl NULL\n", __func__, __LINE__);
		return -EINVAL;
	}
	if (NULL != flash_ctrl->power_info.gpio_conf && NULL != flash_ctrl->power_info.gpio_conf->gpio_num_info) {

		if(flash_ctrl->flash_i2c_client.i2c_func_tbl){
			rc = flash_ctrl->flash_i2c_client.i2c_func_tbl->i2c_write_table(
				&flash_ctrl->flash_i2c_client,
				&sy7806e_high_setting);
			if (rc < 0){
				pr_err("%s:%d failed\n", __func__, __LINE__);
				return rc;
			}
		}
	} else {
		pr_err("%s,gpio is NULL\n", __func__);
		return -EINVAL;
	}
	return 0;
}
int32_t hw_sub_flash_i2c_sy7806e_high(struct msm_flash_ctrl_t *flash_ctrl,
		struct msm_flash_cfg_data_t *flash_data)
{
	int rc = 0;
	SY7806E_DBG("%s:%d called\n", __func__, __LINE__);

	if (!flash_ctrl) {
		pr_err("%s:%d fctrl NULL\n", __func__, __LINE__);
		return -EINVAL;
	}
	if (NULL != flash_ctrl->power_info.gpio_conf && NULL != flash_ctrl->power_info.gpio_conf->gpio_num_info) {

		if(flash_ctrl->flash_i2c_client.i2c_func_tbl){
			rc = flash_ctrl->flash_i2c_client.i2c_func_tbl->i2c_write_table(
				&flash_ctrl->flash_i2c_client,
				&sy7806e_sub_high_setting);
			if (rc < 0){
				pr_err("%s:%d failed\n", __func__, __LINE__);
				return rc;
			}
		}
	} else {
		pr_err("%s,gpio is NULL\n", __func__);
		return -EINVAL;
	}
	return 0;
}

int32_t hw_flash_i2c_sy7806e_torch(struct msm_flash_ctrl_t *flash_ctrl,
		struct msm_flash_cfg_data_t *flash_data)
{
	int rc = 0;

	SY7806E_DBG("%s:%d called\n", __func__, __LINE__);

	if (!flash_ctrl) {
		pr_err("%s:%d fctrl NULL\n", __func__, __LINE__);
		return -EINVAL;
	}
	if (NULL != flash_ctrl->power_info.gpio_conf && NULL != flash_ctrl->power_info.gpio_conf->gpio_num_info) {
		gpio_set_value_cansleep(flash_ctrl->power_info.gpio_conf->gpio_num_info->gpio_num[SENSOR_GPIO_FL_EN],1);
		msleep(2);

		if(flash_ctrl->flash_i2c_client.i2c_func_tbl){
			rc = flash_ctrl->flash_i2c_client.i2c_func_tbl->i2c_write_table(
				&flash_ctrl->flash_i2c_client,
				&sy7806e_torch_setting);
			if (rc < 0){
				pr_err("%s:%d failed\n", __func__, __LINE__);
				return rc;
			}
		}
	} else {
		pr_err("%s,gpio is NULL\n", __func__);
		return -EINVAL;
	}

	return 0;
}

int32_t hw_sub_flash_i2c_sy7806e_torch(struct msm_flash_ctrl_t *flash_ctrl,
		struct msm_flash_cfg_data_t *flash_data)
{
	int rc = 0;

	SY7806E_DBG("%s:%d called\n", __func__, __LINE__);

	if (!flash_ctrl) {
		pr_err("%s:%d fctrl NULL\n", __func__, __LINE__);
		return -EINVAL;
	}
	if (NULL != flash_ctrl->power_info.gpio_conf && NULL != flash_ctrl->power_info.gpio_conf->gpio_num_info) {
		gpio_set_value_cansleep(flash_ctrl->power_info.gpio_conf->gpio_num_info->gpio_num[SENSOR_GPIO_FL_EN],1);
		msleep(2);
		if(flash_ctrl->flash_i2c_client.i2c_func_tbl){
			rc = flash_ctrl->flash_i2c_client.i2c_func_tbl->i2c_write_table(
				&flash_ctrl->flash_i2c_client,
				&sy7806e_sub_torch_setting);
			if (rc < 0){
				pr_err("%s:%d failed\n", __func__, __LINE__);
				return rc;
			}
		}
	} else {
		pr_err("%s,gpio is NULL\n", __func__);
		return -EINVAL;
	}

	return 0;
}

int32_t hw_flash_i2c_sy7806e_off(struct msm_flash_ctrl_t *flash_ctrl,
		struct msm_flash_cfg_data_t *flash_data)
{
	int rc = 0;
	uint16_t reg_val = 0;
	SY7806E_DBG("%s:%d called\n", __func__, __LINE__);

	if (!flash_ctrl) {
		pr_err("%s:%d fctrl NULL\n", __func__, __LINE__);
		return -EINVAL;
	}
	if (NULL != flash_ctrl->power_info.gpio_conf && NULL != flash_ctrl->power_info.gpio_conf->gpio_num_info) {
		if(flash_ctrl->flash_i2c_client.i2c_func_tbl){
			rc = flash_ctrl->flash_i2c_client.i2c_func_tbl->i2c_read(
					&flash_ctrl->flash_i2c_client,FLASH_ENABLE_REG,&reg_val, MSM_CAMERA_I2C_BYTE_DATA);//read 0x01 state
			if (rc < 0){
				pr_err("%s:%d failed\n", __func__, __LINE__);
				return rc;
			}
			SY7806E_DBG("%s:reg_val:[0x%x],reg_val&0x0a:[0x%x]\n",__func__, reg_val,reg_val&0x0a);
			rc = flash_ctrl->flash_i2c_client.i2c_func_tbl->i2c_write(
				&flash_ctrl->flash_i2c_client,FLASH_ENABLE_REG,reg_val&0x0a,MSM_CAMERA_I2C_BYTE_DATA);//reg 0x01 = 0x0a;enable led2
			if (rc < 0){
				pr_err("%s:%d failed\n", __func__, __LINE__);
				return rc;
			}
		}

	} else {
		pr_err("%s,gpio is NULL\n", __func__);
		return -EINVAL;
	}

	return 0;
}

int32_t hw_sub_flash_i2c_sy7806e_off(struct msm_flash_ctrl_t *flash_ctrl,
		struct msm_flash_cfg_data_t *flash_data)
{
	int rc = 0;
	uint16_t reg_val = 0;
	SY7806E_DBG("%s:%d called\n", __func__, __LINE__);

	if (!flash_ctrl) {
		pr_err("%s:%d fctrl NULL\n", __func__, __LINE__);
		return -EINVAL;
	}
	if (NULL != flash_ctrl->power_info.gpio_conf && NULL != flash_ctrl->power_info.gpio_conf->gpio_num_info) {
		if(flash_ctrl->flash_i2c_client.i2c_func_tbl){
			rc = flash_ctrl->flash_i2c_client.i2c_func_tbl->i2c_read(
					&flash_ctrl->flash_i2c_client,FLASH_ENABLE_REG,&reg_val, MSM_CAMERA_I2C_BYTE_DATA);//read 0x01 state
			if (rc < 0){
				pr_err("%s:%d failed\n", __func__, __LINE__);
				gpio_set_value_cansleep(flash_ctrl->power_info.gpio_conf->gpio_num_info->gpio_num[SENSOR_GPIO_FL_EN],0);
				return rc;
			}
			SY7806E_DBG("%s:reg_val:[0x%x],reg_val&0x0a:[0x%x]\n",__func__, reg_val,reg_val&0x09);
			rc = flash_ctrl->flash_i2c_client.i2c_func_tbl->i2c_write(
				&flash_ctrl->flash_i2c_client,FLASH_ENABLE_REG,reg_val&0x09,MSM_CAMERA_I2C_BYTE_DATA);//reg 0x01 = 0x0a;enable led2
			if (rc < 0){
				pr_err("%s:%d failed\n", __func__, __LINE__);
				gpio_set_value_cansleep(flash_ctrl->power_info.gpio_conf->gpio_num_info->gpio_num[SENSOR_GPIO_FL_EN],0);
				return rc;
			}
		}

	} else {
		pr_err("%s,gpio is NULL\n", __func__);
		return -EINVAL;
	}

	return 0;
}

/****************************************************************************
* FunctionName: msm_flash_sy7806e_match_id;
* Description :read id and compared with FLASH_CHIP_ID;
***************************************************************************/
int32_t hw_flash_i2c_sy7806e_match_id(struct msm_flash_ctrl_t *flash_ctrl,struct i2c_flash_info_t*flash_info)
{
	int32_t rc = 0;
	int32_t i = 0;
	uint16_t id_val = 0;

	if(!flash_ctrl || !flash_info){
		pr_err("%s:%d, flash_ctrl:%pK, flash_info:%pK", __func__, __LINE__, flash_ctrl, flash_info);
		return -EINVAL;
	}

	if (NULL != flash_ctrl->power_info.gpio_conf && NULL != flash_ctrl->power_info.gpio_conf->gpio_num_info) {
		gpio_set_value_cansleep(flash_ctrl->power_info.gpio_conf->gpio_num_info->gpio_num[SENSOR_GPIO_FL_EN],1);
		msleep(1);

		hw_flash_i2c_sy7806e_check_flag(flash_ctrl);

		if (flash_ctrl->flash_i2c_client.i2c_func_tbl) {
			for(i = 0; i < 3; i++){
				rc = flash_ctrl->flash_i2c_client.i2c_func_tbl->i2c_read(
					&flash_ctrl->flash_i2c_client,FLASH_CHIP_ID_REG,&id_val, MSM_CAMERA_I2C_BYTE_DATA);
				if(rc < 0){
					pr_err("%s: FLASHCHIP READ I2C error!\n", __func__);
					continue;
				}

				pr_info("%s, sy7806e id:0x%x, rc = %d\n", __func__,id_val,rc );

				if ( FLASH_CHIP_ID == id_val ){
					rc = 0;
					break;
				}
			}

			if( i >= 3 ){
				pr_err("%s failed\n",__func__);
				rc = -ENODEV;
			}
		}
		gpio_set_value_cansleep(flash_ctrl->power_info.gpio_conf->gpio_num_info->gpio_num[SENSOR_GPIO_FL_EN],0);

	} else {
		pr_err("%s,gpio is NULL\n", __func__);
		return -EINVAL;
	}
	return rc;
}
