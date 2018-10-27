#ifdef CONFIG_HUAWEI_WIFI
#include <linux/firmware.h>
#include <linux/string.h>
#include <wlan_hdd_includes.h>
#include <wlan_hdd_main.h>
#include <wlan_hdd_assoc.h>
#include <wlan_hdd_cfg.h>
#include <linux/string.h>
#include <vos_types.h>
#include <wlan_hdd_misc.h>

extern int construct_configini_with_ini_type(char *configini_path);

VOS_STATUS drv_auto_nvbin_load(const struct firmware **fw,struct device *dev)
{
	int status = 0;
	char configini_path_with_ini_type[NVBIN_PATH_LENTH] = {0};

	if(!dev){
	   return VOS_STATUS_E_RESOURCES;
	}

    status = construct_configini_with_ini_type(configini_path_with_ini_type);
	if(!status)
	{
		status = request_firmware(fw, configini_path_with_ini_type, dev);
	}

	if(status || !*fw || !((struct firmware *)*fw)->data || !((struct firmware *)*fw)->size) {
      pr_err("wcnss: %s: request_firmware failed for %s (status = %d)\n",
         __func__, configini_path_with_ini_type, status);
      status = request_firmware(fw, WLAN_INI_FILE, dev);
      if(status || !*fw || !((struct firmware *)*fw)->data || !((struct firmware *)*fw)->size)
      {
          hddLog(VOS_TRACE_LEVEL_FATAL, "%s: %s download failed %d",__func__, WLAN_INI_FILE,status);
          return VOS_STATUS_E_FAILURE;
      }else{
      pr_err("wcnss: %s:download firmware_path %s successed;\n",
          __func__, WLAN_INI_FILE);
      }
   }else {
		pr_err("wcnss: %s:download firmware_path %s successed;\n",
          __func__, configini_path_with_ini_type);
   }

   return VOS_STATUS_SUCCESS;
}

/*EXPORT_SYMBOL(drv_auto_nvbin_load);»áÒýÆð±àÒë¶Î´íÎó*/

#endif

