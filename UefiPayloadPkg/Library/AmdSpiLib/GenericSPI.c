/* SPDX-License-Identifier: GPL-2.0-or-later */

#include <Include/Library/DebugLib.h>
#include <Include/PiDxe.h>
#include <Library/BaseMemoryLib/MemLibInternals.h>
#include "GenericSPI.h"

EFI_STATUS spi_claim_bus(CONST struct spi_slave *slave)
{
	CONST struct spi_ctrlr *ctrlr = slave->ctrlr;
	if (ctrlr && ctrlr->claim_bus)
		return ctrlr->claim_bus(slave);
	return EFI_SUCCESS;
}

VOID spi_release_bus(CONST struct spi_slave *slave)
{
	CONST struct spi_ctrlr *ctrlr = slave->ctrlr;
	if (ctrlr && ctrlr->release_bus)
		ctrlr->release_bus(slave);
}

STATIC EFI_STATUS spi_xfer_single_op(CONST struct spi_slave *slave,
			struct spi_op *op)
{
	CONST struct spi_ctrlr *ctrlr = slave->ctrlr;
	EFI_STATUS status;

	if (!ctrlr || !ctrlr->xfer)
		return EFI_DEVICE_ERROR;

	status = ctrlr->xfer(slave, op->dout, op->bytesout, op->din, op->bytesin);
	if (status)
		op->status = SPI_OP_FAILURE;
	else
		op->status = SPI_OP_SUCCESS;

	return status;
}

STATIC EFI_STATUS spi_xfer_vector_default(CONST struct spi_slave *slave,
				struct spi_op vectors[], __SIZE_TYPE__ count)
{
	__SIZE_TYPE__ i;
	EFI_STATUS status;

	for (i = 0; i < count; i++) {
		status = spi_xfer_single_op(slave, &vectors[i]);
		if (status)
			return status;
	}

	return EFI_SUCCESS;
}

EFI_STATUS spi_xfer_vector(CONST struct spi_slave *slave,
		struct spi_op vectors[], __SIZE_TYPE__ count)
{
	CONST struct spi_ctrlr *ctrlr = slave->ctrlr;

	if (ctrlr && ctrlr->xfer_vector)
		return ctrlr->xfer_vector(slave, vectors, count);

	return spi_xfer_vector_default(slave, vectors, count);
}

EFI_STATUS spi_xfer(CONST struct spi_slave *slave, CONST void *dout, __SIZE_TYPE__ bytesout,
	     VOID *din, __SIZE_TYPE__ bytesin)
{
	CONST struct spi_ctrlr *ctrlr = slave->ctrlr;

	if (ctrlr && ctrlr->xfer) {
		return ctrlr->xfer(slave, dout, bytesout, din, bytesin);
	}

	return EFI_DEVICE_ERROR;
}

UINT32 spi_crop_chunk(CONST struct spi_slave *slave, UINT32 cmd_len,
			UINT32 buf_len)
{
	CONST struct spi_ctrlr *ctrlr = slave->ctrlr;
	UINT32 ctrlr_max;
	BOOLEAN deduct_cmd_len;
	BOOLEAN deduct_opcode_len;

	if (!ctrlr)
		return 0;

	deduct_cmd_len = !!(ctrlr->flags & SPI_CNTRLR_DEDUCT_CMD_LEN);
	deduct_opcode_len = !!(ctrlr->flags & SPI_CNTRLR_DEDUCT_OPCODE_LEN);
	ctrlr_max = ctrlr->max_xfer_size;

	/* Assume opcode is always one byte and deduct it from the cmd_len
	   as the hardware has a separate register for the opcode. */
	if (deduct_opcode_len)
		cmd_len--;

	if (deduct_cmd_len && (ctrlr_max > cmd_len))
		ctrlr_max -= cmd_len;

	return MIN(ctrlr_max, buf_len);
}

EFI_STATUS spi_setup_slave(UINT32 bus, UINT32 cs, struct spi_slave *slave)
{
	__SIZE_TYPE__ i;

	InternalMemZeroMem(slave, sizeof(*slave));

	for (i = 0; i < spi_ctrlr_bus_map_count; i++) {
		if ((spi_ctrlr_bus_map[i].bus_start <= bus) &&
		    (spi_ctrlr_bus_map[i].bus_end >= bus)) {
			slave->ctrlr = spi_ctrlr_bus_map[i].ctrlr;
			break;
		}
	}

	if (slave->ctrlr == NULL)
		return EFI_DEVICE_ERROR;

	slave->bus = bus;
	slave->cs = cs;

	if (slave->ctrlr->setup)
		return slave->ctrlr->setup(slave);

	return EFI_SUCCESS;
}
