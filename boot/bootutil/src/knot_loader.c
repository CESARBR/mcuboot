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
/**
 * This file provides an interface to load KNoT BLE configurator on flash .
 *
 */
/*
 * Copyright (c) 2019, CESAR. All rights reserved.
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
#include <assert.h>
#include <stddef.h>
#include <stdbool.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <os/os_malloc.h>
#include <zephyr.h>
#include <nvs/nvs.h>
#include <device.h>
#include <gpio.h>
#include "bootutil/bootutil.h"
#include "bootutil/image.h"
#include "bootutil_priv.h"
#include "bootutil/bootutil_log.h"
#include "bootutil/storage.h"
#include "bootutil/knot_loader.h"
#include "mcuboot_config/mcuboot_config.h"

#ifdef MCUBOOT_ENC_IMAGES
#include "bootutil/enc_key.h"
#endif

/* change this to use another GPIO port */
#ifndef SW0_GPIO_CONTROLLER
#ifdef SW0_GPIO_NAME
#define SW0_GPIO_CONTROLLER SW0_GPIO_NAME
#else
#error SW0_GPIO_NAME or SW0_GPIO_CONTROLLER needs to be set in board.h
#endif
#endif
#define PORT	SW0_GPIO_CONTROLLER

/* change this to use another GPIO pin */
#ifdef SW0_GPIO_PIN
#define PIN     SW0_GPIO_PIN
#else
#error SW0_GPIO_PIN needs to be set in board.h
#endif

/* change to use another GPIO pin interrupt config */
#ifdef SW0_GPIO_FLAGS
#define EDGE    (SW0_GPIO_FLAGS | GPIO_INT_EDGE)
#else
/*
 * If SW0_GPIO_FLAGS not defined used default EDGE value.
 * Change this to use a different interrupt trigger
 */
#define EDGE    (GPIO_INT_EDGE | GPIO_INT_ACTIVE_LOW)
#endif

/* change this to enable pull-up/pull-down */
#ifndef SW0_GPIO_FLAGS
#ifdef SW0_GPIO_PIN_PUD
#define SW0_GPIO_FLAGS SW0_GPIO_PIN_PUD
#else
#define SW0_GPIO_FLAGS 0
#endif
#endif
#define PULL_UP SW0_GPIO_FLAGS

static struct device *gpiob = NULL;

// external structs
extern struct boot_loader_state boot_data;

/**
 * Configure GPIO pin for button 1 of nrf52840_pca10056
 * This button is used to load BLE app on KNoT
 *
 * @return  NULL  = gpio bind error
 *          Not NULL = gpio button 1 configured
 */
struct device * button_one_init (void)
{

    gpiob = device_get_binding(PORT);
	if (!gpiob) {
		BOOT_LOG_INF("Can't bind GPIO port\n");
        return NULL;
	}
    else
    {
	    gpio_pin_configure(gpiob, PIN,
			   GPIO_DIR_IN | GPIO_INT |  PULL_UP | EDGE);
        return gpiob;
    }
}

/**
 * Read status of button one on nrf52840_pca10056
 * This button is used to load BLE app on KNoT
 *
 * @return  0 button pressed
 *          1 button not pressed
 *         -1 GPIO error
 */

int button_one_rd (void)
{
    int val = -1;
    if (gpiob)
    {
        gpio_pin_read(gpiob, PIN, &val);
        return val;
    }
    return val;

}

#ifdef MCUBOOT_USE_FLASH_AREA_GET_SECTORS

static inline int
boot_initialize_area_scratch(struct boot_loader_state *state, int flash_area)
{
    uint32_t num_sectors;
    struct flash_sector *out_sectors;
    size_t *out_num_sectors;
    int rc;

    if ( flash_area ==  FLASH_AREA_IMAGE_SCRATCH)
    {
        num_sectors = 30;
        out_sectors = state->imgs[2].sectors;
        out_num_sectors = &state->imgs[2].num_sectors;

    }
    else
        return -1;

    rc = flash_area_get_sectors(flash_area, &num_sectors, out_sectors);
    if (rc != 0) {
        return rc;
    }
    *out_num_sectors = num_sectors;
    return 0;
}
#endif

static uint8_t
boot_write_sz_scratch(void)
{
    uint8_t elem_sz;
    uint8_t align;

    /* Figure out what size to write update status update as.  The size depends
     * on what the minimum write size is for scratch area, active image slot.
     * We need to use the bigger of those 2 values.
     */
    elem_sz = flash_area_align(boot_data.imgs[0].area);
    align = flash_area_align(boot_data.imgs[2].area);
    if (align > elem_sz) {
        elem_sz = align;
    }

    return elem_sz;
}

/**
 * Determines the sector layout of both image slots and the scratch area.
 * This information is necessary for calculating the number of bytes to erase
 * and copy during an image swap.  The information collected during this
 * function is used to populate the boot_data global.
 */
static int
boot_read_sectors_scratch(void)
{
    int rc;

    rc = boot_initialize_area(&boot_data, FLASH_AREA_IMAGE_0);
    if (rc != 0) {
        return BOOT_EFLASH;
    }

    rc = boot_initialize_area(&boot_data, FLASH_AREA_IMAGE_1);
    if (rc != 0) {
        return BOOT_EFLASH;
    }

    rc = boot_initialize_area_scratch(&boot_data, FLASH_AREA_IMAGE_SCRATCH);
    if (rc != 0) {
        return BOOT_EFLASH;
    }

    BOOT_WRITE_SZ(&boot_data) = boot_write_sz_scratch();

    return 0;
}


/**
 * Prepares the booting process on scratch area.
 *
 * @param rsp                   On success, indicates how booting should occur.
 *
 * @return                      0 on success; nonzero on failure.
 */
int
boot_go_scratch(struct boot_rsp *rsp)
{
    size_t slot;
    int rc;
    int fa_id;

    /* The array of slot sectors are defined here (as opposed to file scope) so
     * that they don't get allocated for non-boot-loader apps.  This is
     * necessary because the gcc option "-fdata-sections" doesn't seem to have
     * any effect in older gcc versions (e.g., 4.8.4).
     */
    static boot_sector_t slot0_sectors[BOOT_MAX_IMG_SECTORS];
    static boot_sector_t slot1_sectors[BOOT_MAX_IMG_SECTORS];
    static boot_sector_t slot2_sectors[BOOT_MAX_IMG_SECTORS];


    boot_data.imgs[0].sectors = slot0_sectors;
    boot_data.imgs[1].sectors = slot1_sectors;
    boot_data.imgs[2].sectors = slot2_sectors;

#ifdef MCUBOOT_ENC_IMAGES
    /* FIXME: remove this after RAM is cleared by sim */
    boot_enc_zeroize();
#endif
 //   rc = flash_area_open(fa_id, &BOOT_IMG_AREA(&boot_data, 2));
 //   assert(rc == 0);
    /* Open boot_data image areas for the duration of this call. */
    for (slot = 0; slot < BOOT_NUM_SLOTS; slot++) {
        fa_id = flash_area_id_from_image_slot(slot);
        rc = flash_area_open(fa_id, &BOOT_IMG_AREA(&boot_data, slot));
        assert(rc == 0);
    }

//   fa_id = flash_area_id_from_image_slot(2);
//    BOOT_LOG_INF("Slot2 id :%d",fa_id);

    /* Determine the sector layout of the image slots and scratch area. */

    rc = boot_read_sectors_scratch();
    if (rc != 0) {
        BOOT_LOG_WRN("Failed reading sectors; BOOT_MAX_IMG_SECTORS=%d - too small?",
                BOOT_MAX_IMG_SECTORS);
        goto out;
    }

    /* Attempt to read an image header from each slot. */
    rc = boot_read_image_headers(false);
    if (rc != 0) {
        goto out;
    }

    slot = 2;
    // check scratch (slot 2) header
    if (boot_data.imgs[slot].hdr.ih_magic == IMAGE_MAGIC) {
        rsp->br_flash_dev_id = boot_data.imgs[slot].area->fa_device_id;
        rsp->br_image_off = boot_img_slot_off(&boot_data, slot);
        rsp->br_hdr = boot_img_hdr(&boot_data, slot);
        rc = 0;
//        rsp->br_flash_dev_id = flash_area_id_from_image_slot(slot);
//        rsp->br_image_off =  0xde000;
//        rsp->br_hdr = boot_img_hdr(&boot_data,slot);
    }
    else
    {
        BOOT_LOG_ERR("bad image magic 0x%lx", (unsigned long)boot_data.imgs[2].hdr.ih_magic);
        rc = BOOT_EBADIMAGE;
    }
/*
    rsp->br_flash_dev_id = boot_data.imgs[2].area->fa_device_id;
    rsp->br_image_off = boot_img_slot_off(&boot_data, 2);
    rsp->br_hdr = boot_img_hdr(&boot_data, slot);
    rc = 0;
*/
 out:
    for (slot = 0; slot < BOOT_NUM_SLOTS; slot++) {
        flash_area_close(BOOT_IMG_AREA(&boot_data, BOOT_NUM_SLOTS - 1 - slot));
    }
    return rc;
}
