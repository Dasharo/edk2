/** @file
Sovereign Boot Wizard implementation.

Copyright (c) 2025, 3mdeb Sp z o.o. All rights reserved.<BR>
SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include "SovereignBootWizard.h"

/**
  Check if it is a leap year.

  @param    Time  The UEFI time to be checked.

  @retval   TRUE  It is a leap year.
  @retval   FALSE It is NOT a leap year.

**/
STATIC
BOOLEAN
IsLeapYear (
  IN EFI_TIME  *Time
  )
{
  if (Time->Year % 4 == 0) {
    if (Time->Year % 100 == 0) {
      if (Time->Year % 400 == 0) {
        return TRUE;
      } else {
        return FALSE;
      }
    } else {
      return TRUE;
    }
  } else {
    return FALSE;
  }
}

/**
  Check if the day in the UEFI time is valid.

  @param    Time    The UEFI time to be checked.

  @retval   TRUE    Valid.
  @retval   FALSE   Invalid.

**/
STATIC
BOOLEAN
IsDayValid (
  IN  EFI_TIME  *Time
  )
{
  STATIC CONST INTN  DayOfMonth[12] = { 31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

  if ((Time->Day < 1) ||
      (Time->Day > DayOfMonth[Time->Month - 1]) ||
      ((Time->Month == 2) && (!IsLeapYear (Time) && (Time->Day > 28)))
      )
  {
    return FALSE;
  }

  return TRUE;
}

/**
  Check if the time zone is valid.
  Valid values are between -1440 and 1440 or 2047 (EFI_UNSPECIFIED_TIMEZONE).

  @param    TimeZone    The time zone to be checked.

  @retval   TRUE    Valid.
  @retval   FALSE   Invalid.

**/
STATIC
BOOLEAN
IsValidTimeZone (
  IN  INT16  TimeZone
  )
{
  return TimeZone == EFI_UNSPECIFIED_TIMEZONE ||
         (TimeZone >= -1440 && TimeZone <= 1440);
}

/**
  Check if the daylight is valid.
  Valid values are:
    0 : Time is not affected.
    1 : Time is affected, and has not been adjusted for daylight savings.
    3 : Time is affected, and has been adjusted for daylight savings.
  All other values are invalid.

  @param    Daylight    The daylight to be checked.

  @retval   TRUE    Valid.
  @retval   FALSE   Invalid.

**/
STATIC
BOOLEAN
IsValidDaylight (
  IN  INT8  Daylight
  )
{
  return Daylight == 0 ||
         Daylight == EFI_TIME_ADJUST_DAYLIGHT ||
         Daylight == (EFI_TIME_ADJUST_DAYLIGHT | EFI_TIME_IN_DAYLIGHT);
}

/**
  Check if the UEFI time is valid.

  @param    Time    The UEFI time to be checked.

  @retval   TRUE    Valid.
  @retval   FALSE   Invalid.

**/
STATIC
BOOLEAN
IsTimeValid (
  IN EFI_TIME  *Time
  )
{
  // Check the input parameters are within the range specified by UEFI
  if ((Time->Year  < 2000)              ||
      (Time->Year   > 2099)              ||
      (Time->Month  < 1)              ||
      (Time->Month  > 12)              ||
      (!IsDayValid (Time))              ||
      (Time->Hour   > 23)              ||
      (Time->Minute > 59)              ||
      (Time->Second > 59)              ||
      (Time->Nanosecond > 999999999)     ||
      (!IsValidTimeZone (Time->TimeZone)) ||
      (!IsValidDaylight (Time->Daylight)))
  {
    return FALSE;
  }

  return TRUE;
}

STATIC VOID
FormatAsn1UtcTime (
  IN     OPENSSL_ASN1_TIME *Time,
  IN OUT CHAR16            *DateBuffer,
  IN     UINTN             DateBufferSize
  )
{
  ASN1_UTC_TIME            *UT;
  BOOLEAN                  HasTimezone;
  BOOLEAN                  IsGMT;
  UINT16                   Year;
  UINT8                    Seconds;
  ASN1_TIMEZONE            *TZ;
  UINTN                    Offset;

  HasTimezone = FALSE;
  IsGMT = FALSE;
  Seconds = 0;
  TZ = NULL;
  Offset = 0;

  UT = Time->Data.UtcTime;
  Year = (UINT16)(UT->Year[0] - '0') * 10 + (UT->Year[1] - '0');

  if (Year > 49) {
    Year += 1900;
  } else {
    Year += 2000;
  }

  if (UT->Seconds[0] >= '0' && UT->Seconds[0] <= '9') {
    Seconds = (UT->Seconds[0] - '0') * 10 + (UT->Seconds[1] - '0');
  }

  if (*(CHAR8 *)((VOID *)UT + Time->Length - 1) == 'Z') {
    IsGMT = TRUE;
  }

  if (!IsGMT) {
    if ((UT->Seconds[0] == '+') || (UT->Seconds[0] == '-')) {
      HasTimezone = TRUE;
      TZ = (ASN1_TIMEZONE *)&UT->Seconds[0];
    }

    if (!HasTimezone) {
      TZ = (ASN1_TIMEZONE *)(UT + 1);
      if ((UINTN)TZ + sizeof(TZ) <= ((UINTN)UT + (UINTN)Time->Length)) {
        if ((TZ->Sign == '+') || (TZ->Sign != '-')) {
          HasTimezone = TRUE;
        }
      }
    }

    if (HasTimezone) {
      if ((UINTN)TZ + sizeof(TZ) > ((UINTN)UT + (UINTN)Time->Length)) {
        HasTimezone = FALSE;
        DEBUG ((DEBUG_INFO, "UTC Time zone out of buffer bounds\n"));
      }
    }
  }

  SetMem (DateBuffer, sizeof (DateBuffer), 0);
  Offset += UnicodeSPrint (
              DateBuffer,
              DateBufferSize,
              L"%04u-%c%c-%c%c %c%c:%c%c:%02u",
              Year, UT->Month[0], UT->Month[1],
              UT->Day[0], UT->Day[1],
              UT->Hour[0], UT->Hour[1],
              UT->Minute[0], UT->Minute[1],
              Seconds);

  if (HasTimezone) {
    UnicodeSPrint (
      &DateBuffer[Offset],
      DateBufferSize - Offset * sizeof (CHAR16),
      L" UTC%c%c%c:%c%c",
      TZ->Sign,
      TZ->Hour[0], TZ->Hour[1],
      TZ->Minute[0], TZ->Minute[1]);
  } else {
    UnicodeSPrint (
      &DateBuffer[Offset],
      DateBufferSize - Offset * sizeof (CHAR16),
      L" GMT");
  }
}

STATIC VOID
FormatAsn1GeneralizedTime (
  IN     OPENSSL_ASN1_TIME *Time,
  IN OUT CHAR16            *DateBuffer,
  IN     UINTN             DateBufferSize
  )
{
  ASN1_GENERALIZED_TIME    *GT;
  CHAR8                    *Ptr;
  BOOLEAN                  HasTimezone;
  BOOLEAN                  IsGMT;
  BOOLEAN                  HasFractionSeconds;
  ASN1_TIMEZONE            *TZ;
  UINTN                    Offset;
  INT32                    Index;
  UINT16                   FractionSeconds;
  UINT8                    FractionSecondsDigits;

  HasTimezone = FALSE;
  HasFractionSeconds = FALSE;
  IsGMT = FALSE;
  FractionSeconds = 0;
  TZ = NULL;
  Offset = 0;
  FractionSecondsDigits = 0;


  GT = Time->Data.GeneralizedTime;

  Ptr = (CHAR8 *)(GT + 1);

  for (Index = sizeof (ASN1_GENERALIZED_TIME); Index < Time->Length; Index++, Ptr++) {
    if (*Ptr == '.') {
      HasFractionSeconds = TRUE;
      while (*Ptr != 'Z' && *Ptr != '+' && *Ptr != '-' && Index < Time->Length) {
        FractionSeconds *= 10;
        FractionSeconds = *Ptr - '0';
        FractionSecondsDigits++;
        Index++;
        Ptr++;
      }
      if (Index >= Time->Length) {
        break;
      }
    }

    if ((*Ptr == '+') || (*Ptr == '-')) {
      HasTimezone = TRUE;
      TZ = (ASN1_TIMEZONE *)Ptr;
    }

    if (*Ptr == 'Z') {
      IsGMT = TRUE;
      break;
    }
  }

  if (HasFractionSeconds && FractionSecondsDigits > 3) {
    DEBUG ((DEBUG_INFO, "Fraction seconds incorrect format\n"));
    FractionSeconds = 0;
  }

  if (HasTimezone) {
    if ((UINTN)TZ + sizeof(TZ) <= ((UINTN)GT + (UINTN)Time->Length)) {
      if ((TZ->Sign != '+') && (TZ->Sign != '-')) {
        HasTimezone = FALSE;
        DEBUG ((DEBUG_INFO, "Generalized Time zone incorrect format\n"));
      }
    } else {
      HasTimezone = FALSE;
      DEBUG ((DEBUG_INFO, "Generalized Time zone out of buffer bounds\n"));
    }
  }

  SetMem (DateBuffer, sizeof (DateBuffer), 0);
  Offset += UnicodeSPrint (
              DateBuffer,
              DateBufferSize,
              L"%c%c%c%c-%c%c-%c%c %c%c:%c%c:%c%c",
              GT->Year[0], GT->Year[1], GT->Year[2], GT->Year[3],
              GT->Month[0], GT->Month[1],
              GT->Day[0], GT->Day[1],
              GT->Hour[0], GT->Hour[1],
              GT->Minute[0], GT->Minute[1],
              GT->Seconds[0], GT->Seconds[1]);

  if (HasFractionSeconds && (FractionSeconds != 0)) {
    if (FractionSecondsDigits < 3) {
      while (FractionSecondsDigits > 0) {
        FractionSeconds *= 10;
        FractionSecondsDigits--;
      }

    }
    Offset += UnicodeSPrint (
            &DateBuffer[Offset],
            DateBufferSize - Offset * sizeof (CHAR16),
            L".%04u",
            FractionSeconds);
  }

  if (HasTimezone) {
    UnicodeSPrint (
      &DateBuffer[Offset],
      DateBufferSize - Offset * sizeof (CHAR16),
      L" UTC%c%c%c:%c%c",
      TZ->Sign,
      TZ->Hour[0], TZ->Hour[1],
      TZ->Minute[0], TZ->Minute[1]);
  } else if (IsGMT) {
    UnicodeSPrint (
      &DateBuffer[Offset],
      DateBufferSize - Offset * sizeof (CHAR16),
      L" GMT");
  } else {
    UnicodeSPrint (
      &DateBuffer[Offset],
      DateBufferSize - Offset * sizeof (CHAR16),
      L" (local)");
  }
}

VOID
FormatAsn1Time (
  IN     OPENSSL_ASN1_TIME *Time,
  IN OUT CHAR16            *DateBuffer,
  IN     UINTN             DateBufferSize
  )
{
  if ((Time->Type != ASN1_TYPE_UTC_TIME) &&
      (Time->Type != ASN1_TYPE_GENERALIZED_TIME)) {
    StrCpyS (DateBuffer, DateBufferSize, L"Invalid time format");
    return;
  }

  if (Time->Type == ASN1_TYPE_UTC_TIME) {
    FormatAsn1UtcTime (Time, DateBuffer, DateBufferSize);
  }

  if (Time->Type == ASN1_TYPE_GENERALIZED_TIME) {
    FormatAsn1GeneralizedTime (Time, DateBuffer, DateBufferSize);
  }
}


STATIC BOOLEAN
Asn1UtcTimeToEfiTime (
  IN     OPENSSL_ASN1_TIME *Time,
  IN OUT EFI_TIME          *EfiTime
  )
{
  ASN1_UTC_TIME            *UT;
  BOOLEAN                  HasTimezone;
  BOOLEAN                  IsGMT;
  UINT16                   Year;
  UINT8                    Seconds;
  ASN1_TIMEZONE            *TZ;

  HasTimezone = FALSE;
  IsGMT = FALSE;
  Seconds = 0;
  TZ = NULL;

  UT = Time->Data.UtcTime;
  Year = (UINT16)(UT->Year[0] - '0') * 10 + (UT->Year[1] - '0');

  if (Year > 49) {
    Year += 1900;
  } else {
    Year += 2000;
  }

  if (UT->Seconds[0] >= '0' && UT->Seconds[0] <= '9') {
    Seconds = (UT->Seconds[0] - '0') * 10 + (UT->Seconds[1] - '0');
  }

  if (*(CHAR8 *)((VOID *)UT + Time->Length - 1) == 'Z') {
    IsGMT = TRUE;
  }

  if (!IsGMT) {
    if ((UT->Seconds[0] == '+') || (UT->Seconds[0] == '-')) {
      HasTimezone = TRUE;
      TZ = (ASN1_TIMEZONE *)&UT->Seconds[0];
    }

    if (!HasTimezone) {
      TZ = (ASN1_TIMEZONE *)(UT + 1);
      if ((UINTN)TZ + sizeof(TZ) <= ((UINTN)UT + (UINTN)Time->Length)) {
        if ((TZ->Sign == '+') || (TZ->Sign != '-')) {
          HasTimezone = TRUE;
        }
      }
    }

    if (HasTimezone) {
      if ((UINTN)TZ + sizeof(TZ) > ((UINTN)UT + (UINTN)Time->Length)) {
        HasTimezone = FALSE;
        DEBUG ((DEBUG_INFO, "UTC Time zone out of buffer bounds\n"));
      }
    }
  }

  EfiTime->Year   = Year;
  EfiTime->Month  = (UT->Month[0] - '0') * 10 + (UT->Month[1] - '0');
  EfiTime->Day    = (UT->Day[0] - '0') * 10 + (UT->Day[1] - '0');
  EfiTime->Hour   = (UT->Hour[0] - '0') * 10 + (UT->Hour[1] - '0');
  EfiTime->Minute = (UT->Minute[0] - '0') * 10 + (UT->Minute[1] - '0');
  EfiTime->Second = Seconds;

  if (HasTimezone) {
    EfiTime->TimeZone = (TZ->Hour[0] - '0') * 1000 + (TZ->Hour[1] - '0') * 100;
    EfiTime->TimeZone += (TZ->Minute[0] - '0') * 10 + (TZ->Minute[1] - '0');
    if (TZ->Sign == '-') {
      EfiTime->TimeZone = -EfiTime->TimeZone;
    }
  } else if (IsGMT) {
    EfiTime->TimeZone = 0;
  } else {
    EfiTime->TimeZone = EFI_UNSPECIFIED_TIMEZONE;
  }

  return IsTimeValid (EfiTime);
}

STATIC BOOLEAN
Asn1GeneralizedTimeToEfiTime (
  IN     OPENSSL_ASN1_TIME *Time,
  IN OUT EFI_TIME          *EfiTime
  )
{
  ASN1_GENERALIZED_TIME    *GT;
  CHAR8                    *Ptr;
  BOOLEAN                  HasTimezone;
  BOOLEAN                  IsGMT;
  BOOLEAN                  HasFractionSeconds;
  ASN1_TIMEZONE            *TZ;
  INT32                    Index;
  UINT32                   FractionSeconds;
  UINT8                    FractionSecondsDigits;

  HasTimezone = FALSE;
  HasFractionSeconds = FALSE;
  IsGMT = FALSE;
  FractionSeconds = 0;
  TZ = NULL;
  FractionSecondsDigits = 0;

  SetMem (EfiTime, sizeof (EFI_TIME), 0);

  GT = Time->Data.GeneralizedTime;

  Ptr = (CHAR8 *)(GT + 1);

  for (Index = sizeof (ASN1_GENERALIZED_TIME); Index < Time->Length; Index++, Ptr++) {
    if (*Ptr == '.') {
      HasFractionSeconds = TRUE;
      while (*Ptr != 'Z' && *Ptr != '+' && *Ptr != '-' && Index < Time->Length) {
        FractionSeconds *= 10;
        FractionSeconds = *Ptr - '0';
        FractionSecondsDigits++;
        Index++;
        Ptr++;
      }
      if (Index >= Time->Length) {
        break;
      }
    }

    if ((*Ptr == '+') || (*Ptr == '-')) {
      HasTimezone = TRUE;
      TZ = (ASN1_TIMEZONE *)Ptr;
    }

    if (*Ptr == 'Z') {
      IsGMT = TRUE;
      break;
    }
  }

  if (HasFractionSeconds && FractionSecondsDigits > 3) {
    DEBUG ((DEBUG_INFO, "Fraction seconds incorrect format\n"));
    FractionSeconds = 0;
  }

  if (HasFractionSeconds && (FractionSeconds != 0)) {
    if (FractionSecondsDigits < 3) {
      while (FractionSecondsDigits > 0) {
        FractionSeconds *= 10;
        FractionSecondsDigits--;
      }
    }
    EfiTime->Nanosecond = FractionSeconds * 1000;
  }

  if (HasTimezone) {
    if ((UINTN)TZ + sizeof(TZ) <= ((UINTN)GT + (UINTN)Time->Length)) {
      if ((TZ->Sign != '+') && (TZ->Sign != '-')) {
        HasTimezone = FALSE;
        DEBUG ((DEBUG_INFO, "Generalized Time zone incorrect format\n"));
      }
    } else {
      HasTimezone = FALSE;
      DEBUG ((DEBUG_INFO, "Generalized Time zone out of buffer bounds\n"));
    }
  }

  EfiTime->Year   = (GT->Year[0] - '0') * 1000 + (GT->Year[1] - '0') * 100 +
                    (GT->Year[2] - '0') * 10 + (GT->Year[3] = '0');
  EfiTime->Month  = (GT->Month[0] - '0') * 10 + (GT->Month[1] - '0');
  EfiTime->Day    = (GT->Day[0] - '0') * 10 + (GT->Day[1] - '0');
  EfiTime->Hour   = (GT->Hour[0] - '0') * 10 + (GT->Hour[1] - '0');
  EfiTime->Minute = (GT->Minute[0] - '0') * 10 + (GT->Minute[1] - '0');
  EfiTime->Second = (GT->Seconds[0] - '0') * 10 + (GT->Seconds[1] - '0');

  if (HasTimezone) {
    EfiTime->TimeZone = (TZ->Hour[0] - '0') * 1000 + (TZ->Hour[1] - '0') * 100;
    EfiTime->TimeZone += (TZ->Minute[0] - '0') * 10 + (TZ->Minute[1] - '0');
    if (TZ->Sign == '-') {
      EfiTime->TimeZone = -EfiTime->TimeZone;
    }
  } else if (IsGMT) {
    EfiTime->TimeZone = 0;
  } else {
    EfiTime->TimeZone = EFI_UNSPECIFIED_TIMEZONE;
  }

  return IsTimeValid (EfiTime);
}

BOOLEAN
Asn1TimeToEfiTime (
  IN     OPENSSL_ASN1_TIME *Asn1Time,
  IN OUT EFI_TIME          *EfiTime
  )
{
  SetMem (EfiTime, sizeof (EFI_TIME), 0);

  if (Asn1Time->Type == ASN1_TYPE_UTC_TIME) {
    return Asn1UtcTimeToEfiTime (Asn1Time, EfiTime);
  }

  if (Asn1Time->Type == ASN1_TYPE_GENERALIZED_TIME) {
    return Asn1GeneralizedTimeToEfiTime (Asn1Time, EfiTime);
  }

  return FALSE;
}

OPENSSL_ASN1_TIME *
EfiTimeToAsn1Time (
  IN EFI_TIME              *EfiTime
  )
{
  UINTN DateTimeSize;
  CHAR8 TimeStr[50];
  VOID *Time;

  DateTimeSize = 0;
  Time = NULL;

  if (EfiTime == NULL) {
    return NULL;
  }

  if (!IsTimeValid (EfiTime)) {
    return NULL;
  }

  SetMem (TimeStr, sizeof (TimeStr), 0);
  // Print the YYYYMMDDhhmmssZ string
  AsciiSPrint (TimeStr, sizeof (TimeStr), "%04u%02u%02u%02u%02u%02uZ",
               EfiTime->Year, EfiTime->Month, EfiTime->Day,
               EfiTime->Hour, EfiTime->Minute, EfiTime->Second);

  // Get required buffer size
  X509FormatDateTime (TimeStr, NULL, &DateTimeSize);
  if (DateTimeSize == 0) {
    return NULL;
  }
  Time = AllocateZeroPool (DateTimeSize);
  if (Time == NULL) {
    return NULL;
  }

  if (!X509FormatDateTime (TimeStr, Time, &DateTimeSize)) {
    FreePool (Time);
    return NULL;
  }

  return (OPENSSL_ASN1_TIME *)Time;
}

/**
  Helper function to populate an EFI_TIME instance.

  @param[in] Time   Pointer to the time structure

**/
EFI_STATUS
GetCurrentTime (
  IN EFI_TIME  *Time
  )
{
  EFI_STATUS  Status;
  VOID        *TestPointer;

  if (Time == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  Status = gBS->LocateProtocol (&gEfiRealTimeClockArchProtocolGuid, NULL, &TestPointer);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  ZeroMem (Time, sizeof (EFI_TIME));
  Status = gRT->GetTime (Time, NULL);
  if (EFI_ERROR (Status)) {
    DEBUG ((
      DEBUG_ERROR,
      "%a(), GetTime() failed, status = '%r'\n",
      __func__,
      Status
      ));
    return Status;
  }

  Time->Pad1       = 0;
  Time->Nanosecond = 0;
  Time->TimeZone   = 0;
  Time->Daylight   = 0;
  Time->Pad2       = 0;

  return EFI_SUCCESS;
}
