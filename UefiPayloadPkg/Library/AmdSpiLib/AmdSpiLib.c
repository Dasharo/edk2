#include <Include/PiDxe.h>
#include <Library/DebugLib.h>
#include <Library/TimerLib.h>
#include "GenericSPI.h"
#include "SPIFlashInternal.h"
#include "Winbond.h"

#define BLOCK_SIZE 0x10000
#define VARIABLE_STORAGE_BLOCKS_OFFSET 6
#define ADDRESS(Lba, Offset) (((BLOCK_SIZE) * ((Lba)+(VARIABLE_STORAGE_BLOCKS_OFFSET))) + (Offset))
#define OFFSET_FROM_PAGE_START(Address) ((Address) % (PAGE_SIZE))
#define REMAINING_SPACE(Offset, MaxSize) ((MaxSize) - (Offset))
#define REMAINING_SPACE_IN_BLOCK(Offset) REMAINING_SPACE((Offset), (BLOCK_SIZE))
#define REMAINING_SPACE_IN_PAGE(Offset) REMAINING_SPACE((Offset), (PAGE_SIZE))

STATIC struct spi_flash flash;

EFI_STATUS
AmdSpiRead (
  IN        EFI_LBA                              Lba,
  IN        UINTN                                Offset,
  IN        UINTN                                *NumBytes,
  IN        UINT8                                *Buffer
  )
{
  UINTN address = ADDRESS(Lba, Offset);

  if (address + *NumBytes > 0x90000 || address < 0x60000)
    return EFI_ACCESS_DENIED;

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
  UINTN address = ADDRESS(Lba, Offset);

  if (address + *NumBytes > 0x90000 || address < 0x60000)
    return EFI_ACCESS_DENIED;

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
  UINTN address = ADDRESS(Lba, 0);

  if (address > 0x90000 || address < 0x60000)
    return EFI_ACCESS_DENIED;

  return spi_flash_erase(&flash, address, BLOCK_SIZE);
}

EFI_STATUS
AmdSpiInitialize (VOID)
{
  DEBUG((EFI_D_INFO, "%a\n", __FUNCTION__));

  spi_init();

  return spi_flash_probe(0, 0, &flash);
}
