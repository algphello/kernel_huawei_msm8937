#ifdef CONFIG_HUAWEI_WIFI
#include <linux/firmware.h>
#include "vos_types.h"
#include "vos_status.h"
#include "vos_nvitem.h"
#include "vos_trace.h"
#include "wlan_hdd_misc.h"

extern int construct_nvbin_with_pubfd(char *nvbin_path);

VOS_STATUS vos_auto_nvbin_load(v_VOID_t *pHDDContext,v_VOID_t **pnvtmpBuf,v_SIZE_t *nvReadBufSize)
{
    char nvbin_path_with_pubfd[NVBIN_PATH_LENTH] = {0};
	int status = 0;

	status = hdd_request_firmware(HWCUST_WLAN_CAL_NV_FILE,
								pHDDContext,
								pnvtmpBuf, nvReadBufSize);

		if ((!VOS_IS_STATUS_SUCCESS( status )) || (!pnvtmpBuf))
		{
			VOS_TRACE(VOS_MODULE_ID_VOSS, VOS_TRACE_LEVEL_FATAL,
				 "%s: unable to download NV file %s",
				 __func__, HWCUST_WLAN_CAL_NV_FILE);
			status = construct_nvbin_with_pubfd(nvbin_path_with_pubfd);
			VOS_TRACE(VOS_MODULE_ID_VOSS, VOS_TRACE_LEVEL_FATAL,
				 "%s:we will download NV file %s",
				 __func__, nvbin_path_with_pubfd);
			if(!status){
				status = hdd_request_firmware(nvbin_path_with_pubfd,
						 pHDDContext,
						 pnvtmpBuf, nvReadBufSize);
			}
			if ((!VOS_IS_STATUS_SUCCESS( status )) || (!pnvtmpBuf))
			{
			  VOS_TRACE(VOS_MODULE_ID_VOSS, VOS_TRACE_LEVEL_FATAL,
					 "%s:unable to download NV file %s and we will download NV file %s",
					 __func__, nvbin_path_with_pubfd,HW_WLAN_NV_FILE);
			  status = hdd_request_firmware(HW_WLAN_NV_FILE,
						 pHDDContext,
						 pnvtmpBuf, nvReadBufSize);
			  if ((!VOS_IS_STATUS_SUCCESS( status )) || (!pnvtmpBuf))
			  {
				  VOS_TRACE(VOS_MODULE_ID_VOSS, VOS_TRACE_LEVEL_FATAL,
						 "%s: unable to download NV file %s and we will download %s;",
						 __func__, HW_WLAN_NV_FILE,NVBIN_FILE_QCOM_DEFAULT);
				  status = hdd_request_firmware(NVBIN_FILE_QCOM_DEFAULT,
							 pHDDContext,
							 pnvtmpBuf, nvReadBufSize);
				  if ((!VOS_IS_STATUS_SUCCESS( status )) || (!pnvtmpBuf))
				  {
					  VOS_TRACE(VOS_MODULE_ID_VOSS, VOS_TRACE_LEVEL_FATAL,
							 "%s: unable to download NV file %s",
							 __func__, NVBIN_FILE_QCOM_DEFAULT);
					  return VOS_STATUS_E_RESOURCES;
				  }
				  else
				  {
					  VOS_TRACE(VOS_MODULE_ID_VOSS, VOS_TRACE_LEVEL_FATAL,
							 "%s: download NV file %s successfully",
							 __func__, NVBIN_FILE_QCOM_DEFAULT);
				  }
			  }
			  else
			  {
				  VOS_TRACE(VOS_MODULE_ID_VOSS, VOS_TRACE_LEVEL_FATAL,
						 "%s: download NV file %s successfully",
						 __func__, HW_WLAN_NV_FILE);
			  }
			}
			else
			{
			  VOS_TRACE(VOS_MODULE_ID_VOSS, VOS_TRACE_LEVEL_FATAL,
					 "%s: download NV file %s successfully",
					 __func__, nvbin_path_with_pubfd);
			}
		}
		else
		{
			VOS_TRACE(VOS_MODULE_ID_VOSS, VOS_TRACE_LEVEL_FATAL,
					 "%s: download NV file %s successfully",
					 __func__, HWCUST_WLAN_CAL_NV_FILE);
		}
	return VOS_STATUS_SUCCESS;
}

EXPORT_SYMBOL(vos_auto_nvbin_load);

#endif

