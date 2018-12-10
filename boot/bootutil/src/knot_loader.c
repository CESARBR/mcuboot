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
 * This file provides an interface to load images stored on flash.
 * All images stored on flash need to be validated.
 * Use NVS to determine which image will boot.
 */
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
#include <assert.h>
#include <stddef.h>
#include <stdbool.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <os/os_malloc.h>
#include "bootutil/bootutil.h"
#include "bootutil/image.h"
#include "bootutil_priv.h"
#include "bootutil/bootutil_log.h"

#ifdef MCUBOOT_ENC_IMAGES
#include "bootutil/enc_key.h"
#endif
#include <zephyr.h>
#include <nvs/nvs.h>
#include "bootutil/storage.h"

#include "mcuboot_config/mcuboot_config.h"

static struct boot_loader_state boot_data;


#define BOOT_STATUS_TABLES_COUNT \
    (sizeof boot_status_tables / sizeof boot_status_tables[0])

#define BOOT_LOG_SWAP_STATE(area, state)                            \
    BOOT_LOG_INF("%s: magic=%s, copy_done=0x%x, image_ok=0x%x",     \
                 (area),                                            \
                 ((state)->magic == BOOT_MAGIC_GOOD ? "good" :      \
                  (state)->magic == BOOT_MAGIC_UNSET ? "unset" :    \
                  "bad"),                                           \
                 (state)->copy_done,                                \
                 (state)->image_ok)


static struct net_settings rd_settings;
static int img_status[BOOT_NUM_SLOTS];

/**
 * Open flash memory device and read slot image header
 *
 * @param slot                  slot to be read
 * @param out_hdr               On success,the image header
 *
 * @return                      0 on success; BOOT_EFLASH failure.
 */
static int
boot_read_image_header(int slot, struct image_header *out_hdr)
{
    const struct flash_area *fap;
    int area_id;
    int rc;

    area_id = flash_area_id_from_image_slot(slot);
    rc = flash_area_open(area_id, &fap);
    if (rc != 0) {
        rc = BOOT_EFLASH;
        goto done;
    }

    rc = flash_area_read(fap, 0, out_hdr, sizeof (*out_hdr));
    if (rc != 0) {
        rc = BOOT_EFLASH;
        goto done;
    }

    rc = 0;

done:
    flash_area_close(fap);
    return rc;
}

static uint8_t
boot_write_sz(void)
{
    uint8_t elem_sz;
    uint8_t align;

    /* Figure out what size to write update status update as.  The size depends
     * on what the minimum write size is for scratch area, active image slot.
     * We need to use the bigger of those 2 values.
     */
    elem_sz = flash_area_align(boot_data.imgs[0].area);
    align = flash_area_align(boot_data.scratch_area);
    if (align > elem_sz) {
        elem_sz = align;
    }

    return elem_sz;
}
/**
 * Check flash device and determine if the slots are complatible
 *
 * @param                       none
 *
 * @return                      0 on failure (slots incompatible )
 * @return                      1 on success
 */
static int
boot_slots_compatible(void)
{
    size_t num_sectors_0 = boot_img_num_sectors(&boot_data, 0);
    size_t num_sectors_1 = boot_img_num_sectors(&boot_data, 1);
    size_t size_0, size_1;
    size_t i;

    if (num_sectors_0 > BOOT_MAX_IMG_SECTORS || num_sectors_1 > BOOT_MAX_IMG_SECTORS) {
        BOOT_LOG_WRN("Cannot upgrade: more sectors than allowed");
        return 0;
    }

    /* Ensure both image slots have identical sector layouts. */
    if (num_sectors_0 != num_sectors_1) {
        BOOT_LOG_WRN("Cannot upgrade: number of sectors differ between slots");
        return 0;
    }

    for (i = 0; i < num_sectors_0; i++) {
        size_0 = boot_img_sector_size(&boot_data, 0, i);
        size_1 = boot_img_sector_size(&boot_data, 1, i);
        if (size_0 != size_1) {
            BOOT_LOG_WRN("Cannot upgrade: an incompatible sector was found");
            return 0;
        }
    }

    return 1;
}


/**
 * Determines the sector layout of both image slots and the scratch area.
 * This information is necessary for calculating the number of bytes to erase
 * and copy during an image swap.  The information collected during this
 * function is used to populate the boot_data global.
 */
static int
boot_read_sectors(void)
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

    BOOT_WRITE_SZ(&boot_data) = boot_write_sz();

    return 0;
}

/**
 * Validate image hash/signature in a slot.
 */
static int
boot_image_check(struct image_header *hdr, const struct flash_area *fap,
        struct boot_status *bs)
{
    static uint8_t tmpbuf[BOOT_TMPBUF_SZ];
    int rc;

#ifndef MCUBOOT_ENC_IMAGES
    (void)bs;
    (void)rc;
#else
    if (fap->fa_id == FLASH_AREA_IMAGE_1 && hdr->ih_flags & IMAGE_F_ENCRYPTED) {
        rc = boot_enc_load(hdr, fap, bs->enckey[1]);
        if (rc < 0) {
            return BOOT_EBADIMAGE;
        }
        if (rc == 0 && boot_enc_set_key(1, bs->enckey[1])) {
            return BOOT_EBADIMAGE;
        }
    }
#endif

    if (bootutil_img_validate(hdr, fap, tmpbuf, BOOT_TMPBUF_SZ,
                              NULL, 0, NULL)) {
        return BOOT_EBADIMAGE;
    }
    return 0;
}


static inline int
boot_magic_is_erased(uint8_t erased_val, uint32_t magic)
{
    uint8_t i;
    for (i = 0; i < sizeof(magic); i++) {
        if (erased_val != *(((uint8_t *)&magic) + i)) {
            return 0;
        }
    }
    return 1;
}

static int
boot_validate_slot(int slot, struct boot_status *bs)
{
    const struct flash_area *fap;
    struct image_header *hdr;
    int rc;

    rc = flash_area_open(flash_area_id_from_image_slot(slot), &fap);
    if (rc != 0) {
        return BOOT_EFLASH;
    }

    hdr = boot_img_hdr(&boot_data, slot);
    if (boot_magic_is_erased(flash_area_erased_val(fap), hdr->ih_magic) ||
            hdr->ih_flags & IMAGE_F_NON_BOOTABLE) {
        /* No bootable image in slot; continue booting from slot 0. */
            BOOT_LOG_INF("Image in slot %d is not bootable!", slot);
        return -1;
    }

    if ((hdr->ih_magic != IMAGE_MAGIC || boot_image_check(hdr, fap, bs) != 0)) {
        BOOT_LOG_ERR("Image in slot %d is not valid!", slot);
        return -1;
    }

    flash_area_close(fap);

    /* Image in slot  is valid. */
    return 0;
}


#ifndef MCUBOOT_OVERWRITE_ONLY
static inline int
boot_status_init(const struct flash_area *fap, const struct boot_status *bs)
{
    struct boot_swap_state swap_state;
    int rc;

    rc = boot_read_swap_state_by_id(FLASH_AREA_IMAGE_1, &swap_state);
    assert(rc == 0);

    if (swap_state.image_ok == BOOT_FLAG_SET) {
        rc = boot_write_image_ok(fap);
        assert(rc == 0);
    }

    rc = boot_write_swap_size(fap, bs->swap_size);
    assert(rc == 0);

#ifdef MCUBOOT_ENC_IMAGES
    rc = boot_write_enc_key(fap, 0, bs->enckey[0]);
    assert(rc == 0);

    rc = boot_write_enc_key(fap, 1, bs->enckey[1]);
    assert(rc == 0);
#endif

    rc = boot_write_magic(fap);
    assert(rc == 0);

    return 0;
}
#endif


/**
 * Prepares the booting process :This function checks images around in flash as
 * appropriate.Then use NVS settings and tells you what address to boot from.
 *
 * @param rsp                   On success, indicates how booting should occur.
 *
 * @return                      0 on success; nonzero on failure.
 */
int
boot_go(struct boot_rsp *rsp)
{
    size_t slot;
    int rc,i;
    int fa_id;
    int8_t retfs;
    /* The array of slot sectors are defined here (as opposed to file scope) so
     * that they don't get allocated for non-boot-loader apps.  This is
     * necessary because the gcc option "-fdata-sections" doesn't seem to have
     * any effect in older gcc versions (e.g., 4.8.4).
     */
    static boot_sector_t slot0_sectors[BOOT_MAX_IMG_SECTORS];
    static boot_sector_t slot1_sectors[BOOT_MAX_IMG_SECTORS];
    boot_data.imgs[0].sectors = slot0_sectors;
    boot_data.imgs[1].sectors = slot1_sectors;

#ifdef MCUBOOT_ENC_IMAGES
    /* FIXME: remove this after RAM is cleared by sim */
    boot_enc_zeroize();
#endif
    /* Open boot_data image areas for the duration of this call. */
    for (slot= 0; slot < BOOT_NUM_SLOTS; slot++) {
        fa_id = flash_area_id_from_image_slot(slot);
        rc = flash_area_open(fa_id, &BOOT_IMG_AREA(&boot_data, slot));
        assert(rc == 0);
    }

    rc = flash_area_open(FLASH_AREA_IMAGE_SCRATCH,
                         &BOOT_SCRATCH_AREA(&boot_data));
    assert(rc == 0);


    /* Determine the sector layout of the image slots and scratch area. */
    rc = boot_read_sectors();
    if (rc != 0) {
        BOOT_LOG_WRN("Failed reading sectors; BOOT_MAX_IMG_SECTORS=%d - too small?",
                BOOT_MAX_IMG_SECTORS);
        goto out;
    }

    /* Attempt to read image header for all slots. */
    for (i = 0; i < BOOT_NUM_SLOTS; i++)
        img_status[i] = boot_read_image_header(i, boot_img_hdr(&boot_data, i));

    /* Validate header for all slots */
    if ( img_status[0] && img_status[1] )
    {
        BOOT_LOG_ERR("Panic: Found invalid headers");
        rc =   BOOT_EBADIMAGE;
        goto out;
    }
    /* Attempt to validate both slots */
    for (i = 0; i < BOOT_NUM_SLOTS; i++)
        img_status[i] =boot_validate_slot(i, NULL);

    if ( img_status[0] && img_status[1] )
    {
        BOOT_LOG_ERR("Panic: Found invalid data on slots");
        rc =  BOOT_EBADSTATUS;
        goto out;
    }
    /* Attempt to open NVS filesystem */
    retfs = storage_init();
    if (retfs) {
        BOOT_LOG_ERR("Unable to init nvs");
        rc =  BOOT_EBADSTATUS;
        goto out;
    }

#define STORE_NVS

#ifdef STORE_NVS

// Create storage key and put known value on rd_settings structure

    // Try to execute app on slot 1
    u8_t boot_slot =1;
    retfs = storage_get(STORAGE_KEY_NET_SETTINGS, &rd_settings);
    if (retfs) {
        // Storage STORAGE_KEY_NET_SETTINGS  key not found
        BOOT_LOG_ERR("Unable to read nvs");
        // Run app on slot 1
        rd_settings.setup = boot_slot;
        // Create STORAGE_KEY_NET_SETTINGS key on NVS
        retfs = storage_set(STORAGE_KEY_NET_SETTINGS, &rd_settings);
        if (retfs)
            BOOT_LOG_ERR("Unable to store nvs");
    }


#endif

    // Get selected slot for boot
    retfs = storage_get(STORAGE_KEY_NET_SETTINGS, &rd_settings);
    if (retfs) {
        BOOT_LOG_ERR("Unable to read nvs");
        rc =  BOOT_EBADSTATUS;
        goto out2;

    }

    BOOT_LOG_INF("Mcuboot flash offset area: %x", MCUBOOT_STORAGE_OFFSET);
    BOOT_LOG_INF("Boot area stored value = %d", rd_settings.setup);

    // test valid image on slot 0
    if ( (rd_settings.setup == 0) && img_status[0] == 0)
        slot = 0;
    // test valid image on slot 1
    else if ( (rd_settings.setup >0 ) && img_status[1] == 0)
        slot = 1;
    // can't find a valid boot image
    else
    {
        rc = BOOT_EBADIMAGE;
        goto out;
    }
    // test header of selected boot image
    if ( boot_data.imgs[slot].hdr.ih_magic != IMAGE_MAGIC ) {
        rc = BOOT_EBADIMAGE;

    } else if ( img_status[slot] == 0 ) {
        // return rc response ok
        rc = 0;
        // slot is ok so prepare jump
        BOOT_LOG_INF("Slot %d selected",slot);
        rsp->br_flash_dev_id = boot_data.imgs[slot].area->fa_device_id;
        rsp->br_image_off = boot_img_slot_off(&boot_data, slot);
        rsp->br_hdr = boot_img_hdr(&boot_data, slot);
    }
    else
        rc = BOOT_EBADSTATUS;

out:
    // close flash areas
    flash_area_close(BOOT_SCRATCH_AREA(&boot_data));
    for (slot = 0; slot < BOOT_NUM_SLOTS; slot++) {
        flash_area_close(BOOT_IMG_AREA(&boot_data, BOOT_NUM_SLOTS - 1 - slot));
    }

    if ( rc > 0 ) {
        assert(0);
        BOOT_LOG_ERR("Panic: Invalid image data or status %d",rc);
        /* Loop forever... */
        while (1) {}
    }
    return rc;
out2:
    slot =1;
    if ((img_status[slot] == 0) &&
	(boot_data.imgs[slot].hdr.ih_magic == IMAGE_MAGIC)) {
        // return rc response ok
        rc = 0;
        // slot is ok so prepare jump
        BOOT_LOG_INF("Slot %d selected",slot);
        rsp->br_flash_dev_id = boot_data.imgs[slot].area->fa_device_id;
        rsp->br_image_off = boot_img_slot_off(&boot_data, slot);
        rsp->br_hdr = boot_img_hdr(&boot_data, slot);

    } else
        rc = BOOT_EBADSTATUS;

    goto out;
}
