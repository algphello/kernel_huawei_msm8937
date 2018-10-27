#ifdef CONFIG_HUAWEI_WIFI
#include <linux/platform_device.h>
#include <linux/firmware.h>
#include <linux/wcnss_wlan.h>


#define NVBIN_FILE                "../../vendor/etc/wifi/WCNSS_hw_wlan_nv.bin"
#define NVBIN_FILE_QCOM_DEFAULT   "wlan/prima/WCNSS_qcom_wlan_nv.bin"
#define NVBIN_PATH_LENTH           70

extern const void *get_hw_wifi_pubfile_id(void);
extern const void *get_hw_wifi_ini_type(void);


/**------------------------------------------------------------------------
  \brief construct_nvbin_with_pubfd() -construct wlan nvbin file path
         with pubfd which is defined in the dtsi
  \sa
  -------------------------------------------------------------------------*/
int construct_nvbin_with_pubfd(char *nvbin_path)
{
    const char *pubfd = NULL;
    char nvbin_path_with_pubfd[NVBIN_PATH_LENTH] = {0};

    pubfd = get_hw_wifi_pubfile_id();
    if( NULL != pubfd ) {
        pr_err("%s pubfd:%s;\n", __func__,pubfd);
    }else {
        pr_err("%s get pubfd failed;\n", __func__);
        return -1;
    }
    strncpy(nvbin_path_with_pubfd, "../../vendor/etc/wifi/WCNSS_hw_wlan_nv_",NVBIN_PATH_LENTH - 1);
    pr_err("%s line:%d nvbin_path_with_pubfd:%s;\n", __func__,__LINE__,nvbin_path_with_pubfd);

    strncat(nvbin_path_with_pubfd,pubfd,NVBIN_PATH_LENTH - 1);
    pr_err("%s line:%d nvbin_path_with_pubfd:%s;\n", __func__,__LINE__,nvbin_path_with_pubfd);
    strncat(nvbin_path_with_pubfd,".bin",NVBIN_PATH_LENTH - 1);
    pr_err("%s line:%d nvbin_path_with_pubfd:%s;\n", __func__,__LINE__,nvbin_path_with_pubfd);
    strncpy(nvbin_path,nvbin_path_with_pubfd,NVBIN_PATH_LENTH - 1);
    return 0;;
}
EXPORT_SYMBOL(construct_nvbin_with_pubfd);

/**------------------------------------------------------------------------
  \brief construct_configini_with_ini_type() -construct wlan configini file path
         with pubfd which is defined in the dtsi
  \sa
  -------------------------------------------------------------------------*/
int construct_configini_with_ini_type(char *configini_path)
{
    const char *ini_type = NULL;
    char configini_path_with_ini_type[NVBIN_PATH_LENTH] = {0};
    int err = 0;

    ini_type = get_hw_wifi_ini_type();
    if( NULL != ini_type ) {
        pr_err("%s ini_type:%s;\n", __func__,ini_type);
    }else {
        pr_err("%s get ini_type failed;\n", __func__);
        err = -1;
        return err;
    }
    strncpy(configini_path_with_ini_type, "../../vendor/etc/wifi/WCNSS_qcom_cfg_",NVBIN_PATH_LENTH - 1);
    pr_err("%s line:%d construct_configini_with_ini_type:%s;\n", __func__,__LINE__,configini_path_with_ini_type);
    strncat(configini_path_with_ini_type,ini_type,NVBIN_PATH_LENTH - 1);
    pr_err("%s line:%d construct_configini_with_ini_type:%s;\n", __func__,__LINE__,configini_path_with_ini_type);
    strncat(configini_path_with_ini_type,".ini",NVBIN_PATH_LENTH - 1);
    pr_err("%s line:%d construct_configini_with_ini_type:%s;\n", __func__,__LINE__,configini_path_with_ini_type);
    strncpy(configini_path,configini_path_with_ini_type,NVBIN_PATH_LENTH - 1);
    return err;
}
EXPORT_SYMBOL(construct_configini_with_ini_type);

int wcnss_auto_nvbin_dnld(const struct firmware **nv,struct device *dev)
{
	int ret = 0;	
	char nvbin_path_with_pubfd[NVBIN_PATH_LENTH] = {0};

    ret = construct_nvbin_with_pubfd(nvbin_path_with_pubfd);
	if(!ret){
		ret = request_firmware(nv, nvbin_path_with_pubfd, dev);
	}

	if (ret || !*nv || !((struct firmware *)*nv)->data || !((struct firmware *)*nv)->size) {
		pr_err("wcnss: %s: request_firmware failed for %s (ret = %d)\n",
			__func__, nvbin_path_with_pubfd, ret);
	    ret = request_firmware(nv, NVBIN_FILE, dev);
		if (ret || !*nv || !((struct firmware *)*nv)->data || !((struct firmware *)*nv)->size) {
			pr_err("wcnss: %s: request_firmware failed for %s (ret = %d)\n",
				__func__, NVBIN_FILE, ret);
		    ret = request_firmware(nv, NVBIN_FILE_QCOM_DEFAULT, dev);
		    pr_err("wcnss: %s: firmware_path %s\n",
			    __func__, NVBIN_FILE_QCOM_DEFAULT);
		    if (ret || !*nv || !((struct firmware *)*nv)->data || !((struct firmware *)*nv)->size) {
			    pr_err("wcnss: %s: request_firmware failed for %s (ret = %d)\n",
	                            __func__, NVBIN_FILE_QCOM_DEFAULT, ret);
		        return -1;
		    } else {
			    pr_err("wcnss: %s:download firmware_path %s successed;\n",
			        __func__, NVBIN_FILE_QCOM_DEFAULT);
		    }
		} else {
			pr_err("wcnss: %s:download firmware_path %s successed;\n",
			    __func__, NVBIN_FILE);
		}
	} else {
		pr_err("wcnss: %s:download firmware_path %s successed;\n",
		    __func__, nvbin_path_with_pubfd);
	}
	return 0;
}
EXPORT_SYMBOL(wcnss_auto_nvbin_dnld);

#endif

