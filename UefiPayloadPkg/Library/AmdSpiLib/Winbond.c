#include <Library/DebugLib.h>
#include "SPIFlashInternal.h"
#include "Winbond.h"

union status_reg1 {
	UINT8 u;
	struct {
		UINT8 busy : 1;
		UINT8 wel  : 1;
		UINT8 bp   : 3;
		UINT8 tb   : 1;
		UINT8 sec  : 1;
		UINT8 srp0 : 1;
	} bp3;
	struct {
		UINT8 busy : 1;
		UINT8 wel  : 1;
		UINT8 bp   : 4;
		UINT8 tb   : 1;
		UINT8 srp0 : 1;
	} bp4;
};

union status_reg2 {
	UINT8 u;
	struct {
		UINT8 srp1 : 1;
		UINT8   qe : 1;
		UINT8  res : 1;
		UINT8   lb : 3;
		UINT8  cmp : 1;
		UINT8  sus : 1;
	};
};

struct status_regs {
	union {
		struct {
#if defined(__BIG_ENDIAN)
			union status_reg2 reg2;
			union status_reg1 reg1;
#else
			union status_reg1 reg1;
			union status_reg2 reg2;
#endif
		};
		UINT16 u;
	};
};

static const struct spi_flash_part_id flash_table[] = {
	{
		/* W25P80 */
		.id[0]				= 0x2014,
		.nr_sectors_shift		= 8,
	},
	{
		/* W25P16 */
		.id[0]				= 0x2015,
		.nr_sectors_shift		= 9,
	},
	{
		/* W25P32 */
		.id[0]				= 0x2016,
		.nr_sectors_shift		= 10,
	},
	{
		/* W25X80 */
		.id[0]				= 0x3014,
		.nr_sectors_shift		= 8,
		.fast_read_dual_output_support	= 1,
	},
	{
		/* W25X16 */
		.id[0]				= 0x3015,
		.nr_sectors_shift		= 9,
		.fast_read_dual_output_support	= 1,
	},
	{
		/* W25X32 */
		.id[0]				= 0x3016,
		.nr_sectors_shift		= 10,
		.fast_read_dual_output_support	= 1,
	},
	{
		/* W25X64 */
		.id[0]				= 0x3017,
		.nr_sectors_shift		= 11,
		.fast_read_dual_output_support	= 1,
	},
	{
		/* W25Q80_V */
		.id[0]				= 0x4014,
		.nr_sectors_shift		= 8,
		.fast_read_dual_output_support	= 1,
	},
	{
		/* W25Q16_V */
		.id[0]				= 0x4015,
		.nr_sectors_shift		= 9,
		.fast_read_dual_output_support	= 1,
		.protection_granularity_shift	= 16,
		.bp_bits			= 3,
	},
	{
		/* W25Q16DW */
		.id[0]				= 0x6015,
		.nr_sectors_shift		= 9,
		.fast_read_dual_output_support	= 1,
		.protection_granularity_shift	= 16,
		.bp_bits			= 3,
	},
	{
		/* W25Q32_V */
		.id[0]				= 0x4016,
		.nr_sectors_shift		= 10,
		.fast_read_dual_output_support	= 1,
		.protection_granularity_shift	= 16,
		.bp_bits			= 3,
	},
	{
		/* W25Q32DW */
		.id[0]				= 0x6016,
		.nr_sectors_shift		= 10,
		.fast_read_dual_output_support	= 1,
		.protection_granularity_shift	= 16,
		.bp_bits			= 3,
	},
	{
		/* W25Q64_V */
		.id[0]				= 0x4017,
		.nr_sectors_shift		= 11,
		.fast_read_dual_output_support	= 1,
		.protection_granularity_shift	= 17,
		.bp_bits			= 3,
	},
	{
		/* W25Q64DW */
		.id[0]				= 0x6017,
		.nr_sectors_shift		= 11,
		.fast_read_dual_output_support	= 1,
		.protection_granularity_shift	= 17,
		.bp_bits			= 3,
	},
	{
		/* W25Q64JW */
		.id[0]				= 0x8017,
		.nr_sectors_shift		= 11,
		.fast_read_dual_output_support	= 1,
		.protection_granularity_shift	= 17,
		.bp_bits			= 3,
	},
	{
		/* W25Q128_V */
		.id[0]				= 0x4018,
		.nr_sectors_shift		= 12,
		.fast_read_dual_output_support	= 1,
		.protection_granularity_shift	= 18,
		.bp_bits			= 3,
	},
	{
		/* W25Q128FW */
		.id[0]				= 0x6018,
		.nr_sectors_shift		= 12,
		.fast_read_dual_output_support	= 1,
		.protection_granularity_shift	= 18,
		.bp_bits			= 3,
	},
	{
		/* W25Q128J */
		.id[0]				= 0x7018,
		.nr_sectors_shift		= 12,
		.fast_read_dual_output_support	= 1,
		.protection_granularity_shift	= 18,
		.bp_bits			= 3,
	},
	{
		/* W25Q128JW */
		.id[0]				= 0x8018,
		.nr_sectors_shift		= 12,
		.fast_read_dual_output_support	= 1,
		.protection_granularity_shift	= 18,
		.bp_bits			= 3,
	},
	{
		/* W25Q256_V */
		.id[0]				= 0x4019,
		.nr_sectors_shift		= 13,
		.fast_read_dual_output_support	= 1,
		.protection_granularity_shift	= 16,
		.bp_bits			= 4,
	},
	{
		/* W25Q256J */
		.id[0]				= 0x7019,
		.nr_sectors_shift		= 13,
		.fast_read_dual_output_support	= 1,
		.protection_granularity_shift	= 16,
		.bp_bits			= 4,
	},
};

CONST struct spi_flash_vendor_info spi_flash_winbond_vi = {
	.id = VENDOR_ID_WINBOND,
	.page_size_shift = 8,
	.sector_size_kib_shift = 2,
	.match_id_mask[0] = 0xffff,
	.ids = flash_table,
	.nr_part_ids = ARRAY_SIZE(flash_table),
	.desc = &spi_flash_pp_0x20_sector_desc,
};
