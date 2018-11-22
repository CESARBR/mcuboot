/*
 * Copyright (c) 2018, CESAR. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include <zephyr.h>
#include <nvs/nvs.h>
#include "storage.h"

/* Mininum required flash parameters */
#define NVS_SECTOR_SIZE         FLASH_ERASE_BLOCK_SIZE
#define NVS_SECTOR_COUNT        2

static struct nvs_fs mcuboot_fs = {
    .sector_size = NVS_SECTOR_SIZE,
    .sector_count = NVS_SECTOR_COUNT,
    .offset = MCUBOOT_STORAGE_OFFSET,
};

int8_t storage_init(void)
{
    return nvs_init(&mcuboot_fs, DT_FLASH_DEV_NAME);
}

int8_t storage_reset(void)
{
    return nvs_delete(&mcuboot_fs, STORAGE_KEY_NET_SETTINGS);
}

int8_t storage_get(u16_t key, struct net_settings *net_config)
{
    ssize_t len =  sizeof(*net_config);
    ssize_t read_len;

    if (key != STORAGE_KEY_NET_SETTINGS)
            return -EINVAL;

    read_len = nvs_read(&mcuboot_fs, key, net_config, len);
    if (read_len != len)
        return -EINVAL;

    /* Successful read */
    return 0;
}

int8_t storage_set(u16_t key, const struct net_settings *net_config)
{
	return nvs_write(&mcuboot_fs, key, (void *) net_config,
			 sizeof(*net_config));
}
