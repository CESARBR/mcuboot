/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#include <zephyr.h>

#ifndef __MCUBOOT_LOGGING_H__
#define __MCUBOOT_LOGGING_H__

#define BOOTUTIL_LOG_LEVEL_OFF     1
#define BOOTUTIL_LOG_LEVEL_ERROR   2
#define BOOTUTIL_LOG_LEVEL_WARNING 3
#define BOOTUTIL_LOG_LEVEL_INFO    4
#define BOOTUTIL_LOG_LEVEL_DEBUG   5

/*
 * When building for targets running Zephyr, delegate to its native
 * logging subsystem.
 */

#define MCUBOOT_LOG_MODULE_DECLARE(domain)	LOG_MODULE_DECLARE(domain)
#define MCUBOOT_LOG_MODULE_REGISTER(domain)	LOG_MODULE_REGISTER(domain)

#define MCUBOOT_LOG_ERR(...) LOG_ERR(__VA_ARGS__)
#define MCUBOOT_LOG_WRN(...) LOG_WRN(__VA_ARGS__)
#define MCUBOOT_LOG_INF(...) LOG_INF(__VA_ARGS__)
#define MCUBOOT_LOG_DBG(...) LOG_DBG(__VA_ARGS__)

#include <logging/log.h>

#if MCUBOOT_LOG_LEVEL >= MCUBOOT_LOG_LEVEL_DEBUG
#define MCUBOOT_LOG_DBG(_fmt, ...)                                      \
    do {                                                                \
        printk("[DBG] " _fmt "\n", ##__VA_ARGS__);                      \
    } while (0)
#else
#define MCUBOOT_LOG_DBG(...) IGNORE(__VA_ARGS__)
#endif

#endif
