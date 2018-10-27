#include "fastboot_dump_reason.h"
#include <linux/string.h>
#include <linux/printk.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/kallsyms.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <asm/stacktrace.h>

extern int vscnprintf(char *buf, size_t size, const char *fmt, va_list args);
extern int vsnprintf(char *buf, size_t size, const char *fmt, va_list args);
static fastboot_dump_header *fastboot_dump_global_header_addr = NULL;

/*===========================================================================
**  Function :  fastboot_dump_header_addr_map
* @brief
*   This function initialize header addr.
*
* @param[in]
*   NA
*
* @par Dependencies
*
* @retval
*   NA
*
* @par Side Effects
*   NA
============================================================================*/
static fastboot_dump_header *fastboot_dump_header_addr_map(void)
{
	if(fastboot_dump_global_header_addr==NULL)
	{
		fastboot_dump_global_header_addr = (fastboot_dump_header *)ioremap_nocache(FASTBOOT_DUMP_HEAD_ADDR,FASTBOOT_DUMP_HEAD_SIZE);
		if(fastboot_dump_global_header_addr == NULL)
		{
			printk(KERN_ERR "fastboot_dump_header_addr_map ioremap_nocache fail \n");
			return NULL;
		}
		else
		{
			return fastboot_dump_global_header_addr;
		}
	}
	return fastboot_dump_global_header_addr;
}

/*===========================================================================
**  Function :  fastboot_dump_m_reason_set
* @brief
*   This function set main reason in kernel.
*
* @param[in]
*   NA
*
* @par Dependencies
*
* @retval
*   NA
*
* @par Side Effects
*   NA
============================================================================*/
void fastboot_dump_m_reason_set(uint32_t reason)
{
	fastboot_dump_header *fdump_header = fastboot_dump_header_addr_map();
	printk(KERN_ERR "fastboot_dump_m_reason_set start\n");
	if(fdump_header == NULL)
	{
		printk(KERN_ERR "fastboot_dump_m_reason_set:fdump_header NULL \n");
		return;
	}
	if(fdump_header->reset_reason_info.reset_reason_m_magic == FD_M_INIT)
	{
		fdump_header->reset_reason_info.reset_reason_m_magic = reason;
		printk(KERN_ERR "fastboot_dump_m_reason_set magic_number %d \n",reason);
	}
	else
	{
		printk(KERN_ERR "fastboot_dump_m_reason_set error or have recount\n");
	}
	return;
}
EXPORT_SYMBOL(fastboot_dump_m_reason_set);

/*===========================================================================
**  Function :  fastboot_dump_s_reason_set
* @brief
*   This function set second reason in kernel.
*
* @param[in]
*   NA
*
* @par Dependencies
*
* @retval
*   NA
*
* @par Side Effects
*   NA
============================================================================*/
void fastboot_dump_s_reason_set(uint32_t reason)
{
	fastboot_dump_header *fdump_header = fastboot_dump_header_addr_map();
	printk(KERN_ERR "fastboot_dump_s_reason_set start\n");
	if(fdump_header == NULL)
	{
		printk(KERN_ERR "fastboot_dump_s_reason_set:fdump_header NULL \n");
		return;
	}
	if(fdump_header->reset_reason_info.reset_reason_s_magic == FD_S_INIT)
	{
		fdump_header->reset_reason_info.reset_reason_s_magic = reason;
		printk(KERN_ERR "fastboot_dump_s_reason_set magic_number %d \n",reason);
	}
	else
	{
		printk(KERN_ERR "fastboot_dump_s_reason_set error or have recount\n");
	}
	return;
}
EXPORT_SYMBOL(fastboot_dump_s_reason_set);

/*===========================================================================
**  Function :  fastboot_dump_s_reason_str_set
* @brief
*   This function set second reason info in kernel.
*
* @param[in]
*   NA
*
* @par Dependencies
*
* @retval
*   NA
*
* @par Side Effects
*   NA
============================================================================*/
void fastboot_dump_s_reason_str_set(char *reason_s_info)
{
	fastboot_dump_header *fdump_header = fastboot_dump_header_addr_map();
	unsigned int str_tmp_len = strlen(reason_s_info);
	printk(KERN_ERR "fastboot_dump_s_reason_str_set start\n");
	if(fdump_header == NULL || reason_s_info == NULL)
	{
		return;
	}
	if(fdump_header->reset_reason_info.reset_reason_s_flag != 0)
	{
		printk(KERN_ERR "fastboot_dump_s_reason_set: have recount reset_s_reason skip\n");
		return;
	}
#ifdef CONFIG_ARM64
	memset_io(fdump_header->reset_reason_info.reset_s_reason,'\0',FASTBOOT_DUMP_RESET_REASON_STR_MAX);
#else
	memset(fdump_header->reset_reason_info.reset_s_reason,'\0',FASTBOOT_DUMP_RESET_REASON_STR_MAX);
#endif
	if(str_tmp_len>(FASTBOOT_DUMP_RESET_REASON_STR_MAX-1))
	{
		printk(KERN_ERR "fastboot_dump_s_reason_set reset_s_reason err\n");
	}
	else
	{
#ifdef CONFIG_ARM64
		memcpy_toio(fdump_header->reset_reason_info.reset_s_reason,reason_s_info,str_tmp_len);
		fdump_header->reset_reason_info.reset_s_reason[str_tmp_len]='\0';
#else
		strlcpy(fdump_header->reset_reason_info.reset_s_reason, reason_s_info, FASTBOOT_DUMP_RESET_REASON_STR_MAX);
#endif
		fdump_header->reset_reason_info.reset_reason_s_flag = 1;
	}
}
EXPORT_SYMBOL(fastboot_dump_s_reason_str_set);

/*===========================================================================
**  Function :  fastboot_dump_reset_reason_info_set
* @brief
*   This function set reset reason info in kernel.
*
* @param[in]
*   NA
*
* @par Dependencies
*
* @retval
*   NA
*
* @par Side Effects
*   NA
============================================================================*/
void fastboot_dump_reset_reason_info_set(char *reason_info)
{
	fastboot_dump_header *fdump_header = fastboot_dump_header_addr_map();
	unsigned int str_tmp_len = strlen(reason_info);
	printk(KERN_ERR "fastboot_dump_reset_reason_info_set: start\n");
	if(fdump_header == NULL || reason_info == NULL)
	{
		return;
	}
	if(fdump_header->reset_reason_info.reset_reason_info_flag != 0)
	{
		printk(KERN_ERR "fastboot_dump_reset_reason_info_set: have recount reset_reason_info skip\n");
		return;
	}
	if((fdump_header->reset_reason_info.reset_reason_s_flag == 0) && (fdump_header->reset_reason_info.reset_reason_m_magic == FASTBOOT_REASON_MAGIC_NUMBER_MAX))
	{
		printk(KERN_ERR "fastboot_dump_reset_reason_info_set: no panic no recount reset_reason_info\n");
		return;
	}
#ifdef CONFIG_ARM64
	memset_io(fdump_header->reset_reason_info.reset_reason_info,'\0',FASTBOOT_DUMP_RESET_REASON_STR_MAX);
#else
	memset(fdump_header->reset_reason_info.reset_reason_info,'\0',FASTBOOT_DUMP_RESET_REASON_STR_MAX);
#endif
	if(str_tmp_len>(FASTBOOT_DUMP_RESET_REASON_STR_MAX-1))
	{
		printk(KERN_ERR "fastboot_dump_reset_reason_info_set reason_info err\n");
	}
	else
	{
#ifdef CONFIG_ARM64
		memcpy_toio(fdump_header->reset_reason_info.reset_reason_info,reason_info,str_tmp_len);
		fdump_header->reset_reason_info.reset_reason_info[str_tmp_len]='\0';
#else
		strlcpy(fdump_header->reset_reason_info.reset_reason_info, reason_info, FASTBOOT_DUMP_RESET_REASON_STR_MAX);
#endif
		strlcpy(fdump_header->reset_reason_info.reset_reason_info, reason_info, FASTBOOT_DUMP_RESET_REASON_STR_MAX);
		fdump_header->reset_reason_info.reset_reason_info_flag = 1;
	}
}
EXPORT_SYMBOL(fastboot_dump_reset_reason_info_set);

/*===========================================================================
**  Function :  fastboot_dump_reset_reason_info_set
* @brief
*   This function set reset reason info in kernel.
*
* @param[in]
*   NA
*
* @par Dependencies
*
* @retval
*   NA
*
* @par Side Effects
*   NA
============================================================================*/
void fastboot_dump_reset_reason_info_str_set(const char *fmt, ...)
{
	char buf[FASTBOOT_DUMP_RESET_REASON_STR_MAX]={0};
	int err;
	if(fmt == NULL)
	{
		return;
	}
	va_list ap;
	va_start(ap, fmt);
	err = vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	if(err<0)
	{
		printk(KERN_ERR "fastboot_dump_reset_reason_info_str_set vsnprintf fail\n");
		return;
	}
	printk(KERN_ERR "fastboot_dump reset_s_reason str:%s\n",buf);
	fastboot_dump_reset_reason_info_set(buf);
}
EXPORT_SYMBOL(fastboot_dump_reset_reason_info_str_set);

/*===========================================================================
**  Function :  fastboot_dump_reset_reason_info_regs_kallsyms_set
* @brief
*   This function can set "pc is at xxx" in reset reason info.
*
* @param[in]
*   NA
*
* @par Dependencies
*
* @retval
*   NA
*
* @par Side Effects
*   NA
============================================================================*/
void fastboot_dump_reset_reason_info_regs_kallsyms_set(const char *fmt, unsigned long addr)
{
	int err;
	char temp_reason_info_buffer[FASTBOOT_DUMP_RESET_REASON_STR_MAX]={0};
	if(fmt == NULL)
	{
		return;
	}
	char kallsyms_buffer[KSYM_SYMBOL_LEN];
	__check_printsym_format(fmt, "");
	sprint_symbol(kallsyms_buffer, (unsigned long)__builtin_extract_return_addr((void *)addr));
	err = snprintf(temp_reason_info_buffer, FASTBOOT_DUMP_RESET_REASON_STR_MAX, fmt, kallsyms_buffer);
	if(err<0)
	{
		printk(KERN_ERR "fastboot_dump_reset__reason_info_regs_kallsyms_set snprintf fail\n");
		return;
	}
	printk(KERN_ERR "fastboot_dump reason_info str:%s\n",temp_reason_info_buffer);
	fastboot_dump_reset_reason_info_set(temp_reason_info_buffer);
}
EXPORT_SYMBOL(fastboot_dump_reset_reason_info_regs_kallsyms_set);

/*===========================================================================
**  Function :  fastboot_dump_s_reason_str_set
* @brief
*   This function set second reason info in kernel.
*
* @param[in]
*   NA
*
* @par Dependencies
*
* @retval
*   NA
*
* @par Side Effects
*   NA
============================================================================*/
void fastboot_dump_s_reason_str_set_format(const char *fmt, ...)
{
	char buf[FASTBOOT_DUMP_RESET_REASON_STR_MAX]={0};
	int err;
	va_list ap;
	if(fmt == NULL)
	{
		return;
	}
	va_start(ap, fmt);
	err = vscnprintf(buf, FASTBOOT_DUMP_RESET_REASON_STR_MAX, fmt, ap);
	va_end(ap);
	if(err<0)
	{
		printk(KERN_ERR "fastboot_dump_s_reason_str_set vsnprintf fail\n");
		return;
	}
	printk(KERN_ERR "fastboot_dump reset_s_reason str:%s\n",buf);
	fastboot_dump_s_reason_set(buf);
}
EXPORT_SYMBOL(fastboot_dump_s_reason_str_set_format);

/*===========================================================================
**  Function :  fastboot_dump_init
* @brief
*   This function init reserve ddr address.
*
* @param[in]
*   NA
*
* @par Dependencies
*
* @retval
*   NA
*
* @par Side Effects
*   NA
============================================================================*/
static int __init fastboot_dump_init(void)
{
	printk(KERN_ERR "fastboot_dump_init:start\n");
	fastboot_dump_header_addr_map();
	return 0;
}

/*===========================================================================
**  Function :  fastboot_dump_exit
* @brief
*   This function unmap reserve ddr address.
*
* @param[in]
*   NA
*
* @par Dependencies
*
* @retval
*   NA
*
* @par Side Effects
*   NA
============================================================================*/
static void __exit fastboot_dump_exit(void)
{
	if(fastboot_dump_global_header_addr!=NULL)
	{
		iounmap((void*)fastboot_dump_global_header_addr);
	}
	return;
}
module_init(fastboot_dump_init);
module_exit(fastboot_dump_exit);
MODULE_LICENSE("GPL");
