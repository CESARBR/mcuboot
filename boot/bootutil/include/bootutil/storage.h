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

#ifndef STORAGE_H
#define STORAGE_H

enum storage_key {
    STORAGE_KEY_ID		= 0xFFFF,
    STORAGE_KEY_UUID	= 0xFFFE,
    STORAGE_KEY_TOKEN	= 0xFFFD,
    STORAGE_KEY_NET_SETTINGS = 0xFFFA
};

struct net_settings {
        /** Indicate that network settings is configured */
        u8_t    setup;
        /** Put openthread params here */
};
/**
 * @brief Prepare environment to use flash memory.
 *
 * @details Initialise flash device without clearing values.
 *
 * @retval 0 Success
 * @retval -ERRNO errno code if error
 */
int8_t storage_init(void);

/**
 * @brief Delete all stored values.
 *
 * @details Delete info for all known keys for this file system.
 *
 * @retval 0 Success
 * @retval -ERRNO errno code if error
 */
int8_t storage_reset(void);

/**
 * @brief Gets value from NVM.
 *
 * @details Get value of a key from flash memory.
 *
 * @param Int key for the value to be retrieved.
 * @param net_config struct to store read value.
 *
 * @retval 0 for SUCCESS.
 * @retval -EINVAL for Error.
 */

int8_t storage_get(enum storage_key key, struct net_settings *net_config);

/**
 * @brief Save key value on NVM.
 *
 * @details Save on flash memory.
 *
 * @param Int key for the value to be saved.
 * @param net_config struct to save.
 *
 * @retval 0 for SUCCESS.
 * @retval -1 for INVALID KEY.
 */

int8_t storage_set(enum storage_key key, const struct net_settings *net_config);

// Allocating last 2 blocks of storage area
#define MCUBOOT_STORAGE_TOTAL	(FLASH_AREA_STORAGE_OFFSET + \
                                FLASH_AREA_STORAGE_SIZE)

#define MCUBOOT_STORAGE_OFFSET  (MCUBOOT_STORAGE_TOTAL - \
                                ( 2 * FLASH_ERASE_BLOCK_SIZE))

#endif
