#ifndef __LP8556_H__
#define __LP8556_H__

#define BL_PASSAGE_NUM_ONE              1
#define BL_PASSAGE_NUM_TWO              2
#define BL_PASSAGE_NUM_THREE            3
#define BL_PASSAGE_NUM_FOUR             4
#define BL_PASSAGE_NUM_FIVE             5
#define BL_PASSAGE_NUM_SIX              6

/*Enable LED*/
#define LED_ENABLE_THREE_PASSAGE_VALUE_ONE              0x01
#define LED_ENABLE_THREE_PASSAGE_VALUE_TWO              0x03
#define LED_ENABLE_THREE_PASSAGE_VALUE_THREE            0x07
#define LED_ENABLE_THREE_PASSAGE_VALUE_FOUR             0x0F
#define LED_ENABLE_THREE_PASSAGE_VALUE_FIVE             0x1F
#define LED_ENABLE_THREE_PASSAGE_VALUE_SIX              0x3F

/*Select PWM output phase configuration*/
#define CURRENT_LSB_CFG5_PHASE_VALUE_ONE              0x7B
#define CURRENT_LSB_CFG5_PHASE_VALUE_TWO              0x4B
#define CURRENT_LSB_CFG5_PHASE_VALUE_THREE            0x3B
#define CURRENT_LSB_CFG5_PHASE_VALUE_FOUR             0x2B
#define CURRENT_LSB_CFG5_PHASE_VALUE_FIVE             0x1B
#define CURRENT_LSB_CFG5_PHASE_VALUE_SIX              0x0B

#define BL_MAX_VBOOST_NUM_DEFAULT                           0
#define BL_MAX_VBOOST_NUM_ONE                               1

#define CURRENT_LSB_CFG9_MAX_VBOOST_VALUE_DEFAULT              0x80
#define CURRENT_LSB_CFG9_MAX_VBOOST_VALUE_THREE                0xA0    /*Modify the MAX VBOOST to (25V,34.5V)*/

/*Only read EPROMs once on initial power-up
 * PWM input and brightness register
 * Backlight disabled and chip turned off*/
#define DEVICE_CONTROL_VALUE_1 0X86

/*Set the lk brightness for 100*/
#define BRIGHTNESS_CONTROL_VALUE 0X64

/*Set boost switching frequency 1250kHZ*/
#define BOOST_FREQ_VALUE 0X40

/*The CURRENT_LSB_CFG0_VALUE and the CURRENT_LSB_CFG1_VALUE
 * to scale the current for 50mA*/
#define CURRENT_LSB_CFG0_VALUE 0XFF
#define CURRENT_LSB_CFG1_VALUE 0X3F

/*Only read EPROMs once on initial power-up
 * PWM input and Brightness register
 * Backing enabled and chip turned on*/
#define DEVICE_CONTROL_VALUE_2 0X87

#endif
