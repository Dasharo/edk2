/** @file
  Means for collecting and reporting information about a capsule update.

  Copyright (c) 2026, 3mdeb Sp. z o.o. All rights reserved.<BR>
  SPDX-License-Identifier: GPL-2-or-later

**/

#ifndef _UPDATE_REPORT_H_
#define _UPDATE_REPORT_H_

// This should give an idea where the failure has happened.
typedef enum {
  CAPSULE_UNKNOWN,   // Not yet set.
  CAPSULE_REJECTED,  // Unmatched firmware GUID in the capsule.
  CAPSULE_REFUSED,   // Capsule has failed early validation.
  CAPSULE_NONFMP,    // Capsule is not of FMP type.
  CAPSULE_DRIVER,    // Failed to start a driver.
  CAPSULE_PAYLOAD,   // Failed to set a payload image.
  CAPSULE_FAILED,    // Update process hit an error.
  CAPSULE_SUCCESS,   // Everything went fine.
} UpdateOutcome;

typedef struct {
  EFI_STATUS  Status;
  CHAR16      *Message;  // Optional, can be NULL.
} PayloadResult;

typedef struct {
  UINTN               Index;    // Because capsules can be processed out of order.
  EFI_CAPSULE_HEADER  *Header;

  UpdateOutcome  Outcome;
  EFI_STATUS     Status;

  PayloadResult  *Payloads;
  UINTN          PayloadCount;
} CapsuleResult;

typedef struct {
  CapsuleResult  *Capsules;
  UINTN          CapsuleCount;

  BOOLEAN  Success;
} UpdateReport;

//
// The report can be zero initialized, but its resources needs to be freed with
// ReportFree().
//
// Each call to ReportAddCapsule() invalidates the pointer returned by the
// previous call.  ReportPopCapsule() undoes the most recent ReportAddCapsule().
//
// ReportAddCapsule() returns NULL on failure and it's OK to pass this to
// ReportCapsuleOutcome() and ReportAddPayload().  Can't do much if memory
// allocation fails, so just collect as much information as possible.
//
// ReportCapsuleOutcome() can be called multiple times with result of the first
// call being preserved.  This gives higher priority to the result set by a
// nested call while the callee can still set a generic error code when nothing
// more specific is available.
//

VOID
EFIAPI
ReportFree (
  IN OUT UpdateReport  *Report
  );

CapsuleResult *
EFIAPI
ReportAddCapsule (
  IN UpdateReport        *Report,
  IN UINTN               Index,
  IN EFI_CAPSULE_HEADER  *Header
  );

VOID
EFIAPI
ReportPopCapsule (
  IN UpdateReport  *Report
  );

VOID
EFIAPI
ReportCapsuleOutcome (
  IN CapsuleResult  *CapsuleResult OPTIONAL,
  IN UpdateOutcome  Outcome,
  IN EFI_STATUS     Status
  );

VOID
EFIAPI
ReportAddPayload (
  IN OUT CapsuleResult  *Capsule OPTIONAL,
  IN EFI_STATUS         Status,
  IN CONST CHAR16       *Message OPTIONAL
  );

VOID
EFIAPI
ReportDisplay (
  IN CONST UpdateReport  *Report
  );

#endif // _UPDATE_REPORT_H_
