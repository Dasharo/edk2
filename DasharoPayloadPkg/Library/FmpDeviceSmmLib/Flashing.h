#ifndef FLASHING_H__
#define FLASHING_H__

/**
  Read current firmware in full and return as newly allocated pool memory.

  @return NULL  On error.
**/
VOID *
EFIAPI
ReadCurrentFirmware (
  VOID
  );

#endif // FLASHING_H__
