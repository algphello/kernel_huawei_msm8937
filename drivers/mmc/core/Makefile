#
# Makefile for the kernel mmc core.
#

obj-$(CONFIG_MMC)		+= mmc_core.o
mmc_core-y			:= core.o bus.o host.o \
				   mmc.o mmc_ops.o sd.o sd_ops.o \
				   sdio.o sdio_ops.o sdio_bus.o \
				   sdio_cis.o sdio_io.o sdio_irq.o \
				   quirks.o slot-gpio.o rwlog.o
mmc_core-$(CONFIG_MMC_PASSWORDS) += lock.o sysfs.o sd_lock_ops.o
mmc_core-$(CONFIG_DEBUG_FS)	+= debugfs.o
obj-$(CONFIG_MMC_RING_BUFFER)	+= ring_buffer.o
mmc_core-$(CONFIG_HUAWEI_EMMC_DSM)	+= dsm_emmc.o
mmc_core-$(CONFIG_HUAWEI_SDCARD_DSM)	+= dsm_sdcard.o
