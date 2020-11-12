#include <Include/PiDxe.h>
#include <Library/DebugLib.h>
#include <Library/TimerLib.h>
#include "GenericSPI.h"
#include "SPIFlashInternal.h"
#include "Winbond.h"

#define BLOCK_SIZE 0x10000
#define ADDRESS(Lba, Offset) (UINT32)(((BLOCK_SIZE) * (Lba)) + (Offset) + 0x120000)

STATIC struct spi_flash flash;

EFI_STATUS
AmdSpiRead (
  IN        EFI_LBA                              Lba,
  IN        UINTN                                Offset,
  IN        UINTN                                *NumBytes,
  IN        UINT8                                *Buffer
  )
{
  UINT32 address = ADDRESS(Lba, Offset);

  if (*NumBytes == 0 || Buffer == NULL)
    return EFI_INVALID_PARAMETER;

  return spi_flash_read(&flash, address, *NumBytes, Buffer);
}

EFI_STATUS
AmdSpiWrite (
  IN        EFI_LBA                              Lba,
  IN        UINTN                                Offset,
  IN        UINTN                                *NumBytes,
  IN        UINT8                                *Buffer
  )
{
  UINT32 address = ADDRESS(Lba, Offset);

  if (*NumBytes == 0 || Buffer == NULL)
    return EFI_INVALID_PARAMETER;

  return spi_flash_write(&flash, address, *NumBytes, Buffer);
}

/**
  Erase a block using the AMD SPI

  @param Lba    The logical block index to erase.

**/
EFI_STATUS
AmdSpiEraseBlock (
  IN    EFI_LBA     Lba
  )
{
  UINT32 address = ADDRESS(Lba, 0);

  return spi_flash_erase(&flash, address, BLOCK_SIZE);
}

EFI_STATUS
AmdSpiInitialize (VOID)
{
  DEBUG((EFI_D_INFO, "%a\n", __FUNCTION__));

  spi_init();

  return spi_flash_probe(0, 0, &flash);
}
