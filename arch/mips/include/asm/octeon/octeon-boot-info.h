/*
 * (C) Copyright 2004, 2012 Cavium, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#ifndef __OCTEON_BOOT_INFO_H__
#define __OCTEON_BOOT_INFO_H__

#include <linux/types.h>
#include <asm/octeon/cvmx-spinlock.h>

struct boot_init_vector {
	/* First stage address - in ram instead of flash */
	uint64_t code_addr;
	/* Setup code for application, NOT application entry point */
	uint32_t app_start_func_addr;
	/* k0 is used for global data - needs to be passed to other cores */
	uint32_t k0_val;
	/* Address of boot info block structure */
	uint64_t boot_info_addr;
	uint32_t flags;         /* flags */
	uint32_t pad;
};

/* similar to bootloader's linux_app_boot_info but without global data */
struct linux_app_boot_info {
#ifdef __BIG_ENDIAN_BITFIELD
	uint32_t labi_signature;
	uint32_t start_core0_addr;
	uint32_t avail_coremask;
	uint32_t pci_console_active;
	uint32_t icache_prefetch_disable;
	uint32_t padding;
	uint64_t InitTLBStart_addr;
	uint32_t start_app_addr;
	uint32_t cur_exception_base;
	uint32_t no_mark_private_data;
	uint32_t compact_flash_common_base_addr;
	uint32_t compact_flash_attribute_base_addr;
	uint32_t led_display_base_addr;
#else
	uint32_t start_core0_addr;
	uint32_t labi_signature;

	uint32_t pci_console_active;
	uint32_t avail_coremask;

	uint32_t padding;
	uint32_t icache_prefetch_disable;

	uint64_t InitTLBStart_addr;

	uint32_t cur_exception_base;
	uint32_t start_app_addr;

	uint32_t compact_flash_common_base_addr;
	uint32_t no_mark_private_data;

	uint32_t led_display_base_addr;
	uint32_t compact_flash_attribute_base_addr;
#endif
};

/*
 * The size of this struct should be a fixed size of 1024 bytes.
 * Additional members should be added towards the end of the strcuture
 * by adjusting the size of padding.
 */
struct cvmx_app_hotplug_info {
	char app_name[256];
	uint32_t coremask;
	uint32_t volatile hotplug_activated_coremask;
	int32_t valid;
	int32_t volatile shutdown_done;
	uint64_t shutdown_callback;
	uint64_t unplug_callback;
	uint64_t cores_added_callback;
	uint64_t cores_removed_callback;
	uint64_t hotplug_start;
	uint64_t data;
	uint32_t volatile hplugged_cores;
	uint32_t shutdown_cores;
	uint32_t app_shutdown;
	uint32_t unplug_cores;
	uint32_t padding[172];
};

struct cvmx_app_hotplug_global {
	uint32_t avail_coremask;
	struct cvmx_app_hotplug_info hotplug_info_array[32];
	uint32_t version;
	cvmx_spinlock_t hotplug_global_lock;
	int32_t app_under_boot;
	int32_t app_under_shutdown;
};
#define CVMX_APP_HOTPLUG_INFO_REGION_SIZE  sizeof(struct cvmx_app_hotplug_global)
#define CVMX_APP_HOTPLUG_INFO_REGION_NAME  "cvmx-app-hotplug-block"

/* If not to copy a lot of bootloader's structures
   here is only offset of requested member */
#define AVAIL_COREMASK_OFFSET_IN_LINUX_APP_BOOT_BLOCK    0x765c

/* hardcoded in bootloader */
#define  LABI_ADDR_IN_BOOTLOADER                         0x700

#define LINUX_APP_BOOT_BLOCK_NAME "linux-app-boot"

#define LABI_SIGNATURE 0xAABBCC01

/*  from uboot-headers/octeon_mem_map.h */
#define EXCEPTION_BASE_INCR     (4 * 1024)
/* Increment size for exception base addresses (4k minimum) */
#define EXCEPTION_BASE_BASE     0
#define BOOTLOADER_PRIV_DATA_BASE       (EXCEPTION_BASE_BASE + 0x800)
#define BOOTLOADER_BOOT_VECTOR          (BOOTLOADER_PRIV_DATA_BASE)

#endif /* __OCTEON_BOOT_INFO_H__ */
