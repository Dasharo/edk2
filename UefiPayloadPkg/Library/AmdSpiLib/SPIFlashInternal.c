/* SPDX-License-Identifier: GPL-2.0-or-later */

#include <Library/DebugLib.h>
#include <Library/TimerLib.h>
#include <Include/PiDxe.h>
#include <Library/BaseMemoryLib/MemLibInternals.h>
#include "GenericSPI.h"
#include "SPIFlashInternal.h"

STATIC VOID spi_flash_addr(UINT32 addr, UINT8 *cmd)
{
	/* cmd[0] is actual command */
	cmd[1] = addr >> 16;
	cmd[2] = addr >> 8;
	cmd[3] = addr >> 0;
}

STATIC EFI_STATUS do_spi_flash_cmd(CONST struct spi_slave *spi, CONST VOID *dout,
			    __SIZE_TYPE__ bytes_out, VOID *din, __SIZE_TYPE__ bytes_in)
{
	EFI_STATUS status;
	/*
	 * SPI flash requires command-response kind of behavior. Thus, two
	 * separate SPI vectors are required -- first to transmit dout and other
	 * to receive in din. If some specialized SPI flash controllers
	 * (e.g. x86) can perform both command and response together, it should
	 * be handled at SPI flash controller driver level.
	 */
	struct spi_op vectors[] = {
		[0] = { .dout = dout, .bytesout = bytes_out,
			.din = NULL, .bytesin = 0, },
		[1] = { .dout = NULL, .bytesout = 0,
			.din = din, .bytesin = bytes_in },
	};
	__SIZE_TYPE__ count = ARRAY_SIZE(vectors);
	if (!bytes_in)
		count = 1;

	status = spi_claim_bus(spi);
	if (EFI_ERROR(status))
		return status;

	status = spi_xfer_vector(spi, vectors, count);

	spi_release_bus(spi);
	return status;
}

EFI_STATUS spi_flash_cmd(CONST struct spi_slave *spi, UINT8 cmd, VOID *response, __SIZE_TYPE__ len)
{
	EFI_STATUS status = do_spi_flash_cmd(spi, &cmd, sizeof(cmd), response, len);
	if (EFI_ERROR(status))
		DEBUG((DEBUG_BLKIO, "%a SF: Failed to send command %02x: %d\n", __FUNCTION__, cmd, status));

	return status;
}

/* TODO: This code is quite possibly broken and overflowing stacks. Fix ASAP! */
#pragma GCC diagnostic push
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic ignored "-Wstack-usage="
#endif
#pragma GCC diagnostic ignored "-Wvla"
EFI_STATUS spi_flash_cmd_write(CONST struct spi_slave *spi, CONST UINT8 *cmd,
			__SIZE_TYPE__ cmd_len, CONST VOID *data, __SIZE_TYPE__ data_len)
{
	EFI_STATUS status;
	UINT8 buff[cmd_len + data_len];
	InternalMemCopyMem(buff, cmd, cmd_len);
	InternalMemCopyMem(buff + cmd_len, data, data_len);

	status = do_spi_flash_cmd(spi, buff, cmd_len + data_len, NULL, 0);
	if (EFI_ERROR(status)) {
		DEBUG((DEBUG_BLKIO, "%a SF: Failed to send write command (%zu bytes): %d\n",
				__FUNCTION__, data_len, status));
	}

	return status;
}
#pragma GCC diagnostic pop

/* Perform the read operation honoring spi controller fifo size, reissuing
 * the read command until the full request completed. */
EFI_STATUS spi_flash_cmd_read(CONST struct spi_flash *flash, UINT32 offset,
				  __SIZE_TYPE__ len, VOID *buf)
{
	UINT8 cmd[5];
	EFI_STATUS status;
	UINT8 cmd_len;

	cmd_len = 4;
	cmd[0] = CMD_READ_ARRAY_SLOW;

	UINT8 *data = buf;
	while (len) {
		__SIZE_TYPE__ xfer_len = spi_crop_chunk(&flash->spi, cmd_len, len);
		spi_flash_addr(offset, cmd);
		status = do_spi_flash_cmd(&flash->spi, cmd, cmd_len, data, xfer_len);
		if (EFI_ERROR(status)) {
			DEBUG((DEBUG_BLKIO, "%a SF: Failed to send read command %#.2x(%#x, %#zx): %d\n",
			       __FUNCTION__, cmd[0], offset, xfer_len, status));
			return status;
		}
		offset += xfer_len;
		data += xfer_len;
		len -= xfer_len;
	}

	return EFI_SUCCESS;
}

EFI_STATUS spi_flash_cmd_poll_bit(CONST struct spi_flash *flash, unsigned long timeout,
			   UINT8 cmd, UINT8 poll_bit)
{
	CONST struct spi_slave *spi = &flash->spi;
	EFI_STATUS status = EFI_SUCCESS;
	UINT8 spi_sr;

	while (!EFI_ERROR(status)) {
		status = do_spi_flash_cmd(spi, &cmd, 1, &spi_sr, 1);
		if ((spi_sr & poll_bit) == 0)
			return EFI_SUCCESS;

		MicroSecondDelay(10);
	} while (TRUE);

	return status;
}

EFI_STATUS spi_flash_cmd_wait_ready(CONST struct spi_flash *flash,
			unsigned long timeout)
{
	return spi_flash_cmd_poll_bit(flash, timeout,
		CMD_READ_STATUS, STATUS_WIP);
}

EFI_STATUS spi_flash_cmd_erase(CONST struct spi_flash *flash, UINT32 offset, __SIZE_TYPE__ len)
{
	UINT32 start, end, erase_size;
	EFI_STATUS status = EFI_DEVICE_ERROR;
	UINT8 cmd[4];

	erase_size = flash->sector_size;
	if (offset % erase_size || len % erase_size) {
		DEBUG((DEBUG_BLKIO, "%a SF: Erase offset/length not multiple of erase size\n", __FUNCTION__));
		return EFI_DEVICE_ERROR;
	}
	if (len == 0) {
		DEBUG((DEBUG_BLKIO, "%a SF: Erase length cannot be 0\n", __FUNCTION__));
		return EFI_DEVICE_ERROR;
	}

	cmd[0] = flash->erase_cmd;
	start = offset;
	end = start + len;

	while (offset < end) {
		spi_flash_addr(offset, cmd);
		offset += erase_size;

		DEBUG((DEBUG_BLKIO, "%a SF: erase %2x %2x %2x %2x (%x)\n", __FUNCTION__,
			cmd[0], cmd[1], cmd[2], cmd[3], offset));

		status = spi_flash_cmd(&flash->spi, CMD_WRITE_ENABLE, NULL, 0);
		if (EFI_ERROR(status))
			goto out;

		status = spi_flash_cmd_write(&flash->spi, cmd, sizeof(cmd), NULL, 0);
		if (EFI_ERROR(status))
			goto out;

		status = spi_flash_cmd_wait_ready(flash,
				SPI_FLASH_PAGE_ERASE_TIMEOUT_MS);
		if (EFI_ERROR(status))
			goto out;
	}

	DEBUG((DEBUG_BLKIO, "%a SF: Successfully erased %u bytes @ %x\n",
		__FUNCTION__, len, start));

out:
	return status;
}

EFI_STATUS spi_flash_cmd_status(CONST struct spi_flash *flash, UINT8 *reg)
{
	return spi_flash_cmd(&flash->spi, flash->status_cmd, reg, sizeof(*reg));
}

EFI_STATUS spi_flash_cmd_write_page_program(CONST struct spi_flash *flash, UINT32 offset,
				__SIZE_TYPE__ len, CONST VOID *buf)
{
	unsigned long byte_addr;
	unsigned long page_size;
	__SIZE_TYPE__ chunk_len;
	__SIZE_TYPE__ actual;
	EFI_STATUS status = EFI_SUCCESS;
	UINT8 cmd[4];

	page_size = flash->page_size;
	cmd[0] = flash->pp_cmd;

	for (actual = 0; actual < len; actual += chunk_len) {
		byte_addr = offset % page_size;
		chunk_len = MIN(len - actual, page_size - byte_addr);
		chunk_len = spi_crop_chunk(&flash->spi, sizeof(cmd), chunk_len);

		spi_flash_addr(offset, cmd);
		DEBUG((DEBUG_BLKIO, "%a PP: %x => cmd = { 0x%02x 0x%02x%02x%02x } chunk_len = %u\n",
		 	__FUNCTION__, buf + actual, cmd[0], cmd[1], cmd[2], cmd[3],	chunk_len));

		status = spi_flash_cmd(&flash->spi, flash->wren_cmd, NULL, 0);
		if (EFI_ERROR(status)) {
			DEBUG((DEBUG_BLKIO, "%a SF: Enabling Write failed\n", __FUNCTION__));
			goto out;
		}

		status = spi_flash_cmd_write(&flash->spi, cmd, sizeof(cmd),
				buf + actual, chunk_len);
		if (EFI_ERROR(status)) {
			DEBUG((DEBUG_BLKIO, "%a SF: Page Program failed\n", __FUNCTION__));
			goto out;
		}

		status = spi_flash_cmd_wait_ready(flash, SPI_FLASH_PROG_TIMEOUT_MS);
		if (EFI_ERROR(status))
			goto out;

		offset += chunk_len;
	}
	status = EFI_SUCCESS;

out:
	return status;
}

EFI_STATUS spi_flash_read(CONST struct spi_flash *flash, UINT32 offset, __SIZE_TYPE__ len, VOID *buf)
{
	return flash->ops->read(flash, offset, len, buf);
}

EFI_STATUS spi_flash_write(CONST struct spi_flash *flash, UINT32 offset, __SIZE_TYPE__ len,	CONST VOID *buf)
{
	return flash->ops->write(flash, offset, len, buf);
}

EFI_STATUS spi_flash_erase(CONST struct spi_flash *flash, UINT32 offset, __SIZE_TYPE__ len)
{
	return flash->ops->erase(flash, offset, len);
}

EFI_STATUS spi_flash_status(CONST struct spi_flash *flash, UINT8 *reg)
{
	if (flash->ops->status)
		return flash->ops->status(flash, reg);

	return EFI_DEVICE_ERROR;
}

EFI_STATUS spi_flash_vector_helper(CONST struct spi_slave *slave,
	struct spi_op vectors[], __SIZE_TYPE__ count,
	EFI_STATUS (*func)(CONST struct spi_slave *slave, CONST VOID *dout,
		    __SIZE_TYPE__ bytesout, VOID *din, __SIZE_TYPE__ bytesin))
{
	EFI_STATUS status;
	VOID *din;
	__SIZE_TYPE__ bytes_in;

	if (count < 1 || count > 2)
		return -1;

	/* SPI flash commands always have a command first... */
	if (!vectors[0].dout || !vectors[0].bytesout)
		return EFI_DEVICE_ERROR;
	/* And not read any data during the command. */
	if (vectors[0].din || vectors[0].bytesin)
		return EFI_DEVICE_ERROR;

	if (count == 2) {
		/* If response bytes requested ensure the buffer is valid. */
		if (vectors[1].bytesin && !vectors[1].din)
			return EFI_DEVICE_ERROR;
		/* No sends can accompany a receive. */
		if (vectors[1].dout || vectors[1].bytesout)
			return EFI_DEVICE_ERROR;
		din = vectors[1].din;
		bytes_in = vectors[1].bytesin;
	} else {
		din = NULL;
		bytes_in = 0;
	}

	status = func(slave, vectors[0].dout, vectors[0].bytesout, din, bytes_in);

	if (EFI_ERROR(status)) {
		vectors[0].status = SPI_OP_FAILURE;
		if (count == 2)
			vectors[1].status = SPI_OP_FAILURE;
	} else {
		vectors[0].status = SPI_OP_SUCCESS;
		if (count == 2)
			vectors[1].status = SPI_OP_SUCCESS;
	}

	return status;
}

STATIC CONST struct spi_flash_vendor_info *spi_flash_vendors[] = {
	&spi_flash_adesto_vi,
	&spi_flash_winbond_vi,
};

#define IDCODE_LEN 5

STATIC CONST struct spi_flash_part_id *find_part(CONST struct spi_flash_vendor_info *vi,
						UINT16 id[2])
{
	__SIZE_TYPE__ i;
	CONST UINT16 lid[2] = {
		[0] = id[0] & vi->match_id_mask[0],
		[1] = id[1] & vi->match_id_mask[1],
	};

	for (i = 0; i < vi->nr_part_ids; i++) {
		CONST struct spi_flash_part_id *part = &vi->ids[i];

		if (part->id[0] == lid[0] && part->id[1] == lid[1])
			return part;
	}

	return NULL;
}

STATIC EFI_STATUS fill_spi_flash(const struct spi_slave *spi, struct spi_flash *flash,
	CONST struct spi_flash_vendor_info *vi,
	CONST struct spi_flash_part_id *part)
{
	InternalMemCopyMem(&flash->spi, spi, sizeof(*spi));
	flash->vendor = vi->id;
	flash->model = part->id[0];

	flash->page_size = 1U << vi->page_size_shift;
	flash->sector_size = (1U << vi->sector_size_kib_shift) * (1<<10); /* KiB */
	flash->size = flash->sector_size * (1U << part->nr_sectors_shift);
	flash->erase_cmd = vi->desc->erase_cmd;
	flash->status_cmd = vi->desc->status_cmd;
	flash->pp_cmd = vi->desc->pp_cmd;
	flash->wren_cmd = vi->desc->wren_cmd;

	flash->flags.dual_spi = part->fast_read_dual_output_support;

	flash->ops = &vi->desc->ops;
	flash->part = part;

	if (vi->after_probe)
		return vi->after_probe(flash);

	return EFI_SUCCESS;
}

STATIC EFI_STATUS find_match(CONST struct spi_slave *spi, struct spi_flash *flash,
			UINT8 manuf_id, UINT16 id[2])
{
	UINTN i;

	for (i = 0; i < (UINTN)ARRAY_SIZE(spi_flash_vendors); i++) {
		CONST struct spi_flash_vendor_info *vi;
		CONST struct spi_flash_part_id *part;

		vi = spi_flash_vendors[i];

		if (manuf_id != vi->id)
			continue;

		part = find_part(vi, id);

		if (part == NULL)
			continue;

		return fill_spi_flash(spi, flash, vi, part);
	}

	return EFI_NOT_FOUND;
}

STATIC EFI_STATUS spi_flash_generic_probe(CONST struct spi_slave *spi,
				struct spi_flash *flash)
{
	EFI_STATUS status;
	UINT8 idcode[IDCODE_LEN];
	UINT8 manuf_id;
	UINT16 id[2];

	/* Read the ID codes */
	status = spi_flash_cmd(spi, CMD_READ_ID, idcode, sizeof(idcode));
	if (EFI_ERROR(status))
		return EFI_DEVICE_ERROR;

	manuf_id = idcode[0];

  DEBUG((DEBUG_BLKIO, "%a Manufacturer: %02x\n", __FUNCTION__, manuf_id));

	id[0] = (idcode[1] << 8) | idcode[2];
	id[1] = (idcode[3] << 8) | idcode[4];

	return find_match(spi, flash, manuf_id, id);
}

EFI_STATUS spi_flash_probe(UINT32 bus, UINT32 cs, struct spi_flash *flash)
{
	struct spi_slave spi;
	EFI_STATUS status = EFI_DEVICE_ERROR;

	if (spi_setup_slave(bus, cs, &spi)) {
		DEBUG((DEBUG_BLKIO, "SF: Failed to set up slave\n"));
		return EFI_DEVICE_ERROR;
	}

	/* Try special programmer probe if any. */
	if (spi.ctrlr->flash_probe)
		status = spi.ctrlr->flash_probe(&spi, flash);

	/* If flash is not found, try generic spi flash probe. */
	if (EFI_ERROR(status))
		status = spi_flash_generic_probe(&spi, flash);

	/* Give up -- nothing more to try if flash is not found. */
	if (EFI_ERROR(status)) {
		DEBUG((DEBUG_BLKIO, "SF: Unsupported manufacturer!\n"));
		return EFI_DEVICE_ERROR;
	}

	CONST char *mode_string = "";
	if (flash->flags.dual_spi && spi.ctrlr->xfer_dual)
		mode_string = " (Dual SPI mode)";

	DEBUG((DEBUG_BLKIO, 
	       "SF: Detected %02x %04x with sector size 0x%x, total 0x%x%s\n",
		flash->vendor, flash->model, flash->sector_size, flash->size, mode_string));

	return EFI_SUCCESS;
}

CONST struct spi_flash_ops_descriptor spi_flash_pp_0x20_sector_desc = {
	.erase_cmd = 0x20, /* Sector Erase */
	.status_cmd = 0x05, /* Read Status */
	.pp_cmd = 0x02, /* Page Program */
	.wren_cmd = 0x06, /* Write Enable */
	.ops = {
		.read = spi_flash_cmd_read,
		.write = spi_flash_cmd_write_page_program,
		.erase = spi_flash_cmd_erase,
		.status = spi_flash_cmd_status,
	},
};

CONST struct spi_flash_ops_descriptor spi_flash_pp_0xd8_sector_desc = {
	.erase_cmd = 0xd8, /* Sector Erase */
	.status_cmd = 0x05, /* Read Status */
	.pp_cmd = 0x02, /* Page Program */
	.wren_cmd = 0x06, /* Write Enable */
	.ops = {
		.read = spi_flash_cmd_read,
		.write = spi_flash_cmd_write_page_program,
		.erase = spi_flash_cmd_erase,
		.status = spi_flash_cmd_status,
	},
};
