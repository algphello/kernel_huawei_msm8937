#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/workqueue.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/time.h>
#include <linux/slab.h>
#include <linux/batterydata-lib.h>
#include <linux/interrupt.h>
#include <linux/wakelock.h>
#include <linux/huawei_adc.h>
#include <linux/power/huawei_dsm_charger.h>

#define CWFG_ENABLE_LOG 1 //CHANGE   Customer need to change this for enable/disable log
#define CWFG_I2C_BUSNUM 2 //CHANGE   Customer need to change this number according to the principle of hardware
#define DOUBLE_SERIES_BATTERY 0
#define DELAYED_TIME_2MS         2
#define DELAYED_TIME_50MS        50
#define MS_TO_SECOND             1000
#define queue_delayed_work_time  8000
#define CW_CHG_RECHECK_DELAY        (jiffies + msecs_to_jiffies(30000))
#define CW_CHECK_CHARGE_FULL_TIME        (jiffies + msecs_to_jiffies(1000 * 60 *45))
#define CW_PROPERTIES "cw-bms"

#define REG_VERSION             0x0
#define REG_VCELL               0x2
#define REG_SOC                 0x4
#define REG_RRT_ALERT           0x6
#define REG_CONFIG              0x8
#define REG_MODE                0xA
#define REG_VTEMPL              0xC
#define REG_VTEMPH              0xD
#define REG_BATINFO             0x10
#define REG_SOH_CYCLE           0x1
#define REG_CONFIG2             0xB
#define REG_PROFILE_ID          0x4E
#define REG_PROFILE_CAPACITY    0x4F
#define REG_PROFILE_CYCLE       0x49
#define REG_PROFILE_SOH         0x4A

#define MODE_SLEEP_MASK         (0x3<<6)
#define MODE_SLEEP              (0x3<<6)
#define MODE_NORMAL             (0x0<<6)
#define MODE_QUICK_START        (0x3<<4)
#define MODE_RESTART            (0xf<<0)

#define GET_CYCLE_COUNT         (0x00)
#define GET_SOH                 (0x80)

#define CONFIG_UPDATE_FLG       (0x1<<1)
#define ATHD                    (0x0<<3)        // ATHD = 0%

#define BATTERY_UP_MAX_CHANGE   600*1000            // The time for add capacity when charging
#define BATTERY_DOWN_MAX_CHANGE 64*1000
#define BATTERY_JUMP_TO_ZERO    30*1000
#define BATTERY_CAPACITY_ERROR  40*1000
#define BATTERY_CHARGING_ZERO   1800*1000
#define BATTERY_ADD_ONE         48*1000
#define VOLTAGE_FOR_CHARGING_OPEN 3800

#define CHARGING_ON 1
#define NO_CHARGING 0
#define DESIGN_CAPACITY 3000   //mAH mA
#define UI_FULL 98
#define BAT_LOW_INTERRUPT 1
#define FULL_CAPACITY 100
#define HALF_CAPACITY 50
#define EMPTY_CAPACITY 0
#define SIZE_BATINFO    64
#define BAT_NUM 3
#define PROFILE_NUM 4
#define TRUE    1
#define FALSE   0
#define CUR_VAL_MASK        0x1fff
#define CUR_BIT              8
#define MAX_VAL             8192
#define PLUS_VAL             4095
#define RATIO_1024             1024
#define RATIO_625           625
#define RATIO_2048             2048
#define RATIO_256            256
#define RATIO_90            90
#define RATIO_10            10
#define RATIO_5             5
#define RATIO_4               4
#define RATIO_3             3
#define RATIO_100               100
#define FG_CYCLE_MS      1000
#define FG_DEBUG_MS      	200
#define FG_DELAY_MS      	10
#define FG_READ_SOC_MS       120
#define LOW_CAPACITY        2
#define CAP_STEP            1
#define CAP_95            95
#define CAP_90            90
#define CAP_80            80
#define CAP_0             0
#define DEFAULT_BATT_TEMP 250
#define OFFSET_BIT  8
#define CLR_ATHD         0x07
#define CLR_CAP          0xFF
#define NEW_ATHD_MASK   0xf8
#define RRT_ALERT_MASK   0x7f
#define WR_TIME   30
#define SOC_RANGE   0x64
#define DUL_BAT     2
#define DEFAULT_VOL   4200
#define RETRY_TIME      5
#define WAKE_LOCK_TIMEOUT       (10 * HZ)
#define RRT_OFFSET       (7)
#define LOOP_MAX       200
#define MV_UV_UNIT            1000
#define MA_UV_UNIT            1000
#define UV_2_MV   1000
#define PROFILE_ID_MASK    0x03

#define SOC_MODIFY_SPEED_THOLD1 50
#define SOC_MODIFY_SPEED_THOLD2 25
#define SOC_MODIFY_SPEED_THOLD3 10
#define SPEED_DIVID2 2
#define SPEED_DIVID4 4
#define SPEED_DIVID8 8
#define SOC_DIGIT_MODIFY_SET 50
#define MODIFY_SPEED_DIVID3 3
#define SOC_DESCEND_RANGE1 1
#define SOC_DESCEND_RANGE2 2


static int profile_ID = 0;
static int get_cycle_mark = 0;
static int g_need_start_timer_flag = FALSE;
static int g_full_now_flag = FALSE;
static int chg_full_check_mark = FALSE;
static int full_stop_charger_mark = FALSE;
#define CWFG_NAME "cw2015"

static int max_voltage_level[PROFILE_NUM] = {
	4400, 4380, 4350, 4200,
};

#ifdef CONFIG_PM
static struct timespec suspend_time_before;
static struct timespec after;
static int suspend_resume_mark = 0;
#endif

struct cw_battery {
    struct i2c_client *client;
	struct device		*dev;

    struct workqueue_struct *cwfg_workqueue;
	struct delayed_work battery_delay_work;
	
	struct power_supply bms_psy;
	struct cw_batt_data  *batt_data; 
    int charger_mode;
    int capacity;
    int real_soc;
    int real_soc_dig;
    int voltage;
    int status;
	int change;
    int cw_current;
	int design_capacity;
	int FCC;
	int cycle_count;

    struct device_node *therm_vadc_node;
    struct device_node *id_vadc_node;
    int therm_switch_mode;
    int id_switch_mode;
	int batt_id_kohm;

    struct power_supply *batt_psy;
    struct power_supply *usb_psy;
    struct timer_list chg_check_timer;
    struct timer_list chg_full_check_timer;
	int version_id;
};

const struct qpnp_vadc_map_pt adcmap_batt_therm[] = {
	{-400, 1709},
	{-390, 1704},
	{-380, 1699},
	{-370, 1694},
	{-360, 1688},
	{-350, 1683},
	{-340, 1677},
	{-330, 1671},
	{-320, 1665},
	{-310, 1658},
	{-300, 1651},
	{-290, 1644},
	{-280, 1637},
	{-270, 1629},
	{-260, 1621},
	{-250, 1613},
	{-240, 1605},
	{-230, 1596},
	{-220, 1587},
	{-210, 1578},
	{-200, 1568},
	{-190, 1558},
	{-180, 1548},
	{-170, 1538},
	{-160, 1527},
	{-150, 1516},
	{-140, 1505},
	{-130, 1493},
	{-120, 1481},
	{-110, 1469},
	{-100, 1456},
	{-90, 1444},
	{-80, 1430},
	{-70, 1417},
	{-60, 1404},
	{-50, 1390},
	{-40, 1376},
	{-30, 1361},
	{-20, 1347},
	{-10, 1332},
	{00, 1317},
	{10, 1301},
	{20, 1286},
	{30, 1270},
	{40, 1254},
	{50, 1238},
	{60, 1222},
	{70, 1206},
	{80, 1189},
	{90, 1173},
	{100, 1156},
	{110, 1139},
	{120, 1122},
	{130, 1105},
	{140, 1088},
	{150, 1071},
	{160, 1054},
	{170, 1036},
	{180, 1019},
	{190, 1002},
	{200, 985},
	{210, 968},
	{220, 950},
	{230, 933},
	{240, 916},
	{250, 900},
	{260, 883},
	{270, 866},
	{280, 849},
	{290, 833},
	{300, 817},
	{310, 800},
	{320, 784},
	{330, 768},
	{340, 753},
	{350, 737},
	{360, 722},
	{370, 706},
	{380, 691},
	{390, 677},
	{400, 662},
	{410, 648},
	{420, 634},
	{430, 620},
	{440, 606},
	{450, 592},
	{460, 579},
	{470, 566},
	{480, 553},
	{490, 540},
	{500, 528},
	{510, 516},
	{520, 504},
	{530, 492},
	{540, 481},
	{550, 470},
	{560, 459},
	{570, 448},
	{580, 437},
	{590, 427},
	{600, 417},
	{610, 407},
	{620, 397},
	{630, 388},
	{640, 379},
	{650, 370},
	{660, 361},
	{670, 352},
	{680, 344},
	{690, 335},
	{700, 327},
	{710, 320},
	{720, 312},
	{730, 304},
	{740, 297},
	{750, 290},
	{760, 283},
	{770, 276},
	{780, 270},
	{790, 263},
	{800, 257},
	{810, 251},
	{820, 245},
	{830, 239},
	{840, 233},
	{850, 228},
	{860, 222},
	{870, 217},
	{880, 212},
	{890, 207},
	{900, 202},
	{910, 197},
	{920, 193},
	{930, 188},
	{940, 184},
	{950, 179},
	{960, 175},
	{970, 171},
	{980, 167},
	{990, 163},
	{1000, 159}
};


static void cw_update_current(struct cw_battery *cw_bms);
static void cw_update_FCC_and_cycle(struct cw_battery *cw_bms);
static void cw_init_FCC_and_cycle(struct cw_battery *cw_bat);

/*Define CW2015 iic read function*/
int cw_read(struct i2c_client *client, unsigned char reg, unsigned char buf[])
{
	int ret = 0;

    if (!client) {
        pr_err("%s: invalid param, fatal error\n", __func__);
        return -EINVAL;
    }

	ret = i2c_smbus_read_i2c_block_data( client, reg, 1, buf);
	pr_debug("%2x = %2x\n", reg, buf[0]);
	return ret;
}
/*Define CW2015 iic write function*/		
int cw_write(struct i2c_client *client, unsigned char reg, unsigned char const buf[])
{
	int ret = 0;

    if (!client) {
        pr_err("%s: invalid param, fatal error\n", __func__);
        return -EINVAL;
    }

    /* write 1 byte */
	ret = i2c_smbus_write_i2c_block_data( client, reg, 1, &buf[0] );
	pr_debug("%2x = %2x\n", reg, buf[0]);
	return ret;
}
/*Define CW2015 iic read word function*/	
int cw_read_word(struct i2c_client *client, unsigned char reg, unsigned char buf[])
{
	int ret = 0;

    if (!client) {
        pr_err("%s: invalid param, fatal error\n", __func__);
        return -EINVAL;
    }

    /* read 2 bytes */
	ret = i2c_smbus_read_i2c_block_data( client, reg, 2, buf );
	pr_debug("%2x = %2x %2x\n", reg, buf[0], buf[1]);
	return ret;
}

int get_profile_ID(struct cw_battery *cw_bms)
{
	int ret = 0;
	unsigned char reg_val = 0;

    if (!cw_bms) {
        pr_err("%s: invalid param, fatal error\n", __func__);
        return 0;
    }
	
	pr_info("\n");
	ret = cw_read(cw_bms->client, REG_PROFILE_ID, &reg_val);
	if(ret < 0)
		return 0;
	reg_val = reg_val & PROFILE_ID_MASK;
	return reg_val;
}

int get_bat_ID(struct cw_battery *bms)
{
	int volt = 0;

    if (!bms) {
        pr_err("%s: invalid param, fatal error\n", __func__);
        return 0;
    }

    volt = get_adc_converse_voltage(bms->id_vadc_node, bms->id_switch_mode);
    pr_info("battery id voltage %d\n", volt);
	return volt * UV_2_MV;
}

int get_batt_temp(struct cw_battery *bms)
{
    int ret = 0;
    int volt = 0;
    int32_t result = 0;

    if (!bms) {
        pr_err("%s: invalid param, fatal error\n", __func__);
        return DEFAULT_BATT_TEMP;
    }

    ret = get_adc_converse_temp(bms->therm_vadc_node, bms->therm_switch_mode,
						adcmap_batt_therm, ARRAY_SIZE(adcmap_batt_therm), &result);
    if (ret < 0) {
        pr_err("Read battery temp error, ret default value 250\n");
        return DEFAULT_BATT_TEMP;
    }

    return result;
}

/*CW2015 update profile function, Often called during initialization*/
int cw_update_config_info(struct cw_battery *cw_bms)
{
    int ret = 0;
    unsigned char reg_val = 0;
    int i = 0;
    unsigned char reset_val = 0;
	unsigned char model_data = 0;

    if (!cw_bms) {
        pr_err("%s: invalid param, fatal error\n", __func__);
        return -EINVAL;
    }

    // make sure no in sleep mode
    ret = cw_read(cw_bms->client, REG_MODE, &reg_val);
    if(ret < 0) {
        return ret;
    }

    reset_val = reg_val;
    if((reg_val & MODE_SLEEP_MASK) == MODE_SLEEP) {
        return -1;
    }

    // update new battery info
    for (i = 0; i < SIZE_BATINFO; i++) {
		model_data = cw_bms->batt_data->model_data[profile_ID][i];
        ret = cw_write(cw_bms->client, REG_BATINFO + i, &model_data);
        if(ret < 0) 
			return ret;
    }
	reg_val = CLR_CAP;
	ret = cw_write(cw_bms->client, REG_PROFILE_CAPACITY, &reg_val);
    if(ret < 0) 
		return ret;

	reg_val = 0x0;
    reg_val |= CONFIG_UPDATE_FLG;   // set UPDATE_FLAG
    reg_val &= CLR_ATHD;                // clear ATHD
    reg_val |= ATHD;                // set ATHD
    ret = cw_write(cw_bms->client, REG_CONFIG, &reg_val);
    if(ret < 0) 
		return ret;
    // read back and check
    ret = cw_read(cw_bms->client, REG_CONFIG, &reg_val);
    if(ret < 0) {
        return ret;
    }

    if (!(reg_val & CONFIG_UPDATE_FLG)) {
		pr_err("Error: The new config set fail\n");
    }

    if ((reg_val & NEW_ATHD_MASK) != ATHD) {
		pr_err("Error: The new ATHD set fail\n");
    }

    // reset
    reset_val &= ~(MODE_RESTART);
    reg_val = reset_val | MODE_RESTART;
    ret = cw_write(cw_bms->client, REG_MODE, &reg_val);
    if(ret < 0) 
		return ret;

    msleep(FG_DELAY_MS);

    ret = cw_write(cw_bms->client, REG_MODE, &reset_val);
    if(ret < 0) 
		return ret;

	pr_info("cw2015 update config success!\n");

    return 0;
}

#define SOC_SHIFT   55   // This value use for no charge full state, very import, please do not modify. Please DO NOT Modiy.
static int cw_init_data(struct cw_battery *cw_bms)
{
	int ret = 0;
	int capacity_integer = 0;
	int capacity_decimal = 0;
	int cw_capacity = 0;
	unsigned char reg_val[2] = {0}, reg = 0;
	u16 value16 = 0;

    if (!cw_bms) {
        pr_err("%s: Invalid param, fatal error\n", __func__);
        return -EINVAL;
    }

	ret = cw_read_word(cw_bms->client, REG_SOC, reg_val);
	if (ret < 0)
		return ret;

	capacity_integer = reg_val[0]; //real SOC
	capacity_decimal = reg_val[1];
	cw_bms->real_soc = capacity_integer ;
	cw_bms->real_soc_dig = capacity_decimal;
	cw_capacity = ((capacity_integer * RATIO_256 + capacity_decimal) * FULL_CAPACITY)/ (UI_FULL*RATIO_256);
	if(cw_capacity >= FULL_CAPACITY){
		cw_capacity = FULL_CAPACITY;
	}
	cw_bms->capacity = cw_capacity;

	ret = cw_read_word(cw_bms->client, REG_VCELL, reg_val);
    if(ret < 0) {
        return ret;
    }
    value16 = (reg_val[0] << OFFSET_BIT) + reg_val[1];
    cw_bms->voltage = value16 * RATIO_625 / RATIO_2048;

	ret = cw_read(cw_bms->client, REG_PROFILE_CAPACITY, &reg);
	if (ret < 0)
		return ret;
	if(reg == CLR_CAP){
		reg = cw_capacity;
		ret = cw_write(cw_bms->client, REG_PROFILE_CAPACITY, &reg);
		if (ret < 0)
			return ret;
	}else{
		cw_bms->capacity = cw_capacity < reg ? cw_capacity : reg;

		if(cw_bms->voltage >= VOLTAGE_FOR_CHARGING_OPEN && cw_bms->capacity == 0 ){
			cw_bms->capacity = 1;
		}
	}
	
	reg = GET_SOH;
	ret = cw_write(cw_bms->client, REG_CONFIG2, &reg);
	if(ret < 0) {
		return ret;
	}
	cw_update_current(cw_bms);
	cw_init_FCC_and_cycle(cw_bms);

	pr_info("cw_bms->charger_mode = %d\n", cw_bms->charger_mode);
	pr_info("cw_bms->capacity = %d\n", cw_bms->capacity);
	pr_info("cw_bms->voltage = %d\n", cw_bms->voltage);
	pr_info("cw_bms->status = %d\n", cw_bms->status);
	pr_info("cw_bms->cw_current = %d\n", cw_bms->cw_current);
	pr_info("cw_bms->design_capacity = %d\n", cw_bms->design_capacity);
	pr_info("cw_bms->FCC = %d\n", cw_bms->FCC);
	pr_info("cw_bms->cycle_count = %d\n", cw_bms->cycle_count);

	return 0;
}

/*CW2015 init function, Often called during initialization*/
static int cw_init(struct cw_battery *cw_bms, int boot_strap)
{
    int ret = 0;
    int i = 0;
    unsigned char reg_val = MODE_SLEEP;

    if (!cw_bms) {
        pr_err("%s: invalid param, fatal error\n", __func__);
        return -EINVAL;
    }

    if ((reg_val & MODE_SLEEP_MASK) == MODE_SLEEP) {
        reg_val = MODE_NORMAL;
        ret = cw_write(cw_bms->client, REG_MODE, &reg_val);
        if (ret < 0) 
            return ret;
    }

    ret = cw_read(cw_bms->client, REG_CONFIG, &reg_val);
    if (ret < 0)
    	return ret;

    if ((reg_val & NEW_ATHD_MASK) != ATHD) {
        reg_val &= CLR_ATHD;    /* clear ATHD */
        reg_val |= ATHD;    /* set ATHD */
        ret = cw_write(cw_bms->client, REG_CONFIG, &reg_val);
        if (ret < 0)
            return ret;
#ifdef BAT_LOW_INTERRUPT
        ret = cw_read(cw_bms->client, REG_RRT_ALERT, &reg_val);
        if (ret < 0)
            return ret;
        reg_val = reg_val & RRT_ALERT_MASK;
        ret = cw_write(cw_bms->client, REG_RRT_ALERT, &reg_val);
        if(ret < 0) {
            return ret;
        }//clear alrt 
#endif
    }

    ret = cw_read(cw_bms->client, REG_CONFIG, &reg_val);
    if (ret < 0) 
        return ret;

	if(!boot_strap){
        ret = cw_update_config_info(cw_bms);
        if (ret < 0) {
			pr_err("%s : update config fail\n", __func__);
            return ret;
        }
		reg_val = cw_bms->FCC * RATIO_100 / cw_bms->design_capacity;
		ret = cw_write(cw_bms->client, REG_PROFILE_SOH, &reg_val);
        if (ret < 0) {
            return ret;
        }
		reg_val = cw_bms->cycle_count / RATIO_4;
		ret = cw_write(cw_bms->client, REG_PROFILE_CYCLE, &reg_val);
        if (ret < 0) {
            return ret;
        }
	}else{
	    if (!(reg_val & CONFIG_UPDATE_FLG)) {
			pr_info("update config flg is true, need update config\n");
			//need Chaman modify for get FCC change profile ID and CV
			//new battery insert, FCC and cycle change to defult
			profile_ID = 0;
	        ret = cw_update_config_info(cw_bms);
	        if (ret < 0) {
				pr_err("%s : update config fail\n", __func__);
	            return ret;
	        }
	    } else {
	    	profile_ID = get_profile_ID(cw_bms);
	    	for(i = 0; i < SIZE_BATINFO; i++) { 
		        ret = cw_read(cw_bms->client, (REG_BATINFO + i), &reg_val);
		        if (ret < 0)
		        	return ret;
		        if (REG_BATINFO + i == REG_PROFILE_SOH 
					|| REG_BATINFO + i == REG_PROFILE_CYCLE
					|| REG_BATINFO + i == REG_PROFILE_CAPACITY){
					pr_info("jump continue i = %d, config_info[profile_ID][%d] = %d\n", 
						i, i, cw_bms->batt_data->model_data[profile_ID][i]);
					continue;
				}
		       if (cw_bms->batt_data->model_data[profile_ID][i] != reg_val)
		            break;
	        }
	        if (i != SIZE_BATINFO) {
				pr_info("config didn't match, need update config\n");
	        	ret = cw_update_config_info(cw_bms);
	            if (ret < 0){
	                return ret;
	            }
	        }
	    }
	}
	msleep(FG_DELAY_MS);
    for (i = 0; i < WR_TIME; i++) {
        ret = cw_read(cw_bms->client, REG_SOC, &reg_val);
        if (ret < 0)
            return ret;
        else if (reg_val <= SOC_RANGE)
            break;
        msleep(FG_READ_SOC_MS);
    }

    if (i >= WR_TIME ){
    	 reg_val = MODE_SLEEP;
         ret = cw_write(cw_bms->client, REG_MODE, &reg_val);
         pr_err("cw2015 input unvalid power error, cw2015 join sleep mode\n");
         return -EINVAL;
    } 

	pr_info("cw2015 init success!\n");	
    return 0;
}

static int get_charge_state(void)
{
	int present = 0;
	union power_supply_propval ret = {0,};
	struct power_supply *usb_psy = NULL;

	usb_psy = power_supply_get_by_name("usb");
    if (!usb_psy) {
        pr_err("%s: Cannot get usb psy, fatal error\n", __func__);
        return -ENODEV;
    }
	usb_psy->get_property(usb_psy,POWER_SUPPLY_PROP_PRESENT, &ret);
	present = ret.intval ;

    return present;
}

static int cw_por(struct cw_battery *cw_bms)
{
	int ret = 0;
	unsigned char reset_val = 0;

    if (!cw_bms) {
        pr_err("%s: invalid param, fatal error\n", __func__);
        return -EINVAL;
    }
	
	reset_val = MODE_SLEEP;
	ret = cw_write(cw_bms->client, REG_MODE, &reset_val);
	if (ret < 0)
		return ret;
	reset_val = MODE_NORMAL;
	msleep(FG_DELAY_MS);
	ret = cw_write(cw_bms->client, REG_MODE, &reset_val);
	if (ret < 0)
		return ret;
	ret = cw_init(cw_bms, FALSE);
	if (ret) 
		return ret;
	return 0;
}

static int cw_capacity_second_change(int cap, struct cw_battery *cw_bms)
{
	static int second_cap_timer = 0;
	int cap_second = 0;
	int ret = 0;
	int up_speed  = BATTERY_ADD_ONE / queue_delayed_work_time;
	unsigned char reg_val = 0;
	if(cap == 0 && cw_bms->voltage >= VOLTAGE_FOR_CHARGING_OPEN && cw_bms->charger_mode > 0){
		cap_second = 1;
		return cap_second;
	}

	if(cw_bms->charger_mode == 0 && cap > cw_bms->capacity){
		cap_second = cw_bms->capacity;
	}else if(cw_bms->charger_mode > 0 && cap > cw_bms->capacity + 1){
		second_cap_timer ++;
		if( cap - cw_bms->capacity > SOC_MODIFY_SPEED_THOLD1 ){
			up_speed = up_speed / SPEED_DIVID2;
		}else if(cap - cw_bms->capacity > SOC_MODIFY_SPEED_THOLD2 ){
			up_speed = up_speed / SPEED_DIVID2 + up_speed / SPEED_DIVID8;
		}else if(cap - cw_bms->capacity > SOC_MODIFY_SPEED_THOLD3 ){
			up_speed = up_speed / SPEED_DIVID2 + up_speed / SPEED_DIVID4;
		}else{
			up_speed = up_speed;
		}
		if(second_cap_timer > up_speed){
			cap_second = cw_bms->capacity + 1;
			second_cap_timer = 0;
		}else{
			cap_second = cw_bms->capacity;
		}
	}else{
		second_cap_timer = 0;
		cap_second = cap;
	}

	return cap_second;
}
#define REMAINDER_DECIMAL_UPPER    90
#define REMAINDER_DECIMAL_LOWER    10
static int cw_get_capacity(struct cw_battery *cw_bms)
{
	int cw_capacity = 0;
	int capacity_decimal = 0;
	int capacity_integer = 0;
	int ret = 0;
	unsigned char reg_val[2] = {0};

	static int reset_loop = 0;
	static int charging_5_loop = 0;
	int remainder = 0;

    if (!cw_bms) {
        pr_err("%s: invalid param, fatal error\n", __func__);
        return -EINVAL;
    }
	
	ret = cw_read_word(cw_bms->client, REG_SOC, reg_val);
	if (ret < 0)
		return ret;

	capacity_integer = reg_val[0]; //real SOC
	capacity_decimal = reg_val[1];

	pr_info("cw_get_capacity : IC SOC 04 = %d\n", capacity_integer );

	cw_bms->real_soc = capacity_integer;
	cw_bms->real_soc_dig = capacity_decimal;
	pr_info("cw_get_capacity :  cw_bms->real_soc = %d cw_bms->real_soc_dig = %d\n", cw_bms->real_soc, cw_bms->real_soc_dig);
	cw_capacity = ((capacity_integer * RATIO_256 + capacity_decimal) * RATIO_100)/ (UI_FULL*RATIO_256);
	remainder = ((((capacity_integer * RATIO_256 + capacity_decimal) * RATIO_100) * RATIO_100)/ (UI_FULL*RATIO_256)) % RATIO_100;
	pr_info("cw_get_capacity:  cw_capacity = %d   remainder = %d\n", cw_capacity, remainder);
	if(cw_capacity >= FULL_CAPACITY){
		cw_capacity = FULL_CAPACITY;
	}
	else 
	{
		if( (remainder > REMAINDER_DECIMAL_UPPER || remainder < REMAINDER_DECIMAL_LOWER) \
			&& cw_capacity >= cw_bms->capacity - 1 && cw_capacity <= cw_bms->capacity + 1) 
		{
			cw_capacity = cw_bms->capacity;
		}
	}
	pr_info("cw_get_capacity:  cw_capacity_fixed = %d\n", cw_capacity);

	if ((capacity_integer < 0) || (capacity_integer > FULL_CAPACITY)) {
		pr_err("Error:  capacity_integer = %d\n", capacity_integer);
		reset_loop++;
		if (reset_loop > (BATTERY_CAPACITY_ERROR / queue_delayed_work_time)){ 
			profile_ID = get_profile_ID(cw_bms);
			if (profile_ID < 0) {
				pr_err("Error: Read profile id error\n");
				profile_ID = 0;
			}
			cw_por(cw_bms);
			reset_loop =0;
		}

		return cw_bms->capacity; //cw_capacity Chaman change because I think customer didn't want to get error capacity.
	}else {
		reset_loop =0;
	}
	/*case 4 : avoid battery level is 0% when long time charging*/
	if((cw_bms->charger_mode > 0) &&(cw_capacity == CAP_0))
	{
		charging_5_loop++;
		if (charging_5_loop > BATTERY_CHARGING_ZERO / queue_delayed_work_time) {
			profile_ID = get_profile_ID(cw_bms);
			if (profile_ID < 0) {
				pr_err("Error: Read profile id error\n");
				profile_ID = 0;
			}
			cw_por(cw_bms);
			charging_5_loop = 0;
		}
	}else if(charging_5_loop != 0){
		charging_5_loop = 0;
	}
	#ifdef CONFIG_PM
	if(suspend_resume_mark == TRUE)
		suspend_resume_mark = FALSE;
	#endif
	return cw_capacity;
}


/*This function called when get voltage from cw2015*/
static int cw_get_voltage(struct cw_battery *cw_bms)
{
    int ret = 0;
    unsigned char reg_val[2] = {0};
    u16 value = 0, value_1 = 0, value_2 = 0, value_3 = 0;
    int voltage = 0;

    if (!cw_bms) {
        pr_err("%s: invalid param, fatal error\n", __func__);
        return -EINVAL;
    }

    ret = cw_read_word(cw_bms->client, REG_VCELL, reg_val);
    if(ret < 0) {
        return ret;
    }
    value = (reg_val[0] << CUR_BIT) + reg_val[1];

    ret = cw_read_word(cw_bms->client, REG_VCELL, reg_val);
    if(ret < 0) {
          return ret;
    }
    value_1 = (reg_val[0] << CUR_BIT) + reg_val[1];

    ret = cw_read_word(cw_bms->client, REG_VCELL, reg_val);
    if(ret < 0) {
        return ret;
    }
    value_2 = (reg_val[0] << CUR_BIT) + reg_val[1];

    if(value > value_1) {
        value_3 = value;
        value = value_1;
        value_1 = value_3;
    }

    if(value_1 > value_2) {
    value_3 =value_1;
    value_1 =value_2;
    value_2 =value_3;
    }

    if(value >value_1) {
    value_3 =value;
    value =value_1;
    value_1 =value_3;
    }

    voltage = value_1 * RATIO_625 / RATIO_2048;

	if(DOUBLE_SERIES_BATTERY)
		voltage = voltage * DUL_BAT;
    return voltage;
}

static void cw_update_charge_status(struct cw_battery *cw_bms)
{
	int cw_charger_mode = 0;
	int ret = 0;
	unsigned char reg_val = 0;

    if (!cw_bms) {
        pr_err("%s: invalid param, fatal error\n", __func__);
        return;
    }

	cw_charger_mode = get_charge_state();
	if(cw_bms->charger_mode != cw_charger_mode){
        cw_bms->charger_mode = cw_charger_mode;
		cw_bms->change = TRUE;
	}
}

static void update_charge_full_mark(struct cw_battery *cw_bms)
{
    int ret;
    union power_supply_propval val = {0, };

    if (!cw_bms->batt_psy) {
        cw_bms->batt_psy = power_supply_get_by_name("battery");
    }

    if (!cw_bms->batt_psy) {
        pr_err("%s: Cannot get battery power supply\n", __func__);
        return;
    }
    ret = cw_bms->batt_psy->get_property(cw_bms->batt_psy,
                                   POWER_SUPPLY_PROP_BATTERY_CHARGING_ENABLED, &val);
    if (ret < 0)
    {
        pr_err("%s: Get property from battery power supply fail\n", __func__);
        return;
    }

    pr_err("%s, chg_full_check_mark = %d, full_stop_charger_mark = %d, cw_bms->charger_mode = %d, val.intval[charge status] = %d, cw_bms->capacity = %d\n",
		__func__, chg_full_check_mark,  full_stop_charger_mark, cw_bms->charger_mode, val.intval, cw_bms->capacity);
    if(cw_bms->charger_mode > 0 && val.intval == true && cw_bms->capacity >= FULL_CAPACITY){
        pr_err("in full check\n");
        if(chg_full_check_mark != TRUE && full_stop_charger_mark != TRUE){
	        pr_err("in full check chg_full_check_mark = true\n");
            mod_timer(&cw_bms->chg_full_check_timer, CW_CHECK_CHARGE_FULL_TIME);
            chg_full_check_mark = TRUE;
        }
    }else{
        pr_err("out full check\n");
        if(full_stop_charger_mark != FALSE){
            full_stop_charger_mark = FALSE;
        }
        if(chg_full_check_mark != FALSE){
	         pr_err("out full check chg_full_check_mark = false\n");
             del_timer_sync(&cw_bms->chg_full_check_timer);
             chg_full_check_mark = FALSE;
        }
    }
}

static void cw_update_capacity(struct cw_battery *cw_bms)
{
    int cw_capacity = 0;
	int capacity_temp = 0;
	int ret = 0;
	unsigned char reg_val = 0;
	
    if (!cw_bms) {
        pr_err("%s: invalid param, fatal error\n", __func__);
        return;
    }

    cw_capacity = cw_get_capacity(cw_bms);
	capacity_temp = cw_capacity;
    cw_capacity = cw_capacity_second_change(cw_capacity, cw_bms);

    if ((cw_capacity >= EMPTY_CAPACITY) && (cw_capacity <= FULL_CAPACITY) && (cw_bms->capacity != cw_capacity)) {				
        cw_bms->capacity = cw_capacity;
		reg_val = cw_capacity;
		ret = cw_write(cw_bms->client, REG_PROFILE_CAPACITY, &reg_val);
		if(ret < 0){
			pr_err("write IIC error\n");
		}
		cw_bms->change = TRUE;
    }
	update_charge_full_mark(cw_bms);
}

static void cw_update_vol(struct cw_battery *cw_bms)
{
    int ret = 0;

    if (!cw_bms) {
        pr_err("%s: invalid param, fatal error\n", __func__);
        return;
    }

    ret = cw_get_voltage(cw_bms);
    if ((ret >= 0) && (cw_bms->voltage != ret)) {
        cw_bms->voltage = ret;
		cw_bms->change = TRUE;
    }
}

static void cw_update_status(struct cw_battery *cw_bms)
{
    int status = 0;

    if (!cw_bms) {
        pr_err("%s: invalid param, fatal error\n", __func__);
        return;
    }

    if (cw_bms->charger_mode > 0) {
        if (cw_bms->capacity >= FULL_CAPACITY) 
            status = POWER_SUPPLY_STATUS_FULL;
        else
            status = POWER_SUPPLY_STATUS_CHARGING;
    } else {
        status = POWER_SUPPLY_STATUS_DISCHARGING;
    }

    if (cw_bms->status != status) {
        cw_bms->status = status;
        cw_bms->change = TRUE;
    } 
}

static void cw_update_current(struct cw_battery *cw_bms)
{
    int ret = 0;
    unsigned char reg_val[2] = {0};
    long value = 0;
    unsigned char reg_val_config = 0;

    if (!cw_bms) {
        pr_err("%s: invalid param, fatal error\n", __func__);
        return;
    }

    ret = cw_read(cw_bms->client, REG_CONFIG2, &reg_val_config);
    if (ret < 0)
        return;
    if(reg_val_config == GET_CYCLE_COUNT){
        return;
    }

    ret = cw_read_word(cw_bms->client, REG_RRT_ALERT, reg_val);
    if (ret < 0)
        return ;

    value = ((reg_val[0] << CUR_BIT) + reg_val[1]) & CUR_VAL_MASK;
    if(value > PLUS_VAL){
        value = value - MAX_VAL;
    }
    cw_bms->cw_current = value * cw_bms->design_capacity / RATIO_1024;
    pr_info("cw_bms->cw_current = %d\n",cw_bms->cw_current);
}

static void cw_update_FCC_and_cycle(struct cw_battery *cw_bat)
{
    int ret = 0, last_fcc = 0;
    static int get_cycle_from_reg = 0;
    unsigned char reg_val_config = 0;
    unsigned char reg_val = 0;
    unsigned char reg_val_pCycle = 0;
#ifdef CONFIG_HUAWEI_PMU_DSM
    char learned_cc_info[DSM_POST_BUF_SIZE] = {0, };
#endif

    ret = cw_read(cw_bat->client, REG_CONFIG2, &reg_val_config);
    if (ret < 0)
        return;

    ret = cw_read(cw_bat->client, REG_SOH_CYCLE, &reg_val);
    if (ret < 0)
        return;

    last_fcc = cw_bat->FCC;
    if(reg_val_config == GET_SOH && reg_val != 0){
        cw_bat->FCC = cw_bat->design_capacity * reg_val / RATIO_100;
        ret = cw_write(cw_bat->client, REG_PROFILE_SOH, &reg_val);
        if (ret < 0)
            return;
    }else if(reg_val_config == GET_CYCLE_COUNT){
        get_cycle_from_reg++;
        ret = cw_read(cw_bat->client, REG_PROFILE_CYCLE, &reg_val_pCycle);
        if (ret < 0)
            return;
        if(reg_val >= reg_val_pCycle && reg_val < reg_val_pCycle + RATIO_5){
            get_cycle_from_reg = RATIO_10;
            cw_bat->cycle_count = reg_val * RATIO_4;
            ret = cw_write(cw_bat->client, REG_PROFILE_CYCLE, &reg_val);
            if (ret < 0)
                return;
        }
    }

    if(reg_val_config == GET_CYCLE_COUNT && get_cycle_from_reg >= RATIO_3){
        reg_val = GET_SOH;
        ret = cw_write(cw_bat->client, REG_CONFIG2, &reg_val); 
        if (ret < 0) 
            return; 
        get_cycle_from_reg = 0;
    }

    if(get_cycle_mark == 0 && cw_bat->charger_mode > 0 && cw_bat->capacity >= RATIO_90){
        get_cycle_mark = 1;
        reg_val = GET_CYCLE_COUNT;
        ret = cw_write(cw_bat->client, REG_CONFIG2, &reg_val); 
        if (ret < 0) 
            return;
    }else if(get_cycle_mark == 1 && cw_bat->charger_mode == 0 && cw_bat->capacity <= RATIO_10){
        get_cycle_mark = 0;
    }

    if (last_fcc != cw_bat->FCC) {
        /* need to report fcc change event */
        cap_learning_event_done_notify();
        pr_info("new learned cc: %d, chg_cycle: %d\n",
                cw_bat->FCC, cw_bat->cycle_count);
#ifdef CONFIG_HUAWEI_PMU_DSM
        snprintf(learned_cc_info, DSM_POST_BUF_SIZE,
                "learned cc: %d, chg_cycle: %d\n",
                cw_bat->FCC, cw_bat->cycle_count);
        dsm_post_chg_bms_info(DSM_BMS_LEARN_CC, learned_cc_info);
#endif
    }

    printk("cw_bat->FCC = %d\n",cw_bat->FCC);
    printk("cw_bat->cycle_count = %d\n",cw_bat->cycle_count);
}

static void cw_init_FCC_and_cycle(struct cw_battery *cw_bat)
{
    int ret = 0;
    unsigned char reg_val = 0;

    ret = cw_read(cw_bat->client, REG_PROFILE_CYCLE, &reg_val);
    if (ret < 0)
        return;
    cw_bat->cycle_count = reg_val * RATIO_4;

    ret = cw_read(cw_bat->client, REG_PROFILE_SOH, &reg_val);
    if (ret < 0)
        return;
    cw_bat->FCC = cw_bat->design_capacity * reg_val / RATIO_100;

    printk("cw_bat->FCC = %d\n",cw_bat->FCC);
    printk("cw_bat->cycle_count = %d\n",cw_bat->cycle_count);
}

/* if real soc is full, stop charging
* if real soc is below 98%, start charging
*/
static void cw_chg_full_and_rechg_poll(struct cw_battery *cw_bms)
{
    int ret = 0;
    int chg_en = 0;
    union power_supply_propval val = {0, };
    int present = FALSE;

    if (!cw_bms) {
        pr_err("%s: invalid param, fatal error\n", __func__);
        return -EINVAL;
    }

    /*get usb_psy*/
    if (!cw_bms->usb_psy) {
        cw_bms->usb_psy = power_supply_get_by_name("usb");
    }

    if (!cw_bms->usb_psy) {
        pr_err("%s: Cannot get usb psy, fatal error\n", __func__);
    }
    else
    {
        /*get current usb persent status*/
        cw_bms->usb_psy->get_property(cw_bms->usb_psy, POWER_SUPPLY_PROP_PRESENT, &val);
        present = val.intval ;
    }

    /* get batt_psy*/
    if (!cw_bms->batt_psy) {
        cw_bms->batt_psy = power_supply_get_by_name("battery");
    }

    if (!cw_bms->batt_psy) {
        pr_err("%s: Cannot get battery power supply\n", __func__);
    }
    else
    {
        /*get current charging status*/
        ret = cw_bms->batt_psy->get_property(cw_bms->batt_psy,
                    POWER_SUPPLY_PROP_BATTERY_CHARGING_ENABLED, &val);
        if (ret < 0)
        {
            pr_err("%s: Get property from battery power supply fail\n", __func__);
        }
        else
        {
            chg_en = val.intval;
            /* if real soc is full, stop charging*/
            if ( (full_stop_charger_mark == TRUE) && (TRUE == present) )
            {
                /*when charge done, need start the check timer*/
                g_need_start_timer_flag = TRUE;
                g_full_now_flag = TRUE;
                pr_info("%s: soc is full, stop charging,chg_en = %d\n", __func__, chg_en);
                if (chg_en == TRUE)
                {
                    val.intval = false;
                    cw_bms->batt_psy->set_property(cw_bms->batt_psy,
                            POWER_SUPPLY_PROP_BATTERY_CHARGING_ENABLED, &val);
                    
                }
            }
            /* if real soc is below 98%, start charging*/
            else if ( (((cw_bms->real_soc <= UI_FULL) && (cw_bms->real_soc_dig < SOC_SHIFT)) || (cw_bms->real_soc < UI_FULL)) && (TRUE == present) )
            {
                /*when charge status, not need start the check timer*/
                g_need_start_timer_flag = FALSE;
                pr_info("%s: soc is below 98%%, start charging,chg_en = %d \n", __func__, chg_en);
                if (chg_en == FALSE)
                {
                    val.intval = true;
                    cw_bms->batt_psy->set_property(cw_bms->batt_psy,
                            POWER_SUPPLY_PROP_BATTERY_CHARGING_ENABLED, &val);
                }
            }
            else
            {
                pr_debug("%s: soc is %d now,soc dig is %d now, other status, do nothing\n", \
						__func__, cw_bms->real_soc, cw_bms->real_soc_dig);
            }

            if ( cw_bms->capacity < FULL_CAPACITY )
            {
                g_full_now_flag = FALSE;
                pr_info("%s: not full, set g_full_now_flag = %d\n", __func__, g_full_now_flag);
            }

            if ( FALSE == present )
            {
                /*when usb absent, clear full_flag and need_timer_flag*/
                g_need_start_timer_flag = FALSE;
                g_full_now_flag = FALSE;
                pr_info("%s: usb is absent, clear g_full_now_flag and g_need_start_timer_flag.\n", __func__);
            }
        }
    }
}

static void cw_bat_work(struct work_struct *work)
{
    struct delayed_work *delay_work = NULL;
    struct cw_battery *cw_bms = NULL;
    /*Add for battery swap start*/
    int ret = 0;
    unsigned char reg_val = 0;
    int i = 0;
    /*Add for battery swap end*/
    static int report_done_event = 0;

    if (!work) {
        pr_err("%s: invalid param, fatal error\n", __func__);
        return;
    }

    delay_work = container_of(work, struct delayed_work, work);
    cw_bms = container_of(delay_work, struct cw_battery, battery_delay_work);

    /*Add for battery swap start*/
    ret = cw_read(cw_bms->client, REG_MODE, &reg_val);
    if(ret < 0){
        //battery is out , you can send new battery capacity vol here what you want set
        //for example
        cw_bms->capacity = FULL_CAPACITY;
        cw_bms->voltage = DEFAULT_VOL;
        cw_bms->change = TRUE;
    }else{
        if((reg_val & MODE_SLEEP_MASK) == MODE_SLEEP){
            for(i = 0; i < RETRY_TIME; i++){
                profile_ID = get_profile_ID(cw_bms);
                if (profile_ID < 0) {
                    pr_err("Error: Read profile id error\n");
                    profile_ID = 0;
                }
                if(cw_por(cw_bms) == 0)
                    break;
            }
        }
        cw_update_capacity(cw_bms);
        cw_update_vol(cw_bms);
        cw_update_charge_status(cw_bms);
        cw_update_status(cw_bms);
        cw_update_current(cw_bms);
        cw_update_FCC_and_cycle(cw_bms);
    }

    /*Add for battery swap end*/
    pr_info("charger_mod = %d status = %d capacity = %d real_soc %d voltage = %d\n",
        cw_bms->charger_mode,cw_bms->status,cw_bms->capacity, cw_bms->real_soc, cw_bms->voltage);

    #ifdef CONFIG_PM
    if(suspend_resume_mark == TRUE)
        suspend_resume_mark = FALSE;
    #endif

    /*update the g_need_start_timer_flag*/
    cw_chg_full_and_rechg_poll(cw_bms);

    #ifdef CW_PROPERTIES
    if (cw_bms->change == TRUE){
        power_supply_changed(&cw_bms->bms_psy);
        cw_bms->change = FALSE;
    }
    #endif

    /* report charge done event */
    if (cw_bms->capacity >= FULL_CAPACITY && !report_done_event) {
        cap_charge_done_event_notify();
        report_done_event = 1;
    } else if (cw_bms->capacity < FULL_CAPACITY) {
        report_done_event = 0;
    }

    if (TRUE == g_need_start_timer_flag)
    {
        /*start the check timer*/
        mod_timer(&cw_bms->chg_check_timer, CW_CHG_RECHECK_DELAY);
    }
    else
    {
        queue_delayed_work(cw_bms->cwfg_workqueue, &cw_bms->battery_delay_work, msecs_to_jiffies(queue_delayed_work_time));
    }
}

static int change_profile(struct cw_battery *cw_bms, int max_voltage)
{
    int i = 0;

    if (!cw_bms) {
        pr_err("%s: invalid param, fatal error\n", __func__);
        return -EINVAL;
    }

	for(i = 0; i < PROFILE_NUM; i++){
		if(max_voltage == max_voltage_level[i]){
			break;
		}
	}
	if(i >= PROFILE_NUM){
		pr_err("set a error max voltage!!!\n");
		return -1;
	}
	if(i == get_profile_ID(cw_bms)){
		pr_err("Do not need change profile. It is match with max voltage!!!\n");
		return 0;
	}
	profile_ID = i;
	cw_por(cw_bms);
	return 0;
}

static int get_version_id(struct cw_battery *cw_bms)
{
	int ret = 0;
	unsigned char reg_val = 0;

	if(cw_bms->version_id == -1 ){
		ret = cw_read(cw_bms->client, REG_VERSION, &reg_val);
		if (ret < 0)
			return -1;
		cw_bms->version_id = reg_val;
	}
	return cw_bms->version_id;
}

static int update_battery_voltage_max(struct cw_battery *cw_bms, int voltage_max)
{
    union power_supply_propval val = {0, };
    int ret = 0;

    if (!cw_bms) {
        pr_err("%s: invalid param, fatal error\n", __func__);
        return -EINVAL;
    }

    /* get batt_psy*/
    if (!cw_bms->batt_psy) {
        cw_bms->batt_psy = power_supply_get_by_name("battery");
    }

    if (!cw_bms->batt_psy) {
        pr_err("%s: Cannot get battery power supply\n", __func__);
        return -ENODEV;
    }

    val.intval = voltage_max;
    ret = cw_bms->batt_psy->set_property(cw_bms->batt_psy,
                          POWER_SUPPLY_PROP_VOLTAGE_BASP, &val);

    return ret;
}

static int cw_reset(struct cw_battery *cw_bms)
{
	int ret = 0;
	unsigned char reset_val = 0;

	if (!cw_bms) {
		pr_err("%s: invalid param, fatal error\n", __func__);
		return -EINVAL;
	}

	reset_val = MODE_SLEEP;
	ret = cw_write(cw_bms->client, REG_MODE, &reset_val);
	if (ret < 0)
		return ret;
	reset_val = MODE_NORMAL;
	msleep(FG_DELAY_MS);
	ret = cw_write(cw_bms->client, REG_MODE, &reset_val);
	if (ret < 0)
		return ret;
	ret = cw_init(cw_bms, TRUE);
	if (ret)
		return ret;
	ret = cw_init_data(cw_bms);
    if (ret) {
		pr_err("%s : cw2015 init_data fail!\n", __func__);
        return ret;
    }
	return 0;
}

#define UV_TO_MV 1000
#ifdef CW_PROPERTIES
static int cw_battery_get_property(struct power_supply *psy,
                enum power_supply_property psp,
                union power_supply_propval *val)
{
    int ret = 0;
    struct cw_battery *cw_bms = NULL;

    if (!psy || !val) {
        pr_err("%s: invalid param, fatal error\n", __func__);
        return -EINVAL;
    }

    cw_bms = container_of(psy, struct cw_battery, bms_psy); 

    switch (psp) {
    case POWER_SUPPLY_PROP_CAPACITY:
            val->intval = cw_bms->capacity;
            break;
    case POWER_SUPPLY_PROP_CAPACITY_RAW:
            val->intval = cw_bms->real_soc;
            break;
    case POWER_SUPPLY_PROP_ENERGY_FULL:
            val->intval = g_full_now_flag;
            break;
    case POWER_SUPPLY_PROP_HEALTH:   //Chaman charger ic will give a real value
            val->intval= POWER_SUPPLY_HEALTH_GOOD;
            break;
    case POWER_SUPPLY_PROP_PRESENT:
            val->intval = cw_bms->voltage <= 0 ? 0 : 1;
            break;
    case POWER_SUPPLY_PROP_VOLTAGE_OCV:      
    case POWER_SUPPLY_PROP_VOLTAGE_NOW:
            val->intval = cw_bms->voltage * UV_TO_MV;
            break;

    case POWER_SUPPLY_PROP_TECHNOLOGY:  //Chaman this value no need
            val->intval = POWER_SUPPLY_TECHNOLOGY_LION;
            break;

	case POWER_SUPPLY_PROP_BATTERY_TYPE:
			val->strval = cw_bms->batt_data->battery_type;
			break;

	case POWER_SUPPLY_PROP_CURRENT_NOW:
			val->intval = (cw_bms->cw_current)*MA_UV_UNIT;  //current Chaman
			break;

	case POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN:
			val->intval = cw_bms->batt_data->nom_cap_uah;  // design capacity(MAH) Chaman
			break;
    case POWER_SUPPLY_PROP_CHARGE_COUNTER:
	case POWER_SUPPLY_PROP_CHARGE_FULL:
			val->intval = cw_bms->FCC*MV_UV_UNIT;  //FCC(MAH) Chaman
			break;
	case POWER_SUPPLY_PROP_CYCLE_COUNT:
    case POWER_SUPPLY_PROP_CHG_CYCLE_COUNT:
			val->intval = cw_bms->cycle_count;  //cycle count Chaman
			break;

    case POWER_SUPPLY_PROP_VOLTAGE_MAX:
            val->intval = max_voltage_level[profile_ID];
            break;

    case POWER_SUPPLY_PROP_RESISTANCE_ID:
            val->intval = cw_bms->batt_id_kohm;
            break;

    case POWER_SUPPLY_PROP_PROFILE_STATUS:
			/* TODO: make sure */
            val->intval = TRUE;
            break;

    case POWER_SUPPLY_PROP_TEMP:
            val->intval = get_batt_temp(cw_bms);
            break;

    case POWER_SUPPLY_PROP_RESET_LEARNED_CC:
            val->intval = 0;
            break;

    case POWER_SUPPLY_PROP_CHG_ITERM:
            val->intval = 0;
            break;
    case POWER_SUPPLY_PROP_PROFILE_ID:
            val->intval = get_version_id(cw_bms);
            break;
    case POWER_SUPPLY_PROP_UPDATE_NOW:
            val->intval = 0;
            break;
    default:
            ret = -EINVAL;
            break;
    }
    return ret;
}


static int cw_battery_set_property(struct power_supply *psy,
				enum power_supply_property psp,
				const union power_supply_propval *val)
{
	int ret = 0;
	int max_voltage = 0;
    struct cw_battery *cw_bms = NULL;

    if (!psy || !val) {
        pr_err("%s: invalid para, fatal error\n", __func__);
        return -EINVAL;
    }

   cw_bms = container_of(psy, struct cw_battery, bms_psy); 

   switch (psp) {
   case POWER_SUPPLY_PROP_VOLTAGE_MAX:
        max_voltage = val->intval;
		ret = change_profile(cw_bms, max_voltage);
        ret |= update_battery_voltage_max(cw_bms, max_voltage);
        break;
   case POWER_SUPPLY_PROP_UPDATE_NOW:
   		/* TODO: need to impl it */
        break;
   case POWER_SUPPLY_PROP_RESET_LEARNED_CC:
        ret = cw_reset(cw_bms);
        break;
   case POWER_SUPPLY_PROP_CHG_ITERM:
        break;
   case POWER_SUPPLY_PROP_CAPACITY:
   case POWER_SUPPLY_PROP_HEALTH:    //Chaman charger ic will give a real value
   case POWER_SUPPLY_PROP_PRESENT:
   case POWER_SUPPLY_PROP_VOLTAGE_OCV:
   case POWER_SUPPLY_PROP_VOLTAGE_NOW:
   case POWER_SUPPLY_PROP_TECHNOLOGY:  //Chaman this value no need
   case POWER_SUPPLY_PROP_BATTERY_TYPE:
   default:
       ret = -EINVAL;
		   break;
   }
   return ret;
}

static enum power_supply_property cw_bms_properties[] = {
    POWER_SUPPLY_PROP_CAPACITY,
    POWER_SUPPLY_PROP_HEALTH,
    POWER_SUPPLY_PROP_PRESENT,
    POWER_SUPPLY_PROP_VOLTAGE_NOW,
    POWER_SUPPLY_PROP_TECHNOLOGY,
    POWER_SUPPLY_PROP_CURRENT_NOW,
    POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN,
    POWER_SUPPLY_PROP_CHARGE_FULL,
    POWER_SUPPLY_PROP_CYCLE_COUNT,
    POWER_SUPPLY_PROP_CHG_CYCLE_COUNT,
    POWER_SUPPLY_PROP_VOLTAGE_OCV,
	POWER_SUPPLY_PROP_UPDATE_NOW,
	POWER_SUPPLY_PROP_PROFILE_STATUS,
    POWER_SUPPLY_PROP_BATTERY_TYPE,
    POWER_SUPPLY_PROP_VOLTAGE_MAX,
    POWER_SUPPLY_PROP_CHG_ITERM,
    POWER_SUPPLY_PROP_RESET_LEARNED_CC,
};

static int cw_bms_property_is_writeable(struct power_supply *psy,
				enum power_supply_property psp)
{
	switch (psp) {
	case POWER_SUPPLY_PROP_UPDATE_NOW:
		return TRUE;
	default:
		break;
	}

	return 0;
}

static char *cw_battery_supplied_to[] = {
	"battery",
};
#endif 

#ifdef BAT_LOW_INTERRUPT
#define WAKE_LOCK_TIMEOUT       (10 * HZ)
static struct wake_lock bat_low_wakelock;

static int cw_get_and_clear_alt(struct cw_battery *cw_bat)
{
        int ret = 0;
        u8 reg_val = 0;
        u8 value = 0;
        int alrt = 0;

    if (!cw_bat) {
        pr_err("%s: invalid param, fatal error\n", __func__);
        return -EINVAL;
    }

        ret = cw_read(cw_bat->client, REG_RRT_ALERT, &reg_val);
        if (ret < 0)
            return ret;
        value = reg_val;
        alrt = value >> RRT_OFFSET;

        value = value & RRT_ALERT_MASK;
        reg_val = value;
        ret = cw_write(cw_bat->client, REG_RRT_ALERT, &reg_val);
        if(ret < 0) {
            return ret;
        }
        return alrt;
}

static irqreturn_t bat_low_detect_irq_handler(int irq, void *dev_id)
{
    struct cw_battery *cw_bat = dev_id;

    if (!cw_bat) {
        pr_err("%s: invalid param, fatal error\n", __func__);
        return IRQ_HANDLED;
    }

    wake_lock_timeout(&bat_low_wakelock, WAKE_LOCK_TIMEOUT);
	cw_get_and_clear_alt(cw_bat);	
	pr_info("batt low interrupt is triggered\n");
    return IRQ_HANDLED;
}
#endif

static int cw2015_parse_dt(struct device_node *np, struct cw_battery *bms)
{
    int ret = 0;

    if (!np || !bms) {
        pr_err("%s: invalid param, fatal error\n", __func__);
        return -EINVAL;
    }

    bms->therm_vadc_node = of_parse_phandle(np, "cw,therm-adc", 0);
    if (!bms->therm_vadc_node) {
        pr_err("Huawei thermal adc read fail\n");
        return -EINVAL;
    }

    ret = of_property_read_u32(np, "cw,therm-switch-mode", &(bms->therm_switch_mode));
    if (ret) {
        pr_err("Huawei read therm switch mode fail\n");
        return -EINVAL;
    }
	pr_debug("%s: battery thermal switch mode %d\n", __func__, bms->therm_switch_mode);

    bms->id_vadc_node = of_parse_phandle(np, "cw,id-adc", 0);
    if (!bms->id_vadc_node) {
        pr_err("Huawei id adc read fail\n");
        return -EINVAL;
    }

    ret = of_property_read_u32(np, "cw,id-switch-mode", &(bms->id_switch_mode));
    if (ret) {
        pr_err("huawei read id switch mode fail\n");
        return -EINVAL;
    }
	pr_debug("%s: battery id switch mode %d\n", __func__, bms->id_switch_mode);

    return 0;
}

static void cw_chg_check_timer_func(unsigned long data)
{
    struct cw_battery *cw_bms = (struct cw_battery *) data;
    queue_delayed_work(cw_bms->cwfg_workqueue, &cw_bms->battery_delay_work, msecs_to_jiffies(0));
    pr_info("cw_chg_check_timer_func executed !\n");
}

static void chg_full_check_timer_func(unsigned long data)
{
    struct cw_battery *cw_bms = (struct cw_battery *) data;
    full_stop_charger_mark = TRUE;
    chg_full_check_mark = FALSE;
    pr_info("chg_full_check_timer_func executed !\n");
}


static int cw2015_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
    int ret = 0;
    int loop = 0;
	struct cw_battery *cw_bms = NULL;
	int batt_uv = 0;
	struct device_node *np = NULL;

    if (!client || !id) {
        pr_err("%s: invalid param, fatal error\n", __func__);
        return -EINVAL;
    }

    cw_bms = devm_kzalloc(&client->dev, sizeof(*cw_bms), GFP_KERNEL);
    if (!cw_bms) {
		pr_err("cw_bms create fail!\n");
        return -ENOMEM;
    }

	np = client->dev.of_node;
    ret = cw2015_parse_dt(np, cw_bms);
    if (ret) {
        pr_err("cw2015 parse dts fail, ret = %d\n", ret);
        devm_kfree(&client->dev, cw_bms);
        return ret;
    }

	if (!huawei_adc_init_done(cw_bms->therm_vadc_node) ||
		!huawei_adc_init_done(cw_bms->id_vadc_node)) {
		pr_debug("%s: vadc not probe yet, defer\n", __func__);
		devm_kfree(&client->dev, cw_bms);
		cw_bms = NULL;
		return -EPROBE_DEFER;
	}


    i2c_set_clientdata(client, cw_bms);
	cw_bms->dev = &client->dev;
    cw_bms->client = client;
    cw_bms->capacity = HALF_CAPACITY;
    cw_bms->voltage = 0;
    cw_bms->status = 0;
	cw_bms->charger_mode = NO_CHARGING;
	cw_bms->change = 0;
	cw_bms->cw_current = 0;
	cw_bms->design_capacity = DESIGN_CAPACITY;
	cw_bms->FCC = DESIGN_CAPACITY;
	cw_bms->cycle_count = 0;
	cw_bms->batt_data = devm_kzalloc(cw_bms->dev,
			sizeof(struct cw_batt_data), GFP_KERNEL);
	cw_bms->version_id = -1;
	if (!cw_bms->batt_data) {
		pr_err("Could not alloc battery data\n");
        devm_kfree(&client->dev, cw_bms);
		return -ENOMEM;
	}

	batt_uv = get_bat_ID(cw_bms);
 	of_batterydata_read_fgauge_data_cw(np,cw_bms->batt_data,batt_uv);
	cw_bms->design_capacity = cw_bms->batt_data->nom_cap_uah/UV_2_MV;
	cw_bms->batt_id_kohm = of_batterydata_batt_kohm_cw(np, batt_uv);
	pr_debug("batt_id_uv %dmV, batt_id_kohm = %d\n", batt_uv, cw_bms->batt_id_kohm);

	profile_ID = 0;
    ret = cw_init(cw_bms,TRUE);
    while ((loop++ < LOOP_MAX) && (ret != 0)) {
		pr_debug("%s: load battery profile error, try again\n", __func__);
		msleep(FG_DEBUG_MS);
        ret = cw_init(cw_bms,TRUE);
    }
    if (ret) {
		pr_err("%s : cw2015 init fail!\n", __func__);
        return ret;	
    }

	ret = cw_init_data(cw_bms);
    if (ret) {
		pr_err("%s : cw2015 init_data fail!\n", __func__);
        return ret;
    }
    pr_info("client->irq %d\n",client->irq);

	#ifdef CW_PROPERTIES
	cw_bms->bms_psy.name = "bms";
	cw_bms->bms_psy.type = POWER_SUPPLY_TYPE_BMS;
	cw_bms->bms_psy.properties = cw_bms_properties;
	cw_bms->bms_psy.num_properties = ARRAY_SIZE(cw_bms_properties);
	cw_bms->bms_psy.get_property = cw_battery_get_property;
	cw_bms->bms_psy.set_property = cw_battery_set_property;
	cw_bms->bms_psy.property_is_writeable = cw_bms_property_is_writeable;
	cw_bms->bms_psy.supplied_to = cw_battery_supplied_to;
	cw_bms->bms_psy.num_supplicants = ARRAY_SIZE(cw_battery_supplied_to);
	ret = power_supply_register(&client->dev, &cw_bms->bms_psy);
	if(ret < 0) {
	    power_supply_unregister(&cw_bms->bms_psy);
	    return ret;
	}
	#endif

    setup_timer(&cw_bms->chg_check_timer, cw_chg_check_timer_func, (unsigned long) cw_bms);
    setup_timer(&cw_bms->chg_full_check_timer, chg_full_check_timer_func, (unsigned long) cw_bms);
	cw_bms->cwfg_workqueue = create_singlethread_workqueue("cwfg_gauge");
	INIT_DELAYED_WORK(&cw_bms->battery_delay_work, cw_bat_work);
	queue_delayed_work(cw_bms->cwfg_workqueue, &cw_bms->battery_delay_work , msecs_to_jiffies(DELAYED_TIME_50MS));
    wake_lock_init(&bat_low_wakelock, WAKE_LOCK_SUSPEND, "bat_low_detect");

	if (client->irq) {
		ret = devm_request_threaded_irq(&client->dev, client->irq, NULL,
			bat_low_detect_irq_handler, 
			IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
			"bat_low_detect", cw_bms);
		if (ret < 0) {
			pr_err("request irq for irq=%d failed, ret = %d\n", client->irq, ret);
			//goto err_1;
		}
		enable_irq_wake(client->irq);
	}

	pr_info("cw2015 driver probe success!\n");
    return 0;
}

#ifdef CONFIG_PM
static int cw_bat_suspend(struct device *dev)
{
        struct i2c_client *client = NULL;
        struct cw_battery *cw_bms = NULL;

        if (!dev) {
            pr_err("%s: invalid param, fatal error\n", __func__);
            return 0;
        }

        pr_err("%s: entry\n", __func__);
        client = to_i2c_client(dev);
        cw_bms = i2c_get_clientdata(client);
		read_persistent_clock(&suspend_time_before);
        cancel_delayed_work(&cw_bms->battery_delay_work);
        pr_err("%s: end\n", __func__);
        return 0;
}

static int cw_bat_resume(struct device *dev)
{
        struct i2c_client *client = NULL;
        struct cw_battery *cw_bms = NULL;

        if (!dev) {
            pr_err("%s: invalid param, fatal error\n", __func__);
            return 0;
        }

        pr_err("%s: entry\n", __func__);
        client = to_i2c_client(dev);
        cw_bms = i2c_get_clientdata(client);
		suspend_resume_mark = TRUE;
		read_persistent_clock(&after);
		after = timespec_sub(after, suspend_time_before);
        queue_delayed_work(cw_bms->cwfg_workqueue, &cw_bms->battery_delay_work, msecs_to_jiffies(DELAYED_TIME_2MS));
        pr_err("%s: end\n", __func__);
        return 0;
}

static const struct dev_pm_ops cw_bat_pm_ops = {
        .suspend  = cw_bat_suspend,
        .resume   = cw_bat_resume,
};
#endif

static int cw2015_remove(struct i2c_client *client)	 
{
	pr_info("\n");
	return 0;
}

static const struct i2c_device_id cw2015_id_table[] = {
	{CWFG_NAME, 0},
	{}
};

static struct i2c_driver cw2015_driver = {
	.driver 	  = {
		.name = CWFG_NAME,
#ifdef CONFIG_PM
        .pm     = &cw_bat_pm_ops,
#endif
		.owner	= THIS_MODULE,
	},
	.probe		  = cw2015_probe,
	.remove 	  = cw2015_remove,
	.id_table = cw2015_id_table,
};


static int __init cw2015_init(void)
{
	pr_info("\n");
    i2c_add_driver(&cw2015_driver);
    return 0; 
}


static void __exit cw2015_exit(void)
{
    i2c_del_driver(&cw2015_driver);
}

module_init(cw2015_init);
module_exit(cw2015_exit);

MODULE_AUTHOR("Chaman Qi");
MODULE_DESCRIPTION("CW2015 FGADC Device Driver V3.0");
MODULE_LICENSE("GPL");
