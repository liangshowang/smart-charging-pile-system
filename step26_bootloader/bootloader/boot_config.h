/**
 * boot_config.h — 启动配置接口
 */
#ifndef __BOOT_CONFIG_H__
#define __BOOT_CONFIG_H__

#include "flash_partition.h"

int      boot_config_read(boot_config_t *cfg);
uint32_t boot_config_get_boot_app(void);
int      boot_config_is_valid(void);
int      boot_config_write(const boot_config_t *cfg);
int      boot_config_set_boot_app(uint32_t boot_app);

#endif
