/*
 * f2fs sysfs interface
 *
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *             http://www.samsung.com/
 * Copyright (c) 2017 Chao Yu <chao@kernel.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/proc_fs.h>
#include <linux/f2fs_fs.h>
#include <linux/seq_file.h>

#include "f2fs.h"
#include "segment.h"
#include "gc.h"

static struct proc_dir_entry *f2fs_proc_root;
static struct kset *f2fs_kset;

/* Sysfs support for f2fs */
enum {
	GC_THREAD,	/* struct f2fs_gc_thread */
	SM_INFO,	/* struct f2fs_sm_info */
	DCC_INFO,	/* struct discard_cmd_control */
	NM_INFO,	/* struct f2fs_nm_info */
	F2FS_SBI,	/* struct f2fs_sb_info */
#ifdef CONFIG_F2FS_FAULT_INJECTION
	FAULT_INFO_RATE,	/* struct f2fs_fault_info */
	FAULT_INFO_TYPE,	/* struct f2fs_fault_info */
#endif
	RESERVED_BLOCKS,	/* struct f2fs_sb_info */
#ifdef CONFIG_F2FS_OVP_RESERVED
	OVP_RESERVED_BLOCKS,	/* struct f2fs_sb_info */
#endif
};

struct f2fs_attr {
	struct attribute attr;
	ssize_t (*show)(struct f2fs_attr *, struct f2fs_sb_info *, char *);
	ssize_t (*store)(struct f2fs_attr *, struct f2fs_sb_info *,
			 const char *, size_t);
	int struct_type;
	int offset;
};

static unsigned char *__struct_ptr(struct f2fs_sb_info *sbi, int struct_type)
{
	if (struct_type == GC_THREAD)
		return (unsigned char *)sbi->gc_thread;
	else if (struct_type == SM_INFO)
		return (unsigned char *)SM_I(sbi);
	else if (struct_type == DCC_INFO)
		return (unsigned char *)SM_I(sbi)->dcc_info;
	else if (struct_type == NM_INFO)
		return (unsigned char *)NM_I(sbi);
#ifdef CONFIG_F2FS_OVP_RESERVED
	else if (struct_type == F2FS_SBI || struct_type == RESERVED_BLOCKS ||
		struct_type == OVP_RESERVED_BLOCKS)
#else
	else if (struct_type == F2FS_SBI || struct_type == RESERVED_BLOCKS)
#endif
		return (unsigned char *)sbi;
#ifdef CONFIG_F2FS_FAULT_INJECTION
	else if (struct_type == FAULT_INFO_RATE ||
					struct_type == FAULT_INFO_TYPE)
		return (unsigned char *)&sbi->fault_info;
#endif
	return NULL;
}

static ssize_t lifetime_write_kbytes_show(struct f2fs_attr *a,
		struct f2fs_sb_info *sbi, char *buf)
{
	struct super_block *sb = sbi->sb;

	if (!sb->s_bdev->bd_part)
		return snprintf(buf, PAGE_SIZE, "0\n");

	return snprintf(buf, PAGE_SIZE, "%llu\n",
		(unsigned long long)(sbi->kbytes_written +
			BD_PART_WRITTEN(sbi)));
}

static ssize_t current_reserved_blocks_show(struct f2fs_attr *a,
					struct f2fs_sb_info *sbi, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%u\n", sbi->current_reserved_blocks);
}

#ifdef CONFIG_F2FS_OVP_RESERVED
static ssize_t ovp_current_reserved_blocks_show(struct f2fs_attr *a,
					struct f2fs_sb_info *sbi, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%u\n", OVP_CUR_RSVD_BLOCKS(sbi));
}
#endif

static ssize_t f2fs_sbi_show(struct f2fs_attr *a,
			struct f2fs_sb_info *sbi, char *buf)
{
	unsigned char *ptr = NULL;
	unsigned int *ui;

	ptr = __struct_ptr(sbi, a->struct_type);
	if (!ptr)
		return -EINVAL;

	ui = (unsigned int *)(ptr + a->offset);

	return snprintf(buf, PAGE_SIZE, "%u\n", *ui);
}

static ssize_t f2fs_sbi_store(struct f2fs_attr *a,
			struct f2fs_sb_info *sbi,
			const char *buf, size_t count)
{
	unsigned char *ptr;
	unsigned long t;
	unsigned int *ui;
	ssize_t ret;

	ptr = __struct_ptr(sbi, a->struct_type);
	if (!ptr)
		return -EINVAL;

	ui = (unsigned int *)(ptr + a->offset);

	ret = kstrtoul(skip_spaces(buf), 0, &t);
	if (ret < 0)
		return ret;
#ifdef CONFIG_F2FS_FAULT_INJECTION
	if (a->struct_type == FAULT_INFO_TYPE && t >= (1 << FAULT_MAX))
		return -EINVAL;
#endif
	if (a->struct_type == RESERVED_BLOCKS) {
		spin_lock(&sbi->stat_lock);
		if (t > (unsigned long)sbi->user_block_count) {
			spin_unlock(&sbi->stat_lock);
			return -EINVAL;
		}
		*ui = t;
		sbi->current_reserved_blocks = min(sbi->reserved_blocks,
				sbi->user_block_count - valid_user_blocks(sbi));/*lint !e666*/
#ifdef CONFIG_F2FS_OVP_RESERVED
		update_ovp_rsvd_max_blocks(sbi);
		update_ovp_cur_rsvd_blocks(sbi);
#endif
		spin_unlock(&sbi->stat_lock);
		return count;
	}
#ifdef CONFIG_F2FS_OVP_RESERVED
	if (a->struct_type == OVP_RESERVED_BLOCKS) {
		spin_lock(&sbi->stat_lock);
		if (t > (unsigned long)sbi->ovp_rsvd_end_free) {
			spin_unlock(&sbi->stat_lock);
			return -EINVAL;
		}
		*ui = t;
		update_ovp_rsvd_max_blocks(sbi);
		update_ovp_cur_rsvd_blocks(sbi);

		spin_unlock(&sbi->stat_lock);
		return count;
	}
#endif
	*ui = t;
	return count;
}

static ssize_t f2fs_attr_show(struct kobject *kobj,
				struct attribute *attr, char *buf)
{
	struct f2fs_sb_info *sbi = container_of(kobj, struct f2fs_sb_info,
								s_kobj);
	struct f2fs_attr *a = container_of(attr, struct f2fs_attr, attr);

	return a->show ? a->show(a, sbi, buf) : 0;
}

static ssize_t f2fs_attr_store(struct kobject *kobj, struct attribute *attr,
						const char *buf, size_t len)
{
	struct f2fs_sb_info *sbi = container_of(kobj, struct f2fs_sb_info,
									s_kobj);
	struct f2fs_attr *a = container_of(attr, struct f2fs_attr, attr);

	return a->store ? a->store(a, sbi, buf, len) : 0;
}

static void f2fs_sb_release(struct kobject *kobj)
{
	struct f2fs_sb_info *sbi = container_of(kobj, struct f2fs_sb_info,
								s_kobj);
	complete(&sbi->s_kobj_unregister);
}

#define F2FS_ATTR_OFFSET(_struct_type, _name, _mode, _show, _store, _offset) \
static struct f2fs_attr f2fs_attr_##_name = {			\
	.attr = {.name = __stringify(_name), .mode = _mode },	\
	.show	= _show,					\
	.store	= _store,					\
	.struct_type = _struct_type,				\
	.offset = _offset					\
}

#define F2FS_RW_ATTR(struct_type, struct_name, name, elname)	\
	F2FS_ATTR_OFFSET(struct_type, name, 0644,		\
		f2fs_sbi_show, f2fs_sbi_store,			\
		offsetof(struct struct_name, elname))

#define F2FS_GENERAL_RO_ATTR(name) \
static struct f2fs_attr f2fs_attr_##name = __ATTR(name, 0444, name##_show, NULL)

F2FS_RW_ATTR(GC_THREAD, f2fs_gc_kthread, gc_min_sleep_time, min_sleep_time);
F2FS_RW_ATTR(GC_THREAD, f2fs_gc_kthread, gc_max_sleep_time, max_sleep_time);
F2FS_RW_ATTR(GC_THREAD, f2fs_gc_kthread, gc_no_gc_sleep_time, no_gc_sleep_time);
F2FS_RW_ATTR(GC_THREAD, f2fs_gc_kthread, gc_idle, gc_idle);
F2FS_RW_ATTR(SM_INFO, f2fs_sm_info, reclaim_segments, rec_prefree_segments);
F2FS_RW_ATTR(DCC_INFO, discard_cmd_control, max_small_discards, max_discards);
F2FS_RW_ATTR(RESERVED_BLOCKS, f2fs_sb_info, reserved_blocks, reserved_blocks);
#ifdef CONFIG_F2FS_OVP_RESERVED
F2FS_RW_ATTR(OVP_RESERVED_BLOCKS, f2fs_sb_info, ovp_reserved_blocks, ovp_reserved_blocks);
#endif
F2FS_RW_ATTR(SM_INFO, f2fs_sm_info, batched_trim_sections, trim_sections);
F2FS_RW_ATTR(SM_INFO, f2fs_sm_info, ipu_policy, ipu_policy);
F2FS_RW_ATTR(SM_INFO, f2fs_sm_info, min_ipu_util, min_ipu_util);
F2FS_RW_ATTR(SM_INFO, f2fs_sm_info, min_fsync_blocks, min_fsync_blocks);
F2FS_RW_ATTR(SM_INFO, f2fs_sm_info, min_hot_blocks, min_hot_blocks);
F2FS_RW_ATTR(NM_INFO, f2fs_nm_info, ram_thresh, ram_thresh);
F2FS_RW_ATTR(NM_INFO, f2fs_nm_info, ra_nid_pages, ra_nid_pages);
F2FS_RW_ATTR(NM_INFO, f2fs_nm_info, dirty_nats_ratio, dirty_nats_ratio);
F2FS_RW_ATTR(F2FS_SBI, f2fs_sb_info, max_victim_search, max_victim_search);
F2FS_RW_ATTR(F2FS_SBI, f2fs_sb_info, dir_level, dir_level);
F2FS_RW_ATTR(F2FS_SBI, f2fs_sb_info, readdir_ra, readdir_ra);
F2FS_RW_ATTR(F2FS_SBI, f2fs_sb_info, cp_interval, interval_time[CP_TIME]);
F2FS_RW_ATTR(F2FS_SBI, f2fs_sb_info, idle_interval, interval_time[REQ_TIME]);
#ifdef CONFIG_F2FS_FAULT_INJECTION
F2FS_RW_ATTR(FAULT_INFO_RATE, f2fs_fault_info, inject_rate, inject_rate);
F2FS_RW_ATTR(FAULT_INFO_TYPE, f2fs_fault_info, inject_type, inject_type);
#endif
F2FS_GENERAL_RO_ATTR(lifetime_write_kbytes);
F2FS_GENERAL_RO_ATTR(current_reserved_blocks);
#ifdef CONFIG_F2FS_OVP_RESERVED
F2FS_GENERAL_RO_ATTR(ovp_current_reserved_blocks);
#endif

#define ATTR_LIST(name) (&f2fs_attr_##name.attr)
static struct attribute *f2fs_attrs[] = {
	ATTR_LIST(gc_min_sleep_time),
	ATTR_LIST(gc_max_sleep_time),
	ATTR_LIST(gc_no_gc_sleep_time),
	ATTR_LIST(gc_idle),
	ATTR_LIST(reclaim_segments),
	ATTR_LIST(max_small_discards),
	ATTR_LIST(batched_trim_sections),
	ATTR_LIST(ipu_policy),
	ATTR_LIST(min_ipu_util),
	ATTR_LIST(min_fsync_blocks),
	ATTR_LIST(min_hot_blocks),
	ATTR_LIST(max_victim_search),
	ATTR_LIST(dir_level),
	ATTR_LIST(readdir_ra),
	ATTR_LIST(ram_thresh),
	ATTR_LIST(ra_nid_pages),
	ATTR_LIST(dirty_nats_ratio),
	ATTR_LIST(cp_interval),
	ATTR_LIST(idle_interval),
#ifdef CONFIG_F2FS_FAULT_INJECTION
	ATTR_LIST(inject_rate),
	ATTR_LIST(inject_type),
#endif
	ATTR_LIST(lifetime_write_kbytes),
	ATTR_LIST(reserved_blocks),
	ATTR_LIST(current_reserved_blocks),
#ifdef CONFIG_F2FS_OVP_RESERVED
	ATTR_LIST(ovp_reserved_blocks),
	ATTR_LIST(ovp_current_reserved_blocks),
#endif
	NULL,
};

static const struct sysfs_ops f2fs_attr_ops = {
	.show	= f2fs_attr_show,
	.store	= f2fs_attr_store,
};

static struct kobj_type f2fs_ktype = {
	.default_attrs	= f2fs_attrs,
	.sysfs_ops	= &f2fs_attr_ops,
	.release	= f2fs_sb_release,
};

static int segment_info_seq_show(struct seq_file *seq, void *offset)
{
	struct super_block *sb = seq->private;
	struct f2fs_sb_info *sbi = F2FS_SB(sb);
	unsigned int total_segs =
			le32_to_cpu(sbi->raw_super->segment_count_main);
	int i;

	seq_puts(seq, "format: segment_type|valid_blocks\n"
		"segment_type(0:HD, 1:WD, 2:CD, 3:HN, 4:WN, 5:CN)\n");

	for (i = 0; i < total_segs; i++) {
		struct seg_entry *se = get_seg_entry(sbi, i);

		if ((i % 10) == 0)
			seq_printf(seq, "%-10d", i);
		seq_printf(seq, "%d|%-3u", se->type,
					get_valid_blocks(sbi, i, false));
		if ((i % 10) == 9 || i == (total_segs - 1))
			seq_putc(seq, '\n');
		else
			seq_putc(seq, ' ');
	}

	return 0;
}

static int segment_bits_seq_show(struct seq_file *seq, void *offset)
{
	struct super_block *sb = seq->private;
	struct f2fs_sb_info *sbi = F2FS_SB(sb);
	unsigned int total_segs =
			le32_to_cpu(sbi->raw_super->segment_count_main);
	int i, j;

	seq_puts(seq, "format: segment_type|valid_blocks|bitmaps\n"
		"segment_type(0:HD, 1:WD, 2:CD, 3:HN, 4:WN, 5:CN)\n");

	for (i = 0; i < total_segs; i++) {
		struct seg_entry *se = get_seg_entry(sbi, i);

		seq_printf(seq, "%-10d", i);
		seq_printf(seq, "%d|%-3u|", se->type,
					get_valid_blocks(sbi, i, false));
		for (j = 0; j < SIT_VBLOCK_MAP_SIZE; j++)
			seq_printf(seq, " %.2x", se->cur_valid_map[j]);
		seq_putc(seq, '\n');
	}
	return 0;
}

#define F2FS_PROC_FILE_DEF(_name)					\
static int _name##_open_fs(struct inode *inode, struct file *file)	\
{									\
	return single_open(file, _name##_seq_show, PDE_DATA(inode));	\
}									\
									\
static const struct file_operations f2fs_seq_##_name##_fops = {		\
	.open = _name##_open_fs,					\
	.read = seq_read,						\
	.llseek = seq_lseek,						\
	.release = single_release,					\
};

F2FS_PROC_FILE_DEF(segment_info);
F2FS_PROC_FILE_DEF(segment_bits);

static int undiscard_info_seq_show(struct seq_file *seq, void *offset)
{
        struct super_block *sb = seq->private;
        struct f2fs_sb_info *sbi = F2FS_SB(sb);
        struct dirty_seglist_info *dirty_i = DIRTY_I(sbi);
        struct sit_info *sit_i = SIT_I(sbi);
        unsigned int total_segs =
                le32_to_cpu(sbi->raw_super->segment_count_main);
        unsigned int total = 0;
        unsigned int i, j;

        if (!f2fs_discard_en(sbi))
                goto out;
        for (i = 0; i < total_segs; i++) {
                struct seg_entry *se = get_seg_entry(sbi, i);
                unsigned int entries = SIT_VBLOCK_MAP_SIZE / sizeof(unsigned long);
                unsigned int max_blocks = sbi->blocks_per_seg;
                unsigned long *ckpt_map = (unsigned long *)se->ckpt_valid_map;
                unsigned long *discard_map = (unsigned long *)se->discard_map;
                unsigned long *dmap = SIT_I(sbi)->tmp_map;
                int start = 0, end = -1;

                mutex_lock(&sit_i->sentry_lock);

                if (se->valid_blocks == max_blocks) {
                        mutex_unlock(&sit_i->sentry_lock);
                        continue;
                }

                if (se->valid_blocks == 0) {
                        mutex_lock(&dirty_i->seglist_lock);
                        if (test_bit((int)i, dirty_i->dirty_segmap[PRE]))
                                total += 512;
                        mutex_unlock(&dirty_i->seglist_lock);
                } else {
                        for (j = 0; j < entries; j++)
                                dmap[j] = ~ckpt_map[j] & ~discard_map[j];
                        while (1) {
                                start = (int)__find_rev_next_bit(dmap, (unsigned long)max_blocks,
                                                                (unsigned long)(end + 1));

                                if ((unsigned int)start >= max_blocks)
                                        break;

                                end = (int)__find_rev_next_zero_bit(dmap, (unsigned long)max_blocks,
                                                                (unsigned long)(start + 1));
                                total += (unsigned int)(end - start);
                        }
                }

                mutex_unlock(&sit_i->sentry_lock);
        }
out:
        seq_printf(seq, "%u\n", total * 4);

        return 0;
}

static int undiscard_info_open_fs(struct inode *inode, struct file *file)
{
        return single_open(file, undiscard_info_seq_show, PDE_DATA(inode));
}

static const struct file_operations f2fs_seq_undiscard_info_fops = {
        .owner = THIS_MODULE,
        .open = undiscard_info_open_fs,
        .read = seq_read,
        .llseek = seq_lseek,
        .release = single_release,
};

#ifdef CONFIG_F2FS_STAT_FS
/* f2fs big-data statistics */
#define F2FS_BD_PROC_DEF(_name)					\
static int f2fs_##_name##_open(struct inode *inode, struct file *file)	\
{									\
	return single_open(file, f2fs_##_name##_show, PDE_DATA(inode));	\
}									\
									\
static const struct file_operations f2fs_##_name##_fops = {		\
	.owner = THIS_MODULE,						\
	.open = f2fs_##_name##_open,					\
	.read = seq_read,						\
	.write = f2fs_##_name##_write,					\
	.llseek = seq_lseek,						\
	.release = single_release,					\
};

static int f2fs_bd_base_info_show(struct seq_file *seq, void *p)
{
	struct super_block *sb = seq->private;
	struct f2fs_sb_info *sbi = F2FS_SB(sb);

	/*
	 * each column indicates: blk_cnt fs_blk_cnt free_seg_cnt
	 * reserved_seg_cnt valid_user_blocks
	 */
	seq_printf(seq, "%llu %llu %u %u %u\n",
		   le64_to_cpu(sbi->raw_super->block_count),
		   le64_to_cpu(sbi->raw_super->block_count) - le32_to_cpu(sbi->raw_super->main_blkaddr),
		   free_segments(sbi), reserved_segments(sbi),
		   valid_user_blocks(sbi));
	return 0;
}

static ssize_t f2fs_bd_base_info_write(struct file *file,
				       const char __user *buf,
				       size_t length, loff_t *ppos)
{
	return length;
}

static int f2fs_bd_discard_info_show(struct seq_file *seq, void *p)
{
	struct super_block *sb = seq->private;
	struct f2fs_sb_info *sbi = F2FS_SB(sb);
	struct f2fs_bigdata_info *bd = F2FS_BD_STAT(sbi);

	struct dirty_seglist_info *dirty_i = DIRTY_I(sbi);
	struct sit_info *sit_i = SIT_I(sbi);
	unsigned int segs = le32_to_cpu(sbi->raw_super->segment_count_main);
	unsigned int entries = SIT_VBLOCK_MAP_SIZE / sizeof(unsigned long);
	unsigned int max_blocks = sbi->blocks_per_seg;
	unsigned int total_blks = 0, undiscard_cnt = 0;
	unsigned int i, j;

	for (i = 0; i < segs; i++) {
		struct seg_entry *se = get_seg_entry(sbi, i);
		/*lint -save -e826*/
		unsigned long *ckpt_map = (unsigned long *)se->ckpt_valid_map;
		unsigned long *discard_map = (unsigned long *)se->discard_map;
		/*lint -restore*/
		unsigned long *dmap = SIT_I(sbi)->tmp_map;
		int start = 0, end = -1;

		mutex_lock(&sit_i->sentry_lock);

		if (se->valid_blocks == max_blocks) {
			mutex_unlock(&sit_i->sentry_lock);
			continue;
		}

		if (se->valid_blocks == 0) {
			mutex_lock(&dirty_i->seglist_lock);
			if (test_bit((int)i, dirty_i->dirty_segmap[PRE])) {
				total_blks += 512;
				undiscard_cnt++;
			}
			mutex_unlock(&dirty_i->seglist_lock);
		} else {
			for (j = 0; j < entries; j++)
				dmap[j] = ~ckpt_map[j] & ~discard_map[j];
			while (1) {
				/*lint -save -e571 -e776*/
				start = (int)__find_rev_next_bit(dmap, (unsigned long)max_blocks,
								 (unsigned long)(end + 1));
				/*lint -restore*/
				/*lint -save -e574 -e737*/
				if ((unsigned int)start >= max_blocks)
					break;
				/*lint -restore*/
				/*lint -save -e571 -e776*/
				end = (int)__find_rev_next_zero_bit(dmap, (unsigned long)max_blocks,
								    (unsigned long)(start + 1));
				/*lint -restore*/
				total_blks += (unsigned int)(end - start);
				undiscard_cnt++;
			}
		}

		mutex_unlock(&sit_i->sentry_lock);
	}

	/*
	 * each colum indicates: discard_cnt discard_blk_cnt undiscard_cnt
	 * undiscard_blk_cnt discard_time max_discard_time
	 */
	bd_mutex_lock(&sbi->bd_mutex);
	bd->undiscard_cnt = undiscard_cnt;
	bd->undiscard_blk_cnt = total_blks;
	seq_printf(seq, "%u %u %u %u %llu %llu\n", bd->discard_cnt,
		   bd->discard_blk_cnt, bd->undiscard_cnt,
		   bd->undiscard_blk_cnt, bd->discard_time,
		   bd->max_discard_time);
	bd_mutex_unlock(&sbi->bd_mutex);
	return 0;
}

static ssize_t f2fs_bd_discard_info_write(struct file *file,
					  const char __user *buf,
					  size_t length, loff_t *ppos)
{
	struct seq_file *seq = file->private_data;
	struct super_block *sb = seq->private;
	struct f2fs_sb_info *sbi = F2FS_SB(sb);
	struct f2fs_bigdata_info *bd = F2FS_BD_STAT(sbi);
	char buffer[3] = {0};

	if (!buf || length > 2)
		return -EINVAL;

	if (copy_from_user(&buffer, buf, length))
		return -EFAULT;

	if (buffer[0] != '0')
		return -EINVAL;

	bd_mutex_lock(&sbi->bd_mutex);
	bd->discard_cnt = 0;
	bd->discard_blk_cnt = 0;
	bd->undiscard_cnt = 0;
	bd->undiscard_blk_cnt = 0;
	bd->discard_time = 0;
	bd->max_discard_time = 0;
	bd_mutex_unlock(&sbi->bd_mutex);

	return length;
}

static int f2fs_bd_cp_info_show(struct seq_file *seq, void *p)
{
	struct super_block *sb = seq->private;
	struct f2fs_sb_info *sbi = F2FS_SB(sb);
	struct f2fs_bigdata_info *bd = F2FS_BD_STAT(sbi);

	/*
	 * each column indicates: cp_cnt cp_succ_cnt cp_time max_cp_time
	 * max_cp_submit_time max_cp_flush_meta_time max_cp_discard_time
	 */
	bd_mutex_lock(&sbi->bd_mutex);
	bd->cp_cnt = sbi->stat_info->cp_count;
	seq_printf(seq, "%u %u %llu %llu %llu %llu %llu\n", bd->cp_cnt,
		   bd->cp_succ_cnt, bd->cp_time, bd->max_cp_time,
		   bd->max_cp_submit_time, bd->max_cp_flush_meta_time,
		   bd->max_cp_discard_time);
	bd_mutex_unlock(&sbi->bd_mutex);
	return 0;
}

static ssize_t f2fs_bd_cp_info_write(struct file *file,
				     const char __user *buf,
				     size_t length, loff_t *ppos)
{
	struct seq_file *seq = file->private_data;
	struct super_block *sb = seq->private;
	struct f2fs_sb_info *sbi = F2FS_SB(sb);
	struct f2fs_bigdata_info *bd = F2FS_BD_STAT(sbi);
	char buffer[3] = {0};

	if (!buf || length > 2)
		return -EINVAL;

	if (copy_from_user(&buffer, buf, length))
		return -EFAULT;

	if (buffer[0] != '0')
		return -EINVAL;

	bd_mutex_lock(&sbi->bd_mutex);
	bd->cp_cnt = 0;
	bd->cp_succ_cnt = 0;
	bd->cp_time = 0;
	bd->max_cp_time = 0;
	bd->max_cp_submit_time = 0;
	bd->max_cp_flush_meta_time = 0;
	bd->max_cp_discard_time = 0;
	bd_mutex_unlock(&sbi->bd_mutex);

	return length;
}

static int f2fs_bd_gc_info_show(struct seq_file *seq, void *p)
{
	struct super_block *sb = seq->private;
	struct f2fs_sb_info *sbi = F2FS_SB(sb);
	struct f2fs_bigdata_info *bd = F2FS_BD_STAT(sbi);

	/*
	 * each column indicates: bggc_cnt bggc_fail_cnt fggc_cnt fggc_fail_cnt
	 * bggc_data_seg_cnt bggc_data_blk_cnt bggc_node_seg_cnt bggc_node_blk_cnt
	 * fggc_data_seg_cnt fggc_data_blk_cnt fggc_node_seg_cnt fggc_node_blk_cnt
	 * node_ssr_cnt data_ssr_cnt node_lfs_cnt data_lfs_cnt data_ipu_cnt
	 * fggc_time
	 */
	bd_mutex_lock(&sbi->bd_mutex);
	seq_printf(seq, "%u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %llu\n",
		   bd->gc_cnt[BG_GC], bd->gc_fail_cnt[BG_GC],
		   bd->gc_cnt[FG_GC], bd->gc_fail_cnt[FG_GC],
		   bd->gc_data_seg_cnt[BG_GC], bd->gc_data_blk_cnt[BG_GC],
		   bd->gc_node_seg_cnt[BG_GC], bd->gc_node_blk_cnt[BG_GC],
		   bd->gc_data_seg_cnt[FG_GC], bd->gc_data_blk_cnt[FG_GC],
		   bd->gc_node_seg_cnt[FG_GC], bd->gc_node_blk_cnt[FG_GC],
		   bd->data_alloc_cnt[SSR], bd->node_alloc_cnt[SSR],
		   bd->data_alloc_cnt[LFS], bd->node_alloc_cnt[LFS],
		   bd->data_ipu_cnt, bd->fggc_time);
	bd_mutex_unlock(&sbi->bd_mutex);
	return 0;
}

static ssize_t f2fs_bd_gc_info_write(struct file *file,
				     const char __user *buf,
				     size_t length, loff_t *ppos)
{
	struct seq_file *seq = file->private_data;
	struct super_block *sb = seq->private;
	struct f2fs_sb_info *sbi = F2FS_SB(sb);
	struct f2fs_bigdata_info *bd = F2FS_BD_STAT(sbi);
	int i;
	char buffer[3] = {0};

	if (!buf || length > 2)
		return -EINVAL;

	if (copy_from_user(&buffer, buf, length))
		return -EFAULT;

	if (buffer[0] != '0')
		return -EINVAL;

	bd_mutex_lock(&sbi->bd_mutex);
	for (i = BG_GC; i <= FG_GC; i++) {
		bd->gc_cnt[i] = 0;
		bd->gc_fail_cnt[i] = 0;
		bd->gc_data_cnt[i] = 0;
		bd->gc_node_cnt[i] = 0;
		bd->gc_data_seg_cnt[i] = 0;
		bd->gc_data_blk_cnt[i] = 0;
		bd->gc_node_seg_cnt[i] = 0;
		bd->gc_node_blk_cnt[i] = 0;
	}
	bd->fggc_time = 0;
	for (i = LFS; i <= SSR; i++) {
		bd->node_alloc_cnt[i] = 0;
		bd->data_alloc_cnt[i] = 0;
	}
	bd->data_ipu_cnt = 0;
	bd_mutex_unlock(&sbi->bd_mutex);

	return length;
}

static int f2fs_bd_fsync_info_show(struct seq_file *seq, void *p)
{
	struct super_block *sb = seq->private;
	struct f2fs_sb_info *sbi = F2FS_SB(sb);
	struct f2fs_bigdata_info *bd = F2FS_BD_STAT(sbi);

	/*
	 * eacho column indicates: fsync_reg_file_cnt fsync_dir_cnt fsync_time
	 * max_fsync_time fsync_wr_file_time max_fsync_wr_file_time
	 * fsync_cp_time max_fsync_cp_time fsync_sync_node_time
	 * max_fsync_sync_node_time fsync_flush_time max_fsync_flush_time
	 */
	bd_mutex_lock(&sbi->bd_mutex);
	seq_printf(seq, "%u %u %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu\n",
		   bd->fsync_reg_file_cnt, bd->fsync_dir_cnt, bd->fsync_time,
		   bd->max_fsync_time, bd->fsync_wr_file_time,
		   bd->max_fsync_wr_file_time, bd->fsync_cp_time,
		   bd->max_fsync_cp_time, bd->fsync_sync_node_time,
		   bd->max_fsync_sync_node_time, bd->fsync_flush_time,
		   bd->max_fsync_flush_time);
	bd_mutex_unlock(&sbi->bd_mutex);
	return 0;
}

static ssize_t f2fs_bd_fsync_info_write(struct file *file,
					const char __user *buf,
					size_t length, loff_t *ppos)
{
	struct seq_file *seq = file->private_data;
	struct super_block *sb = seq->private;
	struct f2fs_sb_info *sbi = F2FS_SB(sb);
	struct f2fs_bigdata_info *bd = F2FS_BD_STAT(sbi);
	char buffer[3] = {0};

	if (!buf || length > 2)
		return -EINVAL;

	if (copy_from_user(&buffer, buf, length))
		return -EFAULT;

	if (buffer[0] != '0')
		return -EINVAL;

	bd_mutex_lock(&sbi->bd_mutex);
	bd->fsync_reg_file_cnt = 0;
	bd->fsync_dir_cnt = 0;
	bd->fsync_time = 0;
	bd->max_fsync_time = 0;
	bd->fsync_cp_time = 0;
	bd->max_fsync_cp_time = 0;
	bd->fsync_wr_file_time = 0;
	bd->max_fsync_wr_file_time = 0;
	bd->fsync_sync_node_time = 0;
	bd->max_fsync_sync_node_time = 0;
	bd->fsync_flush_time = 0;
	bd->max_fsync_flush_time = 0;
	bd_mutex_unlock(&sbi->bd_mutex);

	return length;
}

static int f2fs_bd_hotcold_info_show(struct seq_file *seq, void *p)
{
	struct super_block *sb = seq->private;
	struct f2fs_sb_info *sbi = F2FS_SB(sb);
	struct f2fs_bigdata_info *bd = F2FS_BD_STAT(sbi);

	bd_mutex_lock(&sbi->bd_mutex);
	/*
	 * each colum indicates: hot_data_cnt, warm_data_cnt, cold_data_cnt, hot_node_cnt,
	 * warm_node_cnt, cold_node_cnt, meta_cp_cnt, meta_sit_cnt, meta_nat_cnt, meta_ssa_cnt,
	 * directio_cnt, gc_cold_data_cnt, rewrite_hot_data_cnt, rewrite_warm_data_cnt,
	 * gc_segment_hot_data_cnt, gc_segment_warm_data_cnt, gc_segment_cold_data_cnt,
	 * gc_segment_hot_node_cnt, gc_segment_warm_node_cnt, gc_segment_cold_node_cnt,
	 * gc_block_hot_data_cnt, gc_block_warm_data_cnt, gc_block_cold_data_cnt,
	 * gc_block_hot_node_cnt, gc_block_warm_node_cnt, gc_block_cold_node_cnt
	 */
	seq_printf(seq, "%lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu "
		   "%lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu\n",
		   bd->hotcold_cnt[HC_HOT_DATA], bd->hotcold_cnt[HC_WARM_DATA],
		   bd->hotcold_cnt[HC_COLD_DATA], bd->hotcold_cnt[HC_HOT_NODE],
		   bd->hotcold_cnt[HC_WARM_NODE], bd->hotcold_cnt[HC_COLD_NODE],
		   bd->hotcold_cnt[HC_META], bd->hotcold_cnt[HC_META_SB],
		   bd->hotcold_cnt[HC_META_CP], bd->hotcold_cnt[HC_META_SIT],
		   bd->hotcold_cnt[HC_META_NAT], bd->hotcold_cnt[HC_META_SSA],
		   bd->hotcold_cnt[HC_DIRECTIO], bd->hotcold_cnt[HC_GC_COLD_DATA],
		   bd->hotcold_cnt[HC_REWRITE_HOT_DATA],
		   bd->hotcold_cnt[HC_REWRITE_WARM_DATA],
		   bd->hotcold_gc_seg_cnt[HC_HOT_DATA],
		   bd->hotcold_gc_seg_cnt[HC_WARM_DATA],
		   bd->hotcold_gc_seg_cnt[HC_COLD_DATA],
		   bd->hotcold_gc_seg_cnt[HC_HOT_NODE],
		   bd->hotcold_gc_seg_cnt[HC_WARM_NODE],
		   bd->hotcold_gc_seg_cnt[HC_COLD_NODE],
		   bd->hotcold_gc_blk_cnt[HC_HOT_DATA],
		   bd->hotcold_gc_blk_cnt[HC_WARM_DATA],
		   bd->hotcold_gc_blk_cnt[HC_COLD_DATA],
		   bd->hotcold_gc_blk_cnt[HC_HOT_NODE],
		   bd->hotcold_gc_blk_cnt[HC_WARM_NODE],
		   bd->hotcold_gc_blk_cnt[HC_COLD_NODE]);
	bd_mutex_unlock(&sbi->bd_mutex);
	return 0;
}

static ssize_t f2fs_bd_hotcold_info_write(struct file *file,
					  const char __user *buf,
					  size_t length, loff_t *ppos)
{
	struct seq_file *seq = file->private_data;
	struct super_block *sb = seq->private;
	struct f2fs_sb_info *sbi = F2FS_SB(sb);
	struct f2fs_bigdata_info *bd = F2FS_BD_STAT(sbi);
	char buffer[3] = {0};
	int i;

	if (!buf || length > 2)
		return -EINVAL;

	if (copy_from_user(&buffer, buf, length))
		return -EFAULT;

	if (buffer[0] != '0')
		return -EINVAL;

	bd_mutex_lock(&sbi->bd_mutex);
	for (i = 0; i < NR_HOTCOLD_TYPE; i++)
		bd->hotcold_cnt[i] = 0;
	for (i = 0; i < NR_CURSEG; i++) {
		bd->hotcold_gc_seg_cnt[i] = 0;
		bd->hotcold_gc_blk_cnt[i] = 0;
	}
	bd_mutex_unlock(&sbi->bd_mutex);

	return length;
}

static int f2fs_bd_encrypt_info_show(struct seq_file *seq, void *p)
{
	struct super_block *sb = seq->private;
	struct f2fs_sb_info *sbi = F2FS_SB(sb);
	struct f2fs_bigdata_info *bd = F2FS_BD_STAT(sbi);

	bd_mutex_lock(&sbi->bd_mutex);
	seq_printf(seq, "%x\n", bd->encrypt.encrypt_val);
	bd_mutex_unlock(&sbi->bd_mutex);
	return 0;
}

static ssize_t f2fs_bd_encrypt_info_write(struct file *file,
					  const char __user *buf,
					  size_t length, loff_t *ppos)
{
	struct seq_file *seq = file->private_data;
	struct super_block *sb = seq->private;
	struct f2fs_sb_info *sbi = F2FS_SB(sb);
	struct f2fs_bigdata_info *bd = F2FS_BD_STAT(sbi);
	char buffer[3] = {0};

	if (!buf || length > 2)
		return -EINVAL;

	if (copy_from_user(&buffer, buf, length))
		return -EFAULT;

	if (buffer[0] != '0')
		return -EINVAL;

	bd_mutex_lock(&sbi->bd_mutex);
	bd->encrypt.encrypt_val = 0;
	bd_mutex_unlock(&sbi->bd_mutex);

	return length;
}

F2FS_BD_PROC_DEF(bd_base_info);
F2FS_BD_PROC_DEF(bd_discard_info);
F2FS_BD_PROC_DEF(bd_gc_info);
F2FS_BD_PROC_DEF(bd_cp_info);
F2FS_BD_PROC_DEF(bd_fsync_info);
F2FS_BD_PROC_DEF(bd_hotcold_info);
F2FS_BD_PROC_DEF(bd_encrypt_info);

static void f2fs_build_bd_stat(struct f2fs_sb_info *sbi)
{
	struct super_block *sb = sbi->sb;

	proc_create_data("bd_base_info", S_IRUGO | S_IWUGO, sbi->s_proc,
				&f2fs_bd_base_info_fops, sb);
	proc_create_data("bd_discard_info", S_IRUGO | S_IWUGO, sbi->s_proc,
				&f2fs_bd_discard_info_fops, sb);
	proc_create_data("bd_cp_info", S_IRUGO | S_IWUGO, sbi->s_proc,
				&f2fs_bd_cp_info_fops, sb);
	proc_create_data("bd_gc_info", S_IRUGO | S_IWUGO, sbi->s_proc,
				&f2fs_bd_gc_info_fops, sb);
	proc_create_data("bd_fsync_info", S_IRUGO | S_IWUGO, sbi->s_proc,
				&f2fs_bd_fsync_info_fops, sb);
	proc_create_data("bd_hotcold_info", S_IRUGO | S_IWUGO, sbi->s_proc,
				&f2fs_bd_hotcold_info_fops, sb);
	proc_create_data("bd_encrypt_info", S_IRUGO | S_IWUGO, sbi->s_proc,
				&f2fs_bd_encrypt_info_fops, sb);
}

static void f2fs_destroy_bd_stat(struct f2fs_sb_info *sbi)
{
	remove_proc_entry("bd_base_info", sbi->s_proc);
	remove_proc_entry("bd_discard_info", sbi->s_proc);
	remove_proc_entry("bd_cp_info", sbi->s_proc);
	remove_proc_entry("bd_gc_info", sbi->s_proc);
	remove_proc_entry("bd_fsync_info", sbi->s_proc);
	remove_proc_entry("bd_hotcold_info", sbi->s_proc);
	remove_proc_entry("bd_encrypt_info", sbi->s_proc);

	if (sbi->bd_info) {
		kfree(sbi->bd_info);
		sbi->bd_info = NULL;
	}
}
#else /* !CONFIG_F2FS_STAT_FS */
#define f2fs_build_bd_stat
#define f2fs_destroy_bd_stat
#endif

int __init f2fs_register_sysfs(void)
{
	f2fs_proc_root = proc_mkdir("fs/f2fs", NULL);

	f2fs_kset = kset_create_and_add("f2fs", NULL, fs_kobj);
	if (!f2fs_kset)
		return -ENOMEM;
	return 0;
}

void f2fs_unregister_sysfs(void)
{
	kset_unregister(f2fs_kset);
	remove_proc_entry("fs/f2fs", NULL);
}

int f2fs_init_sysfs(struct f2fs_sb_info *sbi)
{
	struct super_block *sb = sbi->sb;
	int err;

	if (f2fs_proc_root)
		sbi->s_proc = proc_mkdir(sb->s_id, f2fs_proc_root);

	if (sbi->s_proc) {
		proc_create_data("segment_info", S_IRUGO, sbi->s_proc,
				 &f2fs_seq_segment_info_fops, sb);
		proc_create_data("segment_bits", S_IRUGO, sbi->s_proc,
				 &f2fs_seq_segment_bits_fops, sb);
		proc_create_data("undiscard_info", S_IRUGO, sbi->s_proc,
                                &f2fs_seq_undiscard_info_fops, sb);
		f2fs_build_bd_stat(sbi);
	}

	sbi->s_kobj.kset = f2fs_kset;
	init_completion(&sbi->s_kobj_unregister);
	err = kobject_init_and_add(&sbi->s_kobj, &f2fs_ktype, NULL,
							"%s", sb->s_id);
	if (err)
		goto err_out;
	return 0;
err_out:
	if (sbi->s_proc) {
		remove_proc_entry("segment_info", sbi->s_proc);
		remove_proc_entry("segment_bits", sbi->s_proc);
		remove_proc_entry("undiscard_info", sbi->s_proc);
		f2fs_destroy_bd_stat(sbi);
		remove_proc_entry(sb->s_id, f2fs_proc_root);
	}
	return err;
}

void f2fs_exit_sysfs(struct f2fs_sb_info *sbi)
{
	kobject_del(&sbi->s_kobj);
	kobject_put(&sbi->s_kobj);
	wait_for_completion(&sbi->s_kobj_unregister);

	if (sbi->s_proc) {
		remove_proc_entry("segment_info", sbi->s_proc);
		remove_proc_entry("segment_bits", sbi->s_proc);
		remove_proc_entry("undiscard_info", sbi->s_proc);
		f2fs_destroy_bd_stat(sbi);
		remove_proc_entry(sbi->sb->s_id, f2fs_proc_root);
	}
}
