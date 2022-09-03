/** @file
  Coreboot PEI module include file.

  Copyright (c) 2014 - 2015, Intel Corporation. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent

  Copyright (c) 2010 The Chromium OS Authors. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-3-Clause
**/

/*
 * This file is part of the libpayload project.
 *
 * Copyright (C) 2008 Advanced Micro Devices, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */


#ifndef _COREBOOT_PEI_H_INCLUDED_
#define _COREBOOT_PEI_H_INCLUDED_

#if defined(_MSC_VER)
#pragma warning( disable : 4200 )
#endif

#define DYN_CBMEM_ALIGN_SIZE (4096)

#define IMD_ENTRY_MAGIC      (~0xC0389481)
#define CBMEM_ENTRY_MAGIC    (~0xC0389479)

struct cbmem_entry {
  UINT32 magic;
  UINT32 start;
  UINT32 size;
  UINT32 id;
};

struct cbmem_root {
  UINT32 max_entries;
  UINT32 num_entries;
  UINT32 locked;
  UINT32 size;
  struct cbmem_entry entries[0];
};

struct imd_entry {
  UINT32 magic;
  UINT32 start_offset;
  UINT32 size;
  UINT32 id;
};

struct imd_root {
  UINT32 max_entries;
  UINT32 num_entries;
  UINT32 flags;
  UINT32 entry_align;
  UINT32 max_offset;
  struct imd_entry entries[0];
};

struct cbuint64 {
  UINT32 lo;
  UINT32 hi;
};

#define CB_HEADER_SIGNATURE 0x4F49424C

struct cb_header {
  UINT32 signature;
  UINT32 header_bytes;
  UINT32 header_checksum;
  UINT32 table_bytes;
  UINT32 table_checksum;
  UINT32 table_entries;
};

struct cb_record {
  UINT32 tag;
  UINT32 size;
};

#define CB_TAG_UNUSED     0x0000
#define CB_TAG_MEMORY     0x0001

struct cb_memory_range {
  struct cbuint64 start;
  struct cbuint64 size;
  UINT32 type;
};

#define CB_MEM_RAM    1
#define CB_MEM_RESERVED     2
#define CB_MEM_ACPI   3
#define CB_MEM_NVS    4
#define CB_MEM_UNUSABLE     5
#define CB_MEM_VENDOR_RSVD  6
#define CB_MEM_TABLE       16

struct cb_memory {
  UINT32 tag;
  UINT32 size;
  struct cb_memory_range map[0];
};

#define CB_TAG_MAINBOARD  0x0003

struct cb_mainboard {
  UINT32 tag;
  UINT32 size;
  UINT8 vendor_idx;
  UINT8 part_number_idx;
  UINT8 strings[0];
};
#define CB_TAG_VERSION  0x0004
#define CB_TAG_EXTRA_VERSION  0x0005
#define CB_TAG_BUILD    0x0006
#define CB_TAG_COMPILE_TIME   0x0007
#define CB_TAG_COMPILE_BY     0x0008
#define CB_TAG_COMPILE_HOST   0x0009
#define CB_TAG_COMPILE_DOMAIN 0x000a
#define CB_TAG_COMPILER       0x000b
#define CB_TAG_LINKER   0x000c
#define CB_TAG_ASSEMBLER      0x000d

struct cb_string {
  UINT32 tag;
  UINT32 size;
  UINT8 string[0];
};

#define CB_TAG_SERIAL   0x000f

struct cb_serial {
  UINT32 tag;
  UINT32 size;
#define CB_SERIAL_TYPE_IO_MAPPED     1
#define CB_SERIAL_TYPE_MEMORY_MAPPED 2
  UINT32 type;
  UINT32 baseaddr;
  UINT32 baud;
  UINT32 regwidth;

  // Crystal or input frequency to the chip containing the UART.
  // Provide the board specific details to allow the payload to
  // initialize the chip containing the UART and make independent
  // decisions as to which dividers to select and their values
  // to eventually arrive at the desired console baud-rate.
  UINT32 input_hertz;

  // UART PCI address: bus, device, function
  // 1 << 31 - Valid bit, PCI UART in use
  // Bus << 20
  // Device << 15
  // Function << 12
  UINT32 uart_pci_addr;
};

#define CB_TAG_CONSOLE       0x00010

struct cb_console {
  UINT32 tag;
  UINT32 size;
  UINT16 type;
};

struct cb_cbmem_ref {
  UINT32 tag;
  UINT32 size;
  UINT64 cbmem_addr;
};

#define CB_TAG_CONSOLE_SERIAL8250 0
#define CB_TAG_CONSOLE_VGA  1 // OBSOLETE
#define CB_TAG_CONSOLE_BTEXT      2 // OBSOLETE
#define CB_TAG_CONSOLE_LOGBUF     3
#define CB_TAG_CONSOLE_SROM       4 // OBSOLETE
#define CB_TAG_CONSOLE_EHCI       5

#define CB_TAG_FORWARD       0x00011

struct cb_forward {
  UINT32 tag;
  UINT32 size;
  UINT64 forward;
};

#define CB_TAG_FRAMEBUFFER  0x0012
struct cb_framebuffer {
  UINT32 tag;
  UINT32 size;

  UINT64 physical_address;
  UINT32 x_resolution;
  UINT32 y_resolution;
  UINT32 bytes_per_line;
  UINT8 bits_per_pixel;
  UINT8 red_mask_pos;
  UINT8 red_mask_size;
  UINT8 green_mask_pos;
  UINT8 green_mask_size;
  UINT8 blue_mask_pos;
  UINT8 blue_mask_size;
  UINT8 reserved_mask_pos;
  UINT8 reserved_mask_size;
};

#define CB_TAG_VDAT     0x0015
struct cb_vdat {
  UINT32 tag;
  UINT32 size;  /* size of the entire entry */
  UINT64 vdat_addr;
  UINT32 vdat_size;
};

#define CB_TAG_TIMESTAMPS     0x0016
#define CB_TAG_CBMEM_CONSOLE  0x0017
struct cbmem_console {
  UINT32    size;
  UINT32    cursor;
  UINT8     body[0];
} __attribute__ ((packed));

#define CB_TAG_MRC_CACHE  0x0018
struct cb_cbmem_tab {
  UINT32 tag;
  UINT32 size;
  UINT64 cbmem_tab;
};

#define CB_TAG_SMMSTOREV2       0x0039
struct cb_smmstorev2 {
	UINT32 tag;
	UINT32 size;
	UINT32 num_blocks;	/* Number of writeable blocks in SMM */
	UINT32 block_size;	/* Size of a block in byte. Default: 64 KiB */
	UINT32 mmap_addr;	/* MMIO address of the store for read only access */
	UINT32 com_buffer;	/* Physical address of the communication buffer */
	UINT32 com_buffer_size;	/* Size of the communication buffer in byte */
	UINT8 apm_cmd;	/* The command byte to write to the APM I/O port */
	UINT8 unused[3];	/* Set to zero */
};

#define CB_TAG_LOGO       0x00a0

struct cb_bootlogo_header {
	UINT64 size;
} __attribute__((packed));

#define CB_TAG_VBOOT_WORKBUF    0x0034

struct cb_cbmem_entry {
  UINT32 tag;
  UINT32 size;

  UINT64 address;
  UINT32 entry_size;
  UINT32 id;
};

/* Recovery reason codes */
enum vb2_nv_recovery {
  /**********************************************************************/
  /**** Uncategorized errors ********************************************/

  /* Recovery not requested. */
  VB2_RECOVERY_NOT_REQUESTED = 0x00,

  /*
   * Recovery requested from legacy utility.  (Prior to the NV storage
   * spec, recovery mode was a single bitfield; this value is reserved so
   * that scripts which wrote 1 to the recovery field are distinguishable
   * from scripts whch use the recovery reasons listed here.
   */
  VB2_RECOVERY_LEGACY = 0x01,

  /* User manually requested recovery via recovery button */
  VB2_RECOVERY_RO_MANUAL = 0x02,



  /**********************************************************************/
  /**** Firmware verification (RO) errors (and some EC stuff???) ********/

  /* Unspecified RW verification error (when none of 0x10-0x1f fit) */
  VB2_RECOVERY_RO_INVALID_RW = 0x03,

  /* S3 resume failed (deprecated) */
  VB2_RECOVERY_DEPRECATED_RO_S3_RESUME = 0x04,

  /* TPM error in read-only firmware (deprecated, see 0x54+) */
  VB2_RECOVERY_DEPRECATED_RO_TPM_ERROR = 0x05,

  /* Shared data error in read-only firmware */
  VB2_RECOVERY_RO_SHARED_DATA = 0x06,

  /* Test error from S3Resume() (deprecated) */
  VB2_RECOVERY_DEPRECATED_RO_TEST_S3 = 0x07,

  /* Test error from LoadFirmwareSetup() (deprecated) */
  VB2_RECOVERY_DEPRECATED_RO_TEST_LFS = 0x08,

  /* Test error from LoadFirmware() (deprecated) */
  VB2_RECOVERY_DEPRECATED_RO_TEST_LF = 0x09,

  /*
   * RW firmware failed signature check (neither RW firmware slot was
   * valid).  Recovery reason is VB2_RECOVERY_DEPRECATED_RW_NOT_DONE +
   * the check value for the slot which came closest to validating; see
   * VBSD_LF_CHECK_* in vboot_struct.h (deprecated).
   */
  VB2_RECOVERY_DEPRECATED_RW_NOT_DONE = 0x10,

  /* Latest tried RW firmware developer flag mismatch */
  VB2_RECOVERY_DEPRECATED_RW_DEV_FLAG_MISMATCH = 0x11,

  /* Latest tried RW firmware recovery flag mismatch */
  VB2_RECOVERY_DEPRECATED_RW_REC_FLAG_MISMATCH = 0x12,

  /* Latest tried RW firmware keyblock verification failed */
  VB2_RECOVERY_FW_KEYBLOCK = 0x13,

  /* Latest tried RW firmware key version too old */
  VB2_RECOVERY_FW_KEY_ROLLBACK = 0x14,

  /* Latest tried RW firmware unable to parse data key */
  VB2_RECOVERY_DEPRECATED_RW_DATA_KEY_PARSE = 0x15,

  /* Latest tried RW firmware preamble verification failed */
  VB2_RECOVERY_FW_PREAMBLE = 0x16,

  /* Latest tried RW firmware version too old */
  VB2_RECOVERY_FW_ROLLBACK = 0x17,

  /* Latest tried RW firmware header valid */
  VB2_RECOVERY_DEPRECATED_FW_HEADER_VALID = 0x18,

  /* Latest tried RW firmware unable to get firmware body */
  VB2_RECOVERY_DEPRECATED_FW_GET_FW_BODY = 0x19,

  /* Latest tried RW firmware hash wrong size */
  VB2_RECOVERY_DEPRECATED_FW_HASH_WRONG_SIZE = 0x1a,

  /* Latest tried RW firmware body verification failed */
  VB2_RECOVERY_FW_BODY = 0x1b,

  /* Latest tried RW firmware valid */
  VB2_RECOVERY_DEPRECATED_FW_VALID = 0x1c,

  /* Latest tried RW firmware RO normal path not supported */
  VB2_RECOVERY_DEPRECATED_FW_NO_RO_NORMAL = 0x1d,

  /*
   * Firmware boot failure outside of verified boot (RAM init, missing
   * SSD, etc.).
   */
  VB2_RECOVERY_RO_FIRMWARE = 0x20,

  /*
   * Recovery mode TPM initialization requires a system reboot.  The
   * system was already in recovery mode for some other reason when this
   * happened.
   */
  VB2_RECOVERY_RO_TPM_REBOOT = 0x21,

  /* EC software sync - other error */
  VB2_RECOVERY_EC_SOFTWARE_SYNC = 0x22,

  /* EC software sync - unable to determine active EC image */
  VB2_RECOVERY_EC_UNKNOWN_IMAGE = 0x23,

  /* EC software sync - error obtaining EC image hash (deprecated) */
  VB2_RECOVERY_DEPRECATED_EC_HASH = 0x24,

  /* EC software sync - error obtaining expected EC image (deprecated) */
  VB2_RECOVERY_DEPRECATED_EC_EXPECTED_IMAGE = 0x25,

  /* EC software sync - error updating EC */
  VB2_RECOVERY_EC_UPDATE = 0x26,

  /* EC software sync - unable to jump to EC-RW */
  VB2_RECOVERY_EC_JUMP_RW = 0x27,

  /* EC software sync - unable to protect / unprotect EC-RW */
  VB2_RECOVERY_EC_PROTECT = 0x28,

  /* EC software sync - error obtaining expected EC hash */
  VB2_RECOVERY_EC_EXPECTED_HASH = 0x29,

  /* EC software sync - expected EC image doesn't match hash (deprc.) */
  VB2_RECOVERY_DEPRECATED_EC_HASH_MISMATCH = 0x2a,

  /* Firmware secure data initialization error */
  VB2_RECOVERY_SECDATA_FIRMWARE_INIT = 0x2b,

  /* GBB header is bad */
  VB2_RECOVERY_GBB_HEADER = 0x2c,

  /* Unable to clear TPM owner */
  VB2_RECOVERY_TPM_CLEAR_OWNER = 0x2d,

  /* Error determining/updating virtual dev switch */
  VB2_RECOVERY_DEV_SWITCH = 0x2e,

  /* Error determining firmware slot */
  VB2_RECOVERY_FW_SLOT = 0x2f,

  /* Error updating auxiliary firmware */
  VB2_RECOVERY_AUXFW_UPDATE = 0x30,

  /*
   * Intel CSE Lite SKU firmware failure; see subcodes defined in coreboot for specific
   * reason.
   */
  VB2_RECOVERY_INTEL_CSE_LITE_SKU = 0x31,

  /* Unspecified/unknown error in read-only firmware */
  VB2_RECOVERY_RO_UNSPECIFIED = 0x3f,



  /**********************************************************************/
  /**** Kernel verification (RW) errors *********************************/

  /*
   * User manually requested recovery by pressing a key at developer
   * warning screen (deprecated)
   */
  VB2_RECOVERY_DEPRECATED_RW_DEV_SCREEN = 0x41,

  /* No OS kernel detected (deprecated, now 0x5b) */
  VB2_RECOVERY_DEPRECATED_RW_NO_OS = 0x42,

  /* OS kernel failed signature check. Since the kernel corrupts itself
     (DMVERROR) on a verity failure, may also indicate corrupt rootfs. */
  VB2_RECOVERY_RW_INVALID_OS = 0x43,

  /* TPM error in rewritable firmware (deprecated, see 0x54+) */
  VB2_RECOVERY_DEPRECATED_RW_TPM_ERROR = 0x44,

  /* RW firmware in dev mode, but dev switch is off (deprecated) */
  VB2_RECOVERY_DEPRECATED_RW_DEV_MISMATCH = 0x45,

  /* Shared data error in rewritable firmware */
  VB2_RECOVERY_RW_SHARED_DATA = 0x46,

  /* Test error from LoadKernel() (deprecated) */
  VB2_RECOVERY_DEPRECATED_RW_TEST_LK = 0x47,

  /* No bootable disk found (deprecated, see 0x5a) */
  VB2_RECOVERY_DEPRECATED_RW_NO_DISK = 0x48,

  /* Rebooting did not correct TPM_E_FAIL or TPM_E_FAILEDSELFTEST  */
  VB2_RECOVERY_TPM_E_FAIL = 0x49,

  /* TPM setup error in read-only firmware */
  VB2_RECOVERY_RO_TPM_S_ERROR = 0x50,

  /* TPM write error in read-only firmware */
  VB2_RECOVERY_RO_TPM_W_ERROR = 0x51,

  /* TPM lock error in read-only firmware */
  VB2_RECOVERY_RO_TPM_L_ERROR = 0x52,

  /* TPM update error in read-only firmware */
  VB2_RECOVERY_RO_TPM_U_ERROR = 0x53,

  /* TPM read error in rewritable firmware */
  VB2_RECOVERY_RW_TPM_R_ERROR = 0x54,

  /* TPM write error in rewritable firmware */
  VB2_RECOVERY_RW_TPM_W_ERROR = 0x55,

  /* TPM lock error in rewritable firmware */
  VB2_RECOVERY_RW_TPM_L_ERROR = 0x56,

  /* EC software sync unable to get EC image hash */
  VB2_RECOVERY_EC_HASH_FAILED = 0x57,

  /* EC software sync invalid image hash size */
  VB2_RECOVERY_EC_HASH_SIZE = 0x58,

  /* Unspecified error while trying to load kernel */
  VB2_RECOVERY_LK_UNSPECIFIED = 0x59,

  /* No bootable storage device in system */
  VB2_RECOVERY_RW_NO_DISK = 0x5a,

  /* No bootable kernel found on disk */
  VB2_RECOVERY_RW_NO_KERNEL = 0x5b,

  /* BCB related error in RW firmware (deprecated) */
  VB2_RECOVERY_DEPRECATED_RW_BCB_ERROR = 0x5c,

  /* Kernel secure data initialization error */
  VB2_RECOVERY_SECDATA_KERNEL_INIT = 0x5d,

  /* Fastboot mode requested in firmware (deprecated) */
  VB2_RECOVERY_DEPRECATED_FW_FASTBOOT = 0x5e,

  /* Recovery hash space lock error in RO firmware */
  VB2_RECOVERY_RO_TPM_REC_HASH_L_ERROR = 0x5f,

  /* Failed to disable the TPM [prior to running untrusted code] */
  VB2_RECOVERY_TPM_DISABLE_FAILED = 0x60,

  /* Verification of altfw payload failed (deprecated) */
  VB2_RECOVERY_ALTFW_HASH_MISMATCH = 0x61,

  /* FWMP secure data initialization error */
  VB2_RECOVERY_SECDATA_FWMP_INIT = 0x62,

  /* Failed to get boot mode from TPM/Cr50 */
  VB2_RECOVERY_CR50_BOOT_MODE = 0x63,

  /* Attempt to escape from NO_BOOT mode was detected */
  VB2_RECOVERY_ESCAPE_NO_BOOT = 0x64,

  /* Unspecified/unknown error in rewritable firmware */
  VB2_RECOVERY_RW_UNSPECIFIED = 0x7f,



  /**********************************************************************/
  /**** OS level (kernel) errors (deprecated) ***************************/

  /*
   * Note: we want to avoid having the kernel touch vboot NVRAM directly
   * in the future, so this whole range is essentially deprecated until
   * further notice.
   */

  /* DM-verity error (deprecated) */
  VB2_RECOVERY_DEPRECATED_KE_DM_VERITY = 0x81,

  /* Unspecified/unknown error in kernel (deprecated) */
  VB2_RECOVERY_DEPRECATED_KE_UNSPECIFIED = 0xbf,



  /**********************************************************************/
  /**** OS level (userspace) errors *************************************/

  /* Recovery mode test from user-mode */
  VB2_RECOVERY_US_TEST = 0xc1,

  /* Recovery requested by user-mode via BCB (deprecated) */
  VB2_RECOVERY_DEPRECATED_BCB_USER_MODE = 0xc2,

  /* Fastboot mode requested by user-mode (deprecated) */
  VB2_RECOVERY_DEPRECATED_US_FASTBOOT = 0xc3,

  /* User requested recovery for training memory and rebooting. */
  VB2_RECOVERY_TRAIN_AND_REBOOT = 0xc4,

  /* Unspecified/unknown error in user-mode */
  VB2_RECOVERY_US_UNSPECIFIED = 0xff,
};

/* MAX_SIZE should not be changed without bumping up DATA_VERSION_MAJOR. */
#define VB2_CONTEXT_MAX_SIZE 384

/* Current version of vb2_shared_data struct */
#define VB2_SHARED_DATA_VERSION_MAJOR 3

#define VB2_SHARED_DATA_MAGIC 0x44533256

struct cb_vboot_workbuf_v2 {
  /* Magic number for struct (VB2_SHARED_DATA_MAGIC) */
  UINT32 magic;

  /* Version of this structure */
  UINT16 struct_version_major;
  UINT16 struct_version_minor;

  /* Public fields are stored in the context object */
  UINT8 ctx[VB2_CONTEXT_MAX_SIZE];

  /* Work buffer length in bytes. */
  UINT32 workbuf_size;

  /*
   * Amount of work buffer used so far.  Verified boot sub-calls use
   * this to know where the unused work area starts.
   */
  UINT32 workbuf_used;

  /* Flags; see enum vb2_shared_data_flags */
  UINT32 flags;

  /*
   * Reason we are in recovery mode this boot (enum vb2_nv_recovery), or
   * 0 if we aren't.
   */
  UINT32 recovery_reason;

  /* Firmware slot used last boot (0=A, 1=B) */
  UINT32 last_fw_slot;

  /* Result of last boot (enum vb2_fw_result) */
  UINT32 last_fw_result;

  /* Firmware slot used this boot */
  UINT32 fw_slot;

  /*
   * Version for this slot (top 16 bits = key, lower 16 bits = firmware).
   */
  UINT32 fw_version;

  /* Version from secdata_firmware (must be <= fw_version to boot). */
  UINT32 fw_version_secdata;

  /*
   * Status flags for this boot; see enum vb2_shared_data_status.  Status
   * is "what we've done"; flags above are "decisions we've made".
   */
  UINT32 status;

  /* Offset from start of this struct to GBB header */
  UINT32 gbb_offset;

  /**********************************************************************
   * Data from kernel verification stage.
   */

  /*
   * Version for the current kernel (top 16 bits = key, lower 16 bits =
   * kernel preamble).
   */
  UINT32 kernel_version;

  /* Version from secdata_kernel (must be <= kernel_version to boot) */
  UINT32 kernel_version_secdata;

  /**********************************************************************
   * Temporary variables used during firmware verification.  These don't
   * really need to persist through to the OS, but there's nowhere else
   * we can put them.
   */

  /* Offset of preamble from start of vblock */
  UINT32 vblock_preamble_offset;

  /*
   * Offset and size of packed data key in work buffer.  Size is 0 if
   * data key is not stored in the work buffer.
   */
  UINT32 data_key_offset;
  UINT32 data_key_size;

  /*
   * Offset and size of firmware preamble in work buffer.  Size is 0 if
   * preamble is not stored in the work buffer.
   */
  UINT32 preamble_offset;
  UINT32 preamble_size;

  /*
   * Offset and size of hash context in work buffer.  Size is 0 if
   * hash context is not stored in the work buffer.
   */
  UINT32 hash_offset;
  UINT32 hash_size;

  /*
   * Current tag we're hashing
   *
   * For new structs, this is the offset of the vb2_signature struct
   * in the work buffer.
   */
  UINT32 hash_tag;

  /* Amount of data we still expect to hash */
  UINT32 hash_remaining_size;

  /**********************************************************************
   * Temporary variables used during kernel verification.  These don't
   * really need to persist through to the OS, but there's nowhere else
   * we can put them.
   */

  /*
   * Formerly a pointer to vboot1 shared data header ("VBSD").  Caller
   * may now export a copy of VBSD via vb2api_export_vbsd().
   */
  UINTN reserved0;

  /*
   * Offset and size of packed kernel key in work buffer.  Size is 0 if
   * subkey is not stored in the work buffer.  Note that kernel key may
   * be inside the firmware preamble.
   */
  UINT32 kernel_key_offset;
  UINT32 kernel_key_size;
} __attribute__((packed));

/* Helpful macros */

#define MEM_RANGE_COUNT(_rec) \
  (((_rec)->size - sizeof(*(_rec))) / sizeof((_rec)->map[0]))

#define MEM_RANGE_PTR(_rec, _idx) \
  (void *)(((UINT8 *) (_rec)) + sizeof(*(_rec)) \
    + (sizeof((_rec)->map[0]) * (_idx)))

#define CB_TAG_TPM_PPI_HANDOFF       0x003a

enum lb_tmp_ppi_tpm_version {
	LB_TPM_VERSION_UNSPEC = 0,
	LB_TPM_VERSION_TPM_VERSION_1_2,
	LB_TPM_VERSION_TPM_VERSION_2,
};

/*
 * Handoff buffer for TPM Physical Presence Interface.
 * * ppi_address   Pointer to PPI buffer shared with ACPI
 *                 The layout of the buffer matches the QEMU virtual memory device
 *                 that is generated by QEMU.
 *                 See files 'hw/i386/acpi-build.c' and 'include/hw/acpi/tpm.h'
 *                 for details.
 * * tpm_version   TPM version: 1 for TPM1.2, 2 for TPM2.0
 * * ppi_version   BCD encoded version of TPM PPI interface
 */
struct cb_tpm_physical_presence {
	UINT32 tag;
	UINT32 size;
	UINT32 ppi_address;	/* Address of ACPI PPI communication buffer */
	UINT8 tpm_version;	/* 1: TPM1.2, 2: TPM2.0 */
	UINT8 ppi_version;	/* BCD encoded */
};

#endif // _COREBOOT_PEI_H_INCLUDED_
