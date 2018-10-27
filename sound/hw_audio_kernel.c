/* Copyright (c) 2017-2018, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
/*************************************************
File name: hw_audio_kernel.c
Author: Version: Date: lichunjie  hw_audio_kernel_1.0  2017.06.05 
Description:  huawei audio kernel;
Others:
Function List: 
        hac_gpio_switch
        hac_switch_get
        hac_switch_put
        hw_msm_init_extra
        hw_hac_init
History: 
1. Date:
Author:
Modification:
2. ...
*************************************************/

#include <linux/kernel.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <sound/soc.h>
#include <sound/hw_audio_kernel.h>

/* hac feature definition begin */
#define DEFAULT_HAC_NONEED       (-1)
#define DEFUALT_HAC_SWITCH_VALUE (0x0)
#define HAC_ENABLE               (1)/* HAC ON */
#define HAC_DISABLE              (0)/* HAC OFF */
#define GPIO_PULL_UP             (1)
#define GPIO_PULL_DOWN           (0)

static int g_hac_en_gpio = DEFAULT_HAC_NONEED;
static int g_hac_switch = DEFUALT_HAC_SWITCH_VALUE;
static const char * const g_hac_switch_text[] = {"OFF", "ON"};
/* hac feature definition end */

/* simple pa aw87318 definition begin */
enum {
    AW87318_PA_MODE_0  = 0,
    AW87318_PA_MODE_1,
    AW87318_PA_MODE_2,
    AW87318_PA_MODE_3,
    AW87318_PA_MODE_4,
    AW87318_PA_MODE_5,
    AW87318_PA_MODE_6,
    AW87318_PA_MODE_7,
    AW87318_PA_MODE_8,
    AW87318_PA_MODE_9,
    AW87318_PA_MODE_MAX
};
static int g_aw87318_pa_mode = AW87318_PA_MODE_1;
static const char * const g_aw87318_pa_mode_text[] = {"ZERO","ONE",
                    "TWO", "THREE","FOUR","FIVE","SIX", "SEVEN","EIGHT","NINE"};
/* simple pa aw87318 end */

/*************************************************
Function:       kcontrol_value_get
Description:    get kcontrol value
callby:         local
Input:          value
Output:         struct snd_ctl_elem_value *ucontrol
Return:         0
Others:
*************************************************/
static int kcontrol_value_get(struct snd_kcontrol *kcontrol,
                    struct snd_ctl_elem_value *ucontrol,
                    int value)
{
    if (NULL == kcontrol || NULL == ucontrol) {
        pr_err("input pointer is null\n");
        return 0;
    }

    ucontrol->value.integer.value[0] = value;

    return 0;
}

/*************************************************
Function:       kcontrol_value_put
Description:    set kcontrol value
callby:         local
Input:          struct snd_ctl_elem_value *ucontrol
Output:         value
Return:         ret
Others:
*************************************************/
static int kcontrol_value_put(struct snd_kcontrol *kcontrol,
                    struct snd_ctl_elem_value *ucontrol,
                    int *value)
{
    int ret = 0;

    if (NULL == kcontrol || NULL == ucontrol) {
        pr_err("input pointer is null\n");
        return ret;
    }

    *value = ucontrol->value.integer.value[0];

    return ret;
}

/*************************************************
Function:       hac_gpio_switch
Description:    switch gpio according to ucontrol 
callby:         local
Input:          int hac_cmd
Output:         null
Return:         hac_cmd
Others:
*************************************************/
static int hac_gpio_switch(int hac_cmd)
{
    if ( g_hac_en_gpio == DEFAULT_HAC_NONEED ) {
        pr_err("Failed to get the hac gpio\n");
        return 0;
    }

    if (HAC_ENABLE == hac_cmd) {
        pr_info("Enable hac enable gpio %u\n", g_hac_en_gpio);
        gpio_direction_output(g_hac_en_gpio, GPIO_PULL_UP);
    } else {
        pr_info("Disable hac enable gpio %u\n", g_hac_en_gpio);
        gpio_direction_output(g_hac_en_gpio, GPIO_PULL_DOWN);
    }

    return hac_cmd;
}

/*************************************************
Function:       hac_switch_get
Description:    get g_hac_switch value
callby:         local
Input:          null
Output:         struct snd_ctl_elem_value *ucontrol
Return:         0
Others:
*************************************************/
static int hac_switch_get(struct snd_kcontrol *kcontrol,
                    struct snd_ctl_elem_value *ucontrol)
{
    return kcontrol_value_get(kcontrol, ucontrol, g_hac_switch);
}

/*************************************************
Function:       hac_switch_put
Description:    set g_hac_switch value
callby:         local
Input:          struct snd_ctl_elem_value *ucontrol
Output:         null
Return:         ret
Others:
*************************************************/
static int hac_switch_put(struct snd_kcontrol *kcontrol,
                    struct snd_ctl_elem_value *ucontrol)
{
    int ret = 0;

    (void)kcontrol_value_put(kcontrol, ucontrol, &g_hac_switch);
    ret = hac_gpio_switch(g_hac_switch);

    return ret;
}

static int aw87318_pa_mode_get(struct snd_kcontrol *kcontrol,
                    struct snd_ctl_elem_value *ucontrol)
{
    return kcontrol_value_get(kcontrol, ucontrol, g_aw87318_pa_mode);
}

static int aw87318_pa_mode_put(struct snd_kcontrol *kcontrol,
                    struct snd_ctl_elem_value *ucontrol)
{
    (void)kcontrol_value_put(kcontrol, ucontrol, &g_aw87318_pa_mode);

    if (g_aw87318_pa_mode >= AW87318_PA_MODE_MAX || g_aw87318_pa_mode <= AW87318_PA_MODE_0 ) {
        pr_err("%s: put aw87318 invalid value: %d\n",__func__,  g_aw87318_pa_mode);
    }

    return 0;
}

static const struct soc_enum huawei_msm_snd_enum[] = {
    SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(g_hac_switch_text),
                        g_hac_switch_text),
    SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(g_aw87318_pa_mode_text),
                        g_aw87318_pa_mode_text),
};

static const struct snd_kcontrol_new huawei_msm_snd_controls[] = {
    SOC_ENUM_EXT("HAC", huawei_msm_snd_enum[0],
                hac_switch_get, hac_switch_put),
    SOC_ENUM_EXT("AW87318 PA Mode", huawei_msm_snd_enum[1],
                aw87318_pa_mode_get, aw87318_pa_mode_put),
};

/*************************************************
Function:       hw_hac_init
Description:    init hac feature,mainly configure GPIO
callby:         internal
Input:          struct device_node* np
Output:         null
Return:         success:true
                fail:false
Others:
*************************************************/
static bool hw_hac_init (struct device_node* np)
{
    int ret = 0;

    pr_info("%s: enter\n", __func__);

    if(NULL == np) {
        pr_err("%s: np is NULL\n", __func__);
        goto err;
    }

    ret = of_get_named_gpio_flags(np, "huawei,hac_gpio", 0, NULL);
    if (ret < 0){
        g_hac_en_gpio = DEFAULT_HAC_NONEED;
        goto err;
    } else {
        g_hac_en_gpio = ret;
    }

    ret = gpio_request(g_hac_en_gpio, "g_hac_en_gpio");
    if (ret) {
        pr_err("%s: Failed to configure hac enable "
            "gpio %u\n", __func__, g_hac_en_gpio);
        goto err;
    }

    if (gpio_direction_output(g_hac_en_gpio, GPIO_PULL_DOWN)) {
        pr_err("%s: g_hac_en_gpio set output failed!\n",__func__);
        gpio_free(g_hac_en_gpio);
        goto err;
    }

    return true;
err:
    return false;
}


/*************************************************
Function:       hw_msm_init_extra
Description:    add an array of controls to a codec
callby:         msm_audrx_init
Input:          struct snd_soc_codec *codec
Output:         null
Return:         null
Others:
*************************************************/
void hw_msm_init_extra(struct snd_soc_codec *codec) 
{
    if(NULL == codec) {
        pr_err("%s: codec is NULL\n", __func__);
        return;
    }

    snd_soc_add_codec_controls(codec, huawei_msm_snd_controls,
            ARRAY_SIZE(huawei_msm_snd_controls));

    pr_info("%s: exit\n", __func__);
    return;
}

/*************************************************
Function:       hw_machine_init_extra
Description:    hw asoc machine extra info init
callby:         asoc machine probe
Input:          struct platform_device *pdev
Output:         null
Return:         success:true
                fail:false
Others:
*************************************************/
bool hw_machine_init_extra (struct platform_device *pdev, void** priv)
{
    int ret = 0;

    pr_info("%s: enter\n", __func__);

    if(pdev == NULL || priv == NULL) {
        pr_err("%s: input param is NULL\n", __func__);
        goto err;
    }

    ret = hw_hac_init(pdev->dev.of_node);
    if (!ret) {
        dev_err(&pdev->dev, "hac init failed, just for North American.\n");
    }

    return true;
err:
    return false;
}

/*************************************************
Function:       hw_string_is_property_read
Description:    read property from the compatible,
                compare if the readvalue is equal to string
callby:         external
Input:          char *propname, char* string
Output:         null
Return:         true or false
Others:
*************************************************/
bool hw_string_is_property_read (char* compatible, char *propname, char* string)
{
    struct device_node *of_audio_node;
    const char *ptype = NULL;

    if (compatible == NULL || propname == NULL || string == NULL) {
        pr_err("%s: propname or string is NULL\n", __func__);
        return false;
    }

    of_audio_node = of_find_compatible_node(NULL, NULL, compatible);
    if (!of_audio_node) {
        pr_err("%s: %s node not find!\n", __func__, compatible);
        return false;
    }

    if (of_property_read_string(of_audio_node, propname, &ptype)) {
        pr_err("%s: Can not find propname: %s\n" , __func__, propname);
        return false;
    }

    pr_info("%s: propname: %s, string:%s\n" , __func__, propname, string);
    return (0 == strncmp(string, ptype, strlen(string))) ? true : false;
}

/*************************************************
Function:       hw_enable_spk_ext_pa
Description:    enable aw87318 pa according to mode
callby:         enable_spk_ext_pa
Input:          int goip
Output:         null
Return:         null
Others:
*************************************************/
void hw_enable_spk_ext_pa(int gpio)
{
    int i;

    if (!gpio_is_valid(gpio)) {
        pr_err("%s: Invalid gpio: %d\n", __func__,gpio);
        return;
    }

    if (g_aw87318_pa_mode >= AW87318_PA_MODE_MAX || g_aw87318_pa_mode <= AW87318_PA_MODE_0 ) {
        pr_err("%s: aw87318 mode is invalid: %d, use default value.\n",__func__,  g_aw87318_pa_mode);
        gpio_set_value_cansleep(gpio, 1); //default value is mode 1
        return;
    }

    gpio_set_value_cansleep(gpio, 1);
    for (i = AW87318_PA_MODE_1; i < g_aw87318_pa_mode; i++ ) {
        udelay(2);
        gpio_set_value_cansleep(gpio, 0);
        udelay(2);
        gpio_set_value_cansleep(gpio, 1);
    }

    return;
}

