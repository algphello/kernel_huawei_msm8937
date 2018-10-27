#include <linux/types.h>
#ifndef FASTBOOT_DUMP_REASON_H
#define FASTBOOT_DUMP_REASON_H
#define FASTBOOT_DUMP_HEAD_ADDR 0x93D00000
#define FASTBOOT_DUMP_HEAD_SIZE 0x01400000 /*20MB*/
#define FASTBOOT_DUMP_FILE_NAME_SIZE 32
#define FASTBOOT_REASON_MAGIC_NUMBER_MAX 99
#define FASTBOOT_DUMP_RESET_REASON_STR_MAX 256

#define FD_M_INIT		0
#define FD_S_INIT  		99

typedef struct
{
	uint32_t reset_reason_m_magic;
	uint32_t reset_reason_s_magic;
	char reset_s_reason[FASTBOOT_DUMP_RESET_REASON_STR_MAX];
	uint32_t reset_reason_s_flag;
	uint32_t reset_reason_info_flag;
	char reset_reason_info[FASTBOOT_DUMP_RESET_REASON_STR_MAX];
}fastboot_dump_reset_reason_info;

typedef struct
{
	uint64_t section_array_baseaddr;
	uint32_t section_number;
	uint64_t red_section_array_baseaddr;
	uint32_t red_section_number;
	uint32_t fastboot_dump_magic;
	uint64_t current_valid_addr;
	fastboot_dump_reset_reason_info reset_reason_info;
}fastboot_dump_header;
#endif