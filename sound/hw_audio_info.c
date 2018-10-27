/* Copyright (c) 2014-2018, The Linux Foundation. All rights reserved.
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


#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/sysfs.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/kobject.h>
#include <linux/string.h>
#include <linux/of.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <sound/soc.h>
#include <linux/regulator/consumer.h>
#include <sound/hw_audio_info.h>
#ifdef CONFIG_HUAWEI_DSM_AUDIO
#include <dsm_audio/dsm_audio.h>
#endif

/* Audio property is an unsigned 32-bit integer stored in the variable of
audio_property.The meaning of audio_property is defined as following MACROs
 in groups of 4 bits */

/* Bit4 ~ bit7:
   Actually existing mics on the phone, it's NOT relevant to fluence. Using
   one bit to denote the existence of one kind of mic, possible mics are:
     master mic: the basic mic used for call and record, if it doesn't exist
                 means the config or software is wrong.
     secondary mic: auxiliary mic, usually used for fluence or paired with
                    speaker in handsfree mode, it's possible that this mic
                    exist but fluence isn't enabled.
     error mic: used in handset ANC. */
#define AUDIO_PROP_MASTER_MIC_EXIST_NODE    "builtin-master-mic-exist"
#define AUDIO_PROP_SECONDARY_MIC_EXIST_NODE "builtin-second-mic-exist"
#define AUDIO_PROP_ERROR_MIC_EXIST_NODE     "builtin-error-mic-exist"
#define AUDIO_PROP_MASTER_MIC_EXIST_MASK    (0x00000010)
#define AUDIO_PROP_SECONDARY_MIC_EXIST_MASK (0x00000020)
#define AUDIO_PROP_ERROR_MIC_EXIST_MASK     (0x00000040)

/* Bit12 ~ bit15:
   Denote which mic would be used in hand held mode, please add as needed */
#define AUDIO_PROP_HANDHELD_MASTER_MIC_NODE "hand_held_master_mic_strategy"
#define AUDIO_PROP_HANDHELD_DUAL_MIC_NODE   "hand_held_dual_mic_strategy"
#define AUDIO_PROP_HANDHELD_AANC_MIC_NODE   "hand_held_aanc_mic_strategy"
#define AUDIO_PROP_HANDHELD_MASTER_MIC      (0x00001000)
#define AUDIO_PROP_HANDHELD_DUAL_MIC        (0x00002000)
#define AUDIO_PROP_HANDHELD_AANC_MIC        (0x00004000)

/* Bit16 ~ bit19:
   Denote which mic would be used in loud speaker mode, please add as needed */
#define AUDIO_LOUDSPEAKER_MASTER_MIC_NODE  "loud_speaker_master_mic_strategy"
#define AUDIO_LOUDSPEAKER_SECOND_MIC_NODE  "loud_speaker_second_mic_strategy"
#define AUDIO_LOUDSPEAKER_ERROR_MIC_NODE   "loud_speaker_error_mic_strategy"
#define AUDIO_LOUDSPEAKER_DUAL_MIC_NODE    "loud_speaker_dual_mic_strategy"
#define AUDIO_LOUDSPEAKER_MASTER_MIC       (0x00010000)
#define AUDIO_LOUDSPEAKER_SECOND_MIC       (0x00020000)
#define AUDIO_LOUDSPEAKER_ERROR_MIC        (0x00040000)
#define AUDIO_LOUDSPEAKER_DUAL_MIC         (0x00080000)

/*add nrec function source */
#define AUDIO_PROP_BTSCO_NREC_ADAPT_MASK    (0xf0000000)
#define AUDIO_PROP_BTSCO_NREC_ADAPT_OFF     (0x10000000)
#define AUDIO_PROP_BTSCO_NREC_ADAPT_ON      (0x20000000)

/*product identifier*/
#define PRODUCT_IDENTIFIER_NODE             "product-identifier"
#define PRODUCT_IDENTIFIER_BUFF_SIZE        (64)

/*get audio_param version from dtsi config, this maybe not used, reserve.*/
#define AUD_PARAM_VER_NODE                  "aud_param_ver"

#define SPEAKER_PA_NODE                     "speaker-pa"
#define SMARTPA_NUM                         "smartpa-num"
#define SMARTPA_ALGO_PARAMSIZE              "smartpa-algo-paramsize"
#define VOICE_LVM_MODE                      "voice-lvm-mode"
#define VOICE_VOL_LEVEL                      "voice-vol-level"
#define SMARTPA_I2S                         "smartpa-i2s"

#define AUDIO_INFO_BUFF_SIZE                 (32)
#define AUDIO_BOX_NAME_SIZE                 (128)

#define AUDIO_CE_PARAM_INSIDE               "audio-ce-param-inside"
#define SPEAKER_TEST_CONTENT                "speaker_test_content"
#define MIC1_DIFF_MODE_ENABLE               "mic1-differential-mode-enabled"

#define AUDIO_EXTPA_TO_HPHR               "audio-extpa-to-hphr"

#define MAX_SPK_BOX_COUNT                   (6)
#define PIN_VOLTAGE_NONE                    (-1)
#define PIN_VOLTAGE_LOW                     (0)
#define PIN_VOLTAGE_HIGH                    (1)
#define PIN_VOLTAGE_FLOAT                   (2)


#define PRODUCT_NERC_ADAPT_CONFIG           "product-btsco-nrec-adapt"
#define RPODUCT_STERO_SMARTPA_CONFIG        "dual_smartpa_support"

#define SMARTPA_ALGO_PARAMSIZE_DEFAULT     (4096)

#ifdef CONFIG_SND_SOC_VRM
#define VRM_ARRAY_LEN        (116)
#define PAYLOAD_START        (100)
#define VRM_DIR_WRITE        (1)
#define VRM_DIR_READ         (0)
#define VRM_SIZE_RW          (16 * sizeof(int))
#define VRM_APRV2_RSV_CODE   (200)

extern int afe_vrm_param_ctrl(uint32_t dir, uint32_t size, uint8_t *payload);

static int param_array[VRM_ARRAY_LEN];
static uint8_t *g_payload = (uint8_t *)&param_array[PAYLOAD_START];
#endif // CONFIG_SND_SOC_VRM

/* codec dump feature related definition begin */
/* the string length of reg dump line
     4 for codec register name
     2 for ': '
     2 for codec register value
     1 for '\n'
*/
#define WCD_CDC_DUMP_LENGTH (9)
/* the size of reg dump line */
#define WCD_CDC_DUMP_BUF_SIZE (WCD_CDC_DUMP_LENGTH + 1)
/* word size of codec reg name length */
#define WCD_CDC_DUMP_WORD_SIZE (4)
/* register size of codec */
#define WCD_CDC_DUMP_REG_SIZE (2)
/* codec dump feature related definition end */

/**
* audio_property         Product specified audio properties
* product_identifier     means to use which acdb files
* speaker_pa             means which smartpa used
* aud_param_ver          means acdb files version, this maybe not used later
* audio_stero_smartpa    means use one or two smartpa
* speaker_test_content   means speaker box connected
* smartpa_num            means speaker smartpa number
* smartpa_algo_paramsize means smartpa algorithm parameters size
* smartpa_i2s            means which i2s smartpa used 
* audio_extpa_to_hphr        means extpa connect to hphr.
*                                        for the product which extpa connect to hphr, this node need set "1"
*/
static unsigned int audio_property;
static char product_identifier[PRODUCT_IDENTIFIER_BUFF_SIZE] = "default";
static char speaker_pa[AUDIO_INFO_BUFF_SIZE] = "none";
static char voice_lvm_mode[AUDIO_INFO_BUFF_SIZE] = "0";
static unsigned int  voice_vol_level = 0;
static char aud_param_ver[AUDIO_INFO_BUFF_SIZE] = "default";
static unsigned int audio_stero_smartpa;
static unsigned int audio_ce_param_inside = 0;
static unsigned int smartpa_num;
static unsigned int smartpa_i2s = 0;
static unsigned int smartpa_algo_paramsize;
static char speaker_test_content[AUDIO_BOX_NAME_SIZE] = "speaker";
static bool mic1_differential_mode_enabled = false;
static unsigned int audio_extpa_to_hphr = 0;


/*************************************************
Function:       audio_property_show
Description:    get audio property 
Input:          driver: target device driver
Output:         buf:buffer for audio_property data
Return:         size_t of "audio_property"
Others: 
*************************************************/
static ssize_t audio_property_show(struct device_driver *driver, char *buf)
{
    if (NULL == buf) {
        pr_err("%s: buf is null", __func__);
        return 0;
    }

    return scnprintf(buf, PAGE_SIZE, "%08X\n", audio_property);
}
DRIVER_ATTR(audio_property, 0444, audio_property_show, NULL);

/*************************************************
Function:       product_identifier_show
Description:    get product name, default value is "default"
Input:          driver: target device driver
Output:         buf:buffer for product_identifier data
Return:         size_t of "product_identifier"
Others: 
*************************************************/
static ssize_t product_identifier_show(struct device_driver *driver, char *buf)
{
    if (NULL == buf) {
        pr_err("%s: buf is null", __func__);
        return 0;
    }

    return scnprintf(buf, PAGE_SIZE, "%s", product_identifier);
}
DRIVER_ATTR(product_identifier, 0444, product_identifier_show, NULL);

/*************************************************
Function:       product_identifier_show
Description:    get product name, default value is "default"
Input:          null
Output:         buf:buffer for product_identifier data , int buflen
Return:         size_t of "product_identifier"
Others: 
*************************************************/
ssize_t get_product_identifier(char *buf, int buflen)
{
    if (NULL == buf) {
        pr_err("%s: buf is null", __func__);
        return 0;
    }

    return scnprintf(buf, buflen, "%s", product_identifier);
}
EXPORT_SYMBOL(get_product_identifier);

/*************************************************
Function:       audiopara_version_show
Description:    get sound parameter version
Input:          driver: target device driver
Output:         buf:buffer for aud_param_ver data
Return:         size_t of "aud_param_ver"
Others:         this maybe not used,reserved.
*************************************************/
static ssize_t audiopara_version_show(struct device_driver *driver, char *buf)
{
    if (NULL == buf) {
        pr_err("%s: buf is null", __func__);
        return 0;
    }

    return scnprintf(buf, PAGE_SIZE, "%s\n", aud_param_ver);
}
DRIVER_ATTR(aud_param_ver, 0444, audiopara_version_show, NULL);

/*************************************************
Function:       voice_lvm_mode_show
Description:    voice lvm mode, 0:not support lvm ,1: support lvm
Input:          driver: target device driver
Output:         buf:buffer for voice_lvm_mode data
Return:         size_t of "voice_lvm_mode"
Others: 
*************************************************/
static ssize_t voice_lvm_mode_show(struct device_driver *driver, char *buf)
{
    if (NULL == buf) {
        pr_err("%s: buf is null", __func__);
        return 0;
    }

    return scnprintf(buf, PAGE_SIZE, "%s", voice_lvm_mode);
}
DRIVER_ATTR(voice_lvm_mode, 0444, voice_lvm_mode_show, NULL);

/*************************************************
Function:       voice_vol_level_show
Description:    voice_vol_level
Input:          driver: target device driver
Output:         buf:buffer for voice_vol_level data
Return:         size_t of "voice_vol_level"
Others: 
*************************************************/
static ssize_t voice_vol_level_show(struct device_driver *driver, char *buf)
{
    if (NULL == buf) {
        pr_err("%s: buf is null", __func__);
        return 0;
    }

    return scnprintf(buf, PAGE_SIZE, "%u", voice_vol_level);
}
DRIVER_ATTR(voice_vol_level, 0444, voice_vol_level_show, NULL);

/*************************************************
Function:       speaker_pa_show
Description:    get speaker pa name,default value is none
Input:          driver: target device driver
Output:         buf:buffer for speaker_pa data
Return:         size_t of "speaker_pa"
Others: 
*************************************************/
static ssize_t speaker_pa_show(struct device_driver *driver, char *buf)
{
    if (NULL == buf) {
        pr_err("%s: buf is null", __func__);
        return 0;
    }

    return scnprintf(buf, PAGE_SIZE, "%s", speaker_pa);
}
DRIVER_ATTR(speaker_pa, 0444, speaker_pa_show, NULL);

/*************************************************
Function:       hw_set_smartpa_type
Description:    set smartpa chip type according device probe.
Input:          smartpa_type
Output:         null
Return:         void
Others:
*************************************************/
void hw_set_smartpa_type(const char* smartpa_type)
{
    if (smartpa_type == NULL) {
        pr_err("hw_audio: get smartpa type failed, ptr is NULL.\n");
    }

    memset(speaker_pa, 0, sizeof(speaker_pa));
    memcpy(speaker_pa, smartpa_type, min(strlen(smartpa_type), sizeof(speaker_pa)-1));
    return;
}
EXPORT_SYMBOL(hw_set_smartpa_type);

/*************************************************
Function:       hw_get_smartpa_type
Description:    get smartpa when need this info
Input:          char* buf, int buflen
Output:         null
Return:         void
Others:
*************************************************/
ssize_t hw_get_smartpa_type(char *buf, int buflen)
{
    if (NULL == buf) {
        pr_err("%s: buf is null", __func__);
        return 0;
    }

    return scnprintf(buf, buflen, "%s", speaker_pa);
}
EXPORT_SYMBOL(hw_get_smartpa_type);

/*************************************************
Function:       smartpa_num_show
Description:    get smartpa num, default value:0
Input:          driver: target device driver
Output:         buf:buffer for smartpa_num data
Return:         size_t of "smartpa_num"
Others: 
*************************************************/
static ssize_t smartpa_num_show(struct device_driver *driver, char *buf)
{
    if (NULL == buf) {
        pr_err("%s: buf is null", __func__);
        return 0;
    }

    return scnprintf(buf, PAGE_SIZE, "%u", smartpa_num);
}
DRIVER_ATTR(smartpa_num, 0444, smartpa_num_show, NULL);

/*************************************************
Function:       box_id_show
Description:    get box name, such as "GOER". default value is none
Input:          driver: target device driver
Output:         buf:buffer for audio_stero_smartpa_show data
Return:         size_t of "audio_stero_smartpa_show"
Others: 
*************************************************/
static ssize_t audio_stero_smartpa_show(struct device_driver *driver, char *buf)
{
    if (NULL == buf) {
        pr_err("%s: buf is null", __func__);
        return 0;
    }

    return scnprintf(buf, PAGE_SIZE, "%d", audio_stero_smartpa);
}
DRIVER_ATTR(stero_smartpa, 0444, audio_stero_smartpa_show, NULL);

/*************************************************
Function:       smartpa_i2s_show
Description:    get which i2s bus does smartpa use.
Input:          driver: target device driver
Output:         buf:buffer for smartpa_i2s_show data
Return:         size_t of "smartpa_i2s_show"
Others: 
*************************************************/
static ssize_t smartpa_i2s_show(struct device_driver *driver, char *buf)
{
    if (NULL == buf) {
        pr_err("%s: buf is null", __func__);
        return 0;
    }

    return scnprintf(buf, PAGE_SIZE, "%u", smartpa_i2s);
}
DRIVER_ATTR(smartpa_i2s, 0444, smartpa_i2s_show, NULL);

/*************************************************
Function:       audio_ce_param_inside_show
Description:    distinguish audio ce and normal parameter
Input:          driver: target device driver
Output:         buf:buffer for audio_ce_param_inside data
Return:         size_t of "audio_ce_param_inside"
Others:
*************************************************/
static ssize_t audio_ce_param_inside_show(struct device_driver *driver, char *buf)
{
    if (NULL == buf) {
        pr_err("%s: buf is null", __func__);
        return 0;
    }
    return scnprintf(buf, PAGE_SIZE, "%u", audio_ce_param_inside);
}
DRIVER_ATTR(audio_ce_param_inside, 0444, audio_ce_param_inside_show, NULL);

/*************************************************
Function:       audio_speaker_test_content_show
Description:    disginguish different speakers, just used for multi-speaker product
Input:          driver: target device driver
Output:         buf:buffer for speaker_test_content data
Return:         size_t of "speaker_test_content"
Others:
*************************************************/
static ssize_t audio_speaker_test_content_show(struct device_driver *driver, char *buf)
{
    if (NULL == buf) {
        pr_err("%s: buf is null", __func__);
        return 0;
    }
    return scnprintf(buf, PAGE_SIZE, "%s", speaker_test_content);
}
DRIVER_ATTR(speaker_test_content, 0444, audio_speaker_test_content_show, NULL);

/*************************************************
Function:       audio_smartpa_algo_paramsize_show
Description:    redefine algo paramsize,for some smartpa product
Input:          driver: target device driver
Output:         buf:buffer for smartpa_algo_paramsize data
Return:         size_t of "smartpa_algo_paramsize"
Others:
*************************************************/
static ssize_t audio_smartpa_algo_paramsize_show(struct device_driver *driver, char *buf)
{
    if (NULL == buf) {
        pr_err("%s: buf is null", __func__);
        return 0;
    }
    return scnprintf(buf, PAGE_SIZE, "%u", smartpa_algo_paramsize);
}
DRIVER_ATTR(smartpa_algo_paramsize, 0444, audio_smartpa_algo_paramsize_show, NULL);

static struct snd_soc_codec *registered_codec;

/*************************************************
Function:       hw_get_registered_codec
Description:    get codec struct when codec_probe, used for codec_dump feature.
Input:          codec
Output:         null
Return:         void
Others:
*************************************************/
void hw_get_registered_codec(struct snd_soc_codec *codec)
{
    registered_codec = codec;
}
EXPORT_SYMBOL_GPL(hw_get_registered_codec);

/*************************************************
Function:       audio_codec_dump_show
Description:    dump codec regs
Input:          driver: target device driver
Output:         buf:buffer for dump codec reg
Return:         size_t of "dump codec reg "
Others:
*************************************************/
static ssize_t audio_codec_dump_show(__attribute__((unused))struct device_driver *driver, char *buf)
{
    int i = 0;
    int len = 0;
    int ret = 0;
    size_t total = 0;
    /* the max size of the codec dump */
    const size_t count = PAGE_SIZE -1;
    /* the size of the codec buffer */
    char tmpbuf[WCD_CDC_DUMP_BUF_SIZE];
    /* the string size of regsiter */
    char regbuf[WCD_CDC_DUMP_REG_SIZE + 1];

    struct snd_soc_codec *codec = registered_codec;

    len = WCD_CDC_DUMP_LENGTH;

    if(NULL == buf) {
        pr_err("%s: buf is NULL.\n", __func__);
        return 0;
    }

    if(NULL == codec) {
        pr_err("%s: codec is NULL.\n", __func__);
        return 0;
    }

    for (i = 0; i < codec->driver->reg_cache_size; i ++) {
        if (!codec->readable_register(codec, i))
            continue;

        if (total + len >= count)
            break;

        ret = codec->driver->read(codec, i);
        if (ret < 0) {
            memset(regbuf, 'X', WCD_CDC_DUMP_REG_SIZE);
            regbuf[WCD_CDC_DUMP_REG_SIZE] = '\0';
        } else {
            snprintf(regbuf, WCD_CDC_DUMP_REG_SIZE + 1, "%.*x", WCD_CDC_DUMP_REG_SIZE, ret);
        }

        /* prepare the buffer */
        snprintf(tmpbuf, len + 1, "%.*x: %s\n", WCD_CDC_DUMP_WORD_SIZE, i, regbuf);
        /* copy it back to the caller without the '\0' */
        memcpy(buf + total, tmpbuf, len);

        total += len;
    }

    total = min(total, count);

    return total;
}
DRIVER_ATTR(codec_dump, 0444, audio_codec_dump_show, NULL);

#ifdef CONFIG_SND_SOC_VRM
/*************************************************
Function:       vrm_data_show
Description:    get vrm data
Input:          driver: target device driver
Output:         buf:buffer for payload data
Return:         size_t of payload data
Others:
*************************************************/
static ssize_t vrm_data_show(struct device_driver *driver, char *buf)
{
    uint32_t *result_payload = NULL;

    if (NULL == buf) {
        pr_err("%s: buf is null", __func__);
        return 0;
    }
    (void)afe_vrm_param_ctrl(VRM_DIR_READ, VRM_SIZE_RW, g_payload);

    result_payload = (uint32_t*) g_payload;

    return sprintf(buf, "MIC0:%d, MIC1:%d, VPTX:%d, VENC:%d, VDEC:%d, VPRX:%d, RCV:%d, ERR:0x%x",
                result_payload[0], result_payload[1], result_payload[2], result_payload[3],
                result_payload[4], result_payload[5], result_payload[6], result_payload[7]);
}

/*************************************************
Function:       vrm_data_store
Description:    set vrm related data, just use for debugging
Input:          driver: target device driver
                buf:debugging on or off
Output:         null
Return:         count
Others:
*************************************************/
static ssize_t vrm_data_store(struct device_driver *drv, const char *buf, size_t count)
{
    unsigned long nstate = 0;
    int *param = NULL;

    if (NULL == buf) {
        pr_err("%s: buf is null", __func__);
        return 0;
    }

    if (count > 0)
    {
        while (' ' == *buf)
        buf++;

        if (0 == strncmp(buf, "on", 2)) {
            nstate = 1;
        }
        else if (0 == strncmp(buf, "off", 3)) {
            nstate = 0;
        }
        else {
            pr_err("%s, invalid param\n", __func__);
            return count;
        }

        param = (int *) g_payload;
        param[0] = nstate;
        param[1] = VRM_APRV2_RSV_CODE;

        (void)afe_vrm_param_ctrl(VRM_DIR_WRITE, 16, (uint8_t *)param);
    }
    return count;
}
DRIVER_ATTR(vrm_data, 0664, vrm_data_show, vrm_data_store);
#endif //CONFIG_SND_SOC_VRM

/*************************************************
Function:       audio_extpa_to_hphr_show
Description:    this node is used to distinguish if the extpa is connect to HPHR
Input:          driver: target device driver
Output:         buf:buffer for audio_extpa_to_hphr data
Return:         size_t of "audio_extpa_to_hphr"
Others:         "1"means extpa is connect to HPHR
*************************************************/
static ssize_t audio_extpa_to_hphr_show(struct device_driver *driver, char *buf)
{
    if (NULL == buf) {
        pr_err("%s: buf is null", __func__);
        return 0;
    }
    return scnprintf(buf, PAGE_SIZE, "%u", audio_extpa_to_hphr);
}
DRIVER_ATTR(audio_extpa_to_hphr, 0444, audio_extpa_to_hphr_show, NULL);


static struct attribute *audio_attrs[] = {
    &driver_attr_audio_property.attr,
    &driver_attr_product_identifier.attr,
    &driver_attr_aud_param_ver.attr,
    &driver_attr_speaker_pa.attr,
    &driver_attr_smartpa_num.attr,
    &driver_attr_smartpa_i2s.attr,
    &driver_attr_stero_smartpa.attr,
    &driver_attr_codec_dump.attr,
    &driver_attr_audio_ce_param_inside.attr,
    &driver_attr_speaker_test_content.attr,
#ifdef CONFIG_SND_SOC_VRM
    &driver_attr_vrm_data.attr,
#endif // CONFIG_SND_SOC_VRM
    &driver_attr_smartpa_algo_paramsize.attr,
    &driver_attr_voice_lvm_mode.attr,
    &driver_attr_voice_vol_level.attr,
    &driver_attr_audio_extpa_to_hphr.attr,
    NULL,
};

static struct attribute_group audio_group = {
    .name = "hw_audio_info",
    .attrs = audio_attrs,
};

static const struct attribute_group *groups[] = {
    &audio_group,
    NULL,
};

static struct of_device_id audio_info_match_table[] = {
    { .compatible = "hw,hw_audio_info",},
    { },
};

bool mic1_differential_mode_enable(void)
{
    return mic1_differential_mode_enabled;
}

/*************************************************
Function:       hw_audio_property_init
Description:    init audio_property according to dtsi configure
Input:          device node
Output:         null
Return:         void
Others:
*************************************************/
static void hw_audio_property_init(struct device_node *of_node)
{
    if (NULL == of_node) {
        pr_err("hw_audio: audio_property_init failed, of_node is NULL\n");
        return;
    }

    if (of_property_read_bool(of_node, AUDIO_PROP_MASTER_MIC_EXIST_NODE)) {
        audio_property |= AUDIO_PROP_MASTER_MIC_EXIST_MASK;
    } else {
        pr_err("hw_audio: check mic config, no master mic found\n");
#ifdef CONFIG_HUAWEI_DSM_AUDIO
        audio_dsm_report_info(AUDIO_CODEC, DSM_AUDIO_CARD_LOAD_FAIL_ERROR_NO, "master mic not found!");
#endif
    }

    if (of_property_read_bool(of_node, AUDIO_PROP_SECONDARY_MIC_EXIST_NODE)) {
        audio_property |= AUDIO_PROP_SECONDARY_MIC_EXIST_MASK;
    }

    if (of_property_read_bool(of_node, AUDIO_PROP_ERROR_MIC_EXIST_NODE)) {
        audio_property |= AUDIO_PROP_ERROR_MIC_EXIST_MASK;
    }

    if (of_property_read_bool(of_node, AUDIO_PROP_HANDHELD_MASTER_MIC_NODE)) {
        audio_property |= AUDIO_PROP_HANDHELD_MASTER_MIC;
    }

    if (of_property_read_bool(of_node, AUDIO_PROP_HANDHELD_DUAL_MIC_NODE)) {
        audio_property |= AUDIO_PROP_HANDHELD_DUAL_MIC;
    }

    if (of_property_read_bool(of_node, AUDIO_PROP_HANDHELD_AANC_MIC_NODE)) {
        audio_property |= AUDIO_PROP_HANDHELD_AANC_MIC;
    }

    if (of_property_read_bool(of_node, AUDIO_LOUDSPEAKER_MASTER_MIC_NODE)) {
        audio_property |= AUDIO_LOUDSPEAKER_MASTER_MIC;
    }

    if (of_property_read_bool(of_node, AUDIO_LOUDSPEAKER_SECOND_MIC_NODE)) {
        audio_property |= AUDIO_LOUDSPEAKER_SECOND_MIC;
    }

    if (of_property_read_bool(of_node, AUDIO_LOUDSPEAKER_ERROR_MIC_NODE)) {
        audio_property |= AUDIO_LOUDSPEAKER_ERROR_MIC;
    }

    if (of_property_read_bool(of_node, AUDIO_LOUDSPEAKER_DUAL_MIC_NODE)) {
        audio_property |= AUDIO_LOUDSPEAKER_DUAL_MIC;
    }

    return;
}

/*************************************************
Function:       hw_audio_read_string
Description:    read string property according to dtsi configure
Input:          device_node/propname/strdest/count
Output:         strdest
Return:         void
Others:
*************************************************/
static void hw_audio_read_string(struct device_node *of_node,
                                                               const char *propname,
                                                               void *strdest,
                                                               size_t count)
{
    int ret = 0;
    const char *string = NULL;

    if ((NULL == of_node) ||(NULL == strdest)) {
        pr_err("hw_audio: hw_audio_read_string failed, ptr is NULL\n");
        return;
    }

    ret = of_property_read_string(of_node, propname, &string);
    if (ret || (NULL == string)) {
        pr_err("hw_audio: read_string %s failed %d\n", propname, ret);
    } else {
        memset(strdest, 0, count);
        strlcpy(strdest, string, count);
        pr_info("%s: getted %s = %d", __func__, propname, (int)count);
    }

    return;
}

/*************************************************
Function:       hw_audio_spk_content_init
Description:    init speaker_test_content according to dtsi configure, 
                this mainly used by multi-smartpa and multi-speaker product
Input:          device_node
Output:         null
Return:         void
Others:
*************************************************/
static void hw_audio_spk_content_init(struct device_node *of_node)
{
    int ret = 0;
    const char *string = NULL;
    int strcount = 0;
    int i = 0;
    char *ptr = NULL;

    if (NULL == of_node) {
        pr_err("hw_audio: hw_audio_read_string failed, of_node is NULL\n");
        return;
    }

    strcount = of_property_count_strings(of_node, SPEAKER_TEST_CONTENT);
    if (strcount <= 0) {
        pr_err("%s: missing %s in dt node or length is incorrect, set to default \n", __func__, SPEAKER_TEST_CONTENT);
        strlcpy(speaker_test_content, "speaker", strlen("speaker")+1);
    } else {
        string = NULL;
        ptr = speaker_test_content;
        memset(speaker_test_content, 0, AUDIO_BOX_NAME_SIZE);
        for (i = 0; i < strcount; i++) {
            ret = of_property_read_string_index(of_node, SPEAKER_TEST_CONTENT, i, &string);
            if (ret) {
                pr_err("%s:of read string %s i %d error %d\n",
                        __func__, SPEAKER_TEST_CONTENT, i, ret);
            } else {
                memcpy(ptr, string, strlen(string));
                ptr += strlen(string);
                *(ptr++) = ',';
            }
            string = NULL;
        }
        if (ptr != speaker_test_content)
            *(--ptr) = '\0';
    }

    return;
}

/*************************************************
Function:       audio_info_probe
Description:    audio info probe, according to dtsi configure
Input:          pdev
Output:         null
Return:         0
Others:
*************************************************/
static int audio_info_probe(struct platform_device *pdev)
{
    int ret = 0;

    if (NULL == pdev) {
        pr_err("hw_audio: audio_info_probe failed, pdev is NULL\n");
        return 0;
    }

    if (NULL == pdev->dev.of_node) {
        pr_err("hw_audio: audio_info_probe failed, of_node is NULL\n");
        return 0;
    }

    hw_audio_property_init(pdev->dev.of_node);

    hw_audio_read_string(pdev->dev.of_node, PRODUCT_IDENTIFIER_NODE,
                                          product_identifier, sizeof(product_identifier));
    hw_audio_read_string(pdev->dev.of_node, VOICE_LVM_MODE,
                                          voice_lvm_mode, sizeof(voice_lvm_mode));
    /* we may use two different smartpa chip for one product, so get smart_pa info from
     * smartpa kernel driver, instead of dtsi*/
    /*hw_audio_read_string(pdev->dev.of_node, SPEAKER_PA_NODE,
                                          speaker_pa, sizeof(speaker_pa));*/
    hw_audio_read_string(pdev->dev.of_node, AUD_PARAM_VER_NODE,
                                          aud_param_ver, sizeof(aud_param_ver));

    if (of_property_read_bool(pdev->dev.of_node, RPODUCT_STERO_SMARTPA_CONFIG))
        audio_stero_smartpa = 1;            /* 1 means: dual smartpa is supported*/

    if (of_property_read_bool(pdev->dev.of_node, AUDIO_CE_PARAM_INSIDE))
        audio_ce_param_inside = 1;            /* 1 means: different ce parameter is needed*/

    if (of_property_read_bool(pdev->dev.of_node, AUDIO_EXTPA_TO_HPHR))
        audio_extpa_to_hphr = 1;            /* 1 means: audio ext pa connect to HPHR*/

    if (of_property_read_bool(pdev->dev.of_node, MIC1_DIFF_MODE_ENABLE))
        mic1_differential_mode_enabled = true;        /* true means: enable mic1 differential mode*/

    /* redefined algo patameter size for some smartpa product, if needed.*/
    ret = of_property_read_u32(pdev->dev.of_node, SMARTPA_ALGO_PARAMSIZE,
            &smartpa_algo_paramsize);
    if (ret) {
        pr_err("hw_audio: read %s failed %d\n", SMARTPA_ALGO_PARAMSIZE, ret);
        smartpa_algo_paramsize = SMARTPA_ALGO_PARAMSIZE_DEFAULT;
    }

    /* get smartpa num, mainly used by multi-smartpa product */
    ret = of_property_read_u32(pdev->dev.of_node, SMARTPA_NUM, &smartpa_num);
    if (ret) {
        pr_err("hw_audio: read %s failed %d\n", SMARTPA_NUM, ret);
        smartpa_num = 0;
    }

    /* get which i2s does smartpa use. this info maybe used by HAL */
    ret = of_property_read_u32(pdev->dev.of_node, SMARTPA_I2S, &smartpa_i2s);
    if (ret) {
        pr_err("hw_audio: read %s failed %d\n", SMARTPA_I2S, ret);
        smartpa_i2s = 0;
    }
	
	    /* get voice volume level. this info maybe used by HAL */
    ret = of_property_read_u32(pdev->dev.of_node, VOICE_VOL_LEVEL, &voice_vol_level);
    if (ret) {
        pr_err("hw_audio: read %s failed %d\n", VOICE_VOL_LEVEL, ret);
        voice_vol_level = 0;
    }

    hw_audio_spk_content_init(pdev->dev.of_node);

    return 0;
}

static struct platform_driver audio_info_driver = {
    .driver = {
        .name  = "hw_audio_info",
        .owner  = THIS_MODULE,
        .groups = groups,
        .of_match_table = audio_info_match_table,
    },

    .probe = audio_info_probe,
    .remove = NULL,
};

static int __init audio_info_init(void)
{
    return platform_driver_register(&audio_info_driver);
}

static void __exit audio_info_exit(void)
{
    platform_driver_unregister(&audio_info_driver);
}

module_init(audio_info_init);
module_exit(audio_info_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("hw audio info");

