#include "msm_flash.h"

int32_t hw_flash_i2c_aw36413_init(struct msm_flash_ctrl_t *flash_ctrl,
		struct msm_flash_cfg_data_t *flash_data);

int32_t hw_flash_i2c_aw36413_release(struct msm_flash_ctrl_t *flash_ctrl);

int32_t hw_flash_i2c_aw36413_low(struct msm_flash_ctrl_t *flash_ctrl,
		struct msm_flash_cfg_data_t *flash_data);

int32_t hw_sub_flash_i2c_aw36413_low(struct msm_flash_ctrl_t *flash_ctrl,
		struct msm_flash_cfg_data_t *flash_data);

int32_t hw_flash_i2c_aw36413_high(struct msm_flash_ctrl_t *flash_ctrl,
		struct msm_flash_cfg_data_t *flash_data);

int32_t hw_sub_flash_i2c_aw36413_high(struct msm_flash_ctrl_t *flash_ctrl,
		struct msm_flash_cfg_data_t *flash_data);

int32_t hw_flash_i2c_aw36413_off(struct msm_flash_ctrl_t *flash_ctrl,
		struct msm_flash_cfg_data_t *flash_data);

int32_t hw_sub_flash_i2c_aw36413_off(struct msm_flash_ctrl_t *flash_ctrl,
		struct msm_flash_cfg_data_t *flash_data);
int32_t hw_flash_i2c_aw36413_torch(struct msm_flash_ctrl_t *flash_ctrl,
		struct msm_flash_cfg_data_t *flash_data);

int32_t hw_sub_flash_i2c_aw36413_torch(struct msm_flash_ctrl_t *flash_ctrl,
		struct msm_flash_cfg_data_t *flash_data);
int32_t hw_flash_i2c_aw36413_match_id(struct msm_flash_ctrl_t *flash_ctrl,
	struct i2c_flash_info_t*flash_info);
