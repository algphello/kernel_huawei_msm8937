#ifndef _HW_AUDIO_KERNEL_H
#define _HW_AUDIO_KERNEL_H

void hw_msm_init_extra(struct snd_soc_codec *codec);
bool hw_machine_init_extra (struct platform_device *pdev, void** priv);
void hw_enable_spk_ext_pa(int gpio);
bool hw_string_is_property_read (char* compatible, char *propname, char* string);

#endif

