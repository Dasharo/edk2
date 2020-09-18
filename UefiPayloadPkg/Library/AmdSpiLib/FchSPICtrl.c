#include <Library/DebugLib.h>
#include <Library/PciLib.h>
#include <Library/TimerLib.h>
#include <Library/BaseLib.h>
#include <Include/PiDxe.h>
#include <Library/IoLib.h>
#include "GenericSPI.h"
#include "SPIFlashInternal.h"

#define GRANULARITY_TEST_4k			0x0000f000		/* bits 15-12 */
#define WORD_TO_DWORD_UPPER(x)	((x << 16) & 0xffff0000)

/* SPI MMIO registers */
#define SPI_REG_OPCODE						0x00
#define SPI_CNTRL0								0x00
#define   SPI_BUSY		           	BIT31
#define SPI_REG_CNTRL01						0x01
#define SPI_REG_CNTRL02						0x02
 #define CNTRL02_FIFO_RESET				(1 << 4)
 #define CNTRL02_EXEC_OPCODE			(1 << 0)
#define SPI_REG_CNTRL03						0x03
 #define CNTRL03_SPIBUSY					(1 << 7)
#define SPI_RESTRICTED_CMD1				0x04
#define SPI_RESTRICTED_CMD2				0x08
#define SPI_REG_FIFO							0x0c
#define SPI_REG_CNTRL11						0x0d
 #define CNTRL11_FIFOPTR_MASK			0x07
#define SPI_EXT_REG_INDX        0x1e
#define SPI_EXT_REG_DATA        0x1f
#define SPI_TX_BYTE_COUNT_IDX   0x05
#define SPI_RX_BYTE_COUNT_IDX   0x06
#define SPI_CMD_CODE							0x45
#define SPI_CMD_TRIGGER						0x47
#define SPI_CMD_TRIGGER_EXECUTE		0x80
#define SPI_TX_BYTE_COUNT					0x48
#define SPI_RX_BYTE_COUNT					0x4b
#define SPI_STATUS								0x4c
#define SPI_DONE_BYTE_COUNT_SHIFT	0
#define SPI_DONE_BYTE_COUNT_MASK	0xff
#define SPI_FIFO_WR_PTR_SHIFT			8
#define SPI_FIFO_WR_PTR_MASK			0x7f
#define SPI_FIFO_RD_PTR_SHIFT			16
#define SPI_FIFO_RD_PTR_MASK			0x7f
#define SPI_BUSY	BIT31
#define SPI_FIFO	0x80
#define SPI_FIFO_LAST_BYTE	0xc7
#define SPI_FIFO_DEPTH	(SPI_FIFO_LAST_BYTE - SPI_FIFO)

#define LPC_DEV		0x14
#define LPC_FUNC	0x03

#define SPIROM_BASE_ADDRESS_REGISTER 0xa0
#define SPI_BASE_ALIGNMENT 0x00000040
#define ALIGN_DOWN(x,a) ((x) & ~((typeof(x))(a)-1UL))

STATIC UINTN spi_base = 0;

STATIC UINTN lpc_get_spibase(VOID)
{
	UINT32 base;
	base = PciRead32(
		PCI_LIB_ADDRESS(0, LPC_DEV, LPC_FUNC, SPIROM_BASE_ADDRESS_REGISTER));
	base = ALIGN_DOWN(base, SPI_BASE_ALIGNMENT);
	return (UINTN)base;
}

STATIC VOID spi_set_base(UINTN base)
{
	spi_base = base;
}

STATIC UINTN spi_get_bar(VOID)
{
	if (spi_base == 0) {
		spi_set_base(lpc_get_spibase());
	}
	return spi_base;
}

STATIC UINT8 spi_read8(UINT8 reg)
{
	return MmioRead8((spi_get_bar() + reg));
}

STATIC VOID spi_write8(UINT8 reg, UINT8 val)
{
	MmioWrite8((spi_get_bar() + reg), val);
}

STATIC VOID
InternalDumpData (
  IN UINT8  *Data,
  IN UINTN  Size
  )
{
  UINTN  Index;
  for (Index = 0; Index < Size; Index++) {
    DEBUG ((DEBUG_BLKIO, "%02x", (UINTN)Data[Index]));
  }
}

STATIC VOID
InternalDumpHex (
  IN UINT8  *Data,
  IN UINTN  Size
  )
{
  UINTN   Index;
  UINTN   Count;
  UINTN   Left;

#define COLUME_SIZE  (16 * 2)

  Count = Size / COLUME_SIZE;
  Left  = Size % COLUME_SIZE;
  for (Index = 0; Index < Count; Index++) {
    DEBUG ((DEBUG_BLKIO, "%04x: ", Index * COLUME_SIZE));
    InternalDumpData (Data + Index * COLUME_SIZE, COLUME_SIZE);
    DEBUG ((DEBUG_BLKIO, "\n"));
  }

  if (Left != 0) {
    DEBUG ((DEBUG_BLKIO, "%04x: ", Index * COLUME_SIZE));
    InternalDumpData (Data + Index * COLUME_SIZE, Left);
    DEBUG ((DEBUG_BLKIO, "\n"));
  }
}

VOID spi_init(VOID)
{
	spi_get_bar();
}

STATIC VOID dump_state(UINT8 phase)
{
	UINT8 dump_size;
	UINT32 addr;

	if (phase == 0)
		DEBUG ((DEBUG_BLKIO, "SPI: Before execute\n"));
	else
		DEBUG ((DEBUG_BLKIO, "SPI: After execute\n"));

	DEBUG ((DEBUG_BLKIO, "Cntrl0: %08x\n", MmioRead32(spi_get_bar() + SPI_CNTRL0)));
	DEBUG ((DEBUG_BLKIO, "Status: %08x\n", MmioRead32(spi_get_bar() + SPI_STATUS)));

	addr = spi_get_bar() + SPI_FIFO;
	if (phase == 0) {
		dump_size = spi_read8(SPI_TX_BYTE_COUNT);
		DEBUG ((DEBUG_BLKIO, "TxByteCount: %02x\n", dump_size));
		DEBUG ((DEBUG_BLKIO, "CmdCode: %02x\n", spi_read8(SPI_CMD_CODE)));
	} else {
		dump_size = spi_read8(SPI_RX_BYTE_COUNT);
		DEBUG ((DEBUG_BLKIO, "RxByteCount: %02x\n", dump_size));
		addr += spi_read8(SPI_TX_BYTE_COUNT);
	}

	if (dump_size > 0)
		InternalDumpHex((VOID *)addr, dump_size);
}

STATIC EFI_STATUS execute_command(void)
{
	dump_state(0);

	spi_write8(SPI_REG_CNTRL02, spi_read8(SPI_REG_CNTRL02) | CNTRL02_EXEC_OPCODE);

	while ((spi_read8(SPI_REG_CNTRL02) & CNTRL02_EXEC_OPCODE) ||
	       (spi_read8(SPI_REG_CNTRL03) & CNTRL03_SPIBUSY));

	dump_state(1);

	return EFI_SUCCESS;
}

STATIC EFI_STATUS spi_ctrlr_xfer(CONST struct spi_slave *slave, CONST VOID *dout,
			__SIZE_TYPE__ bytesout, VOID *din, __SIZE_TYPE__ bytesin)
{
	__SIZE_TYPE__ count;
	UINT8 cmd;
	UINT8 *bufin = din;
	CONST UINT8 *bufout = dout;

	DEBUG((DEBUG_BLKIO,  "%a(%x, %x)\n", __FUNCTION__, bytesout, bytesin));

	/* First byte is cmd which cannot be sent through FIFO */
	cmd = bufout[0];
	bufout++;
	bytesout--;

	/*
	 * Check if this is a write command attempting to transfer more bytes
	 * than the controller can handle.  Iterations for writes are not
	 * supported here because each SPI write command needs to be preceded
	 * and followed by other SPI commands.
	 */
	if (bytesout + bytesin > SPI_FIFO_DEPTH) {
		DEBUG((EFI_D_ERROR, "%a: FCH_SC: Too much to transfer, code error!\n", __FUNCTION__));
		return EFI_DEVICE_ERROR;
	}

	spi_write8(SPI_CMD_CODE, cmd);

	spi_write8(SPI_TX_BYTE_COUNT, bytesout);
	spi_write8(SPI_RX_BYTE_COUNT, bytesin);

	for (count = 0; count < bytesout; count++)
		spi_write8(SPI_FIFO + count, bufout[count]);

	execute_command();

	for (count = 0; count < bytesin; count++)
		bufin[count] = spi_read8(SPI_FIFO + (count + bytesout) % SPI_FIFO_DEPTH);

	return EFI_SUCCESS;
}

STATIC EFI_STATUS xfer_vectors(CONST struct spi_slave *slave,
			struct spi_op vectors[], __SIZE_TYPE__ count)
{
	return spi_flash_vector_helper(slave, vectors, count, spi_ctrlr_xfer);
}

CONST struct spi_ctrlr fch_spi_flash_ctrlr = {
	.xfer = spi_ctrlr_xfer,
	.xfer_vector = xfer_vectors,
	.max_xfer_size = SPI_FIFO_DEPTH,
	.flags = SPI_CNTRLR_DEDUCT_CMD_LEN | SPI_CNTRLR_DEDUCT_OPCODE_LEN,
};

CONST struct spi_ctrlr_buses spi_ctrlr_bus_map[] = {
	{
		.ctrlr = &fch_spi_flash_ctrlr,
		.bus_start = 0,
		.bus_end = 0,
	},
};

CONST __SIZE_TYPE__ spi_ctrlr_bus_map_count = ARRAY_SIZE(spi_ctrlr_bus_map);
