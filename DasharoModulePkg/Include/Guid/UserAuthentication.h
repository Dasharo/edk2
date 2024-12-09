/** @file
  GUID is for UserAuthentication SMM communication.

  Copyright (c) 2019, Intel Corporation. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#ifndef __USER_AUTHENTICATION_GUID_H__
#define __USER_AUTHENTICATION_GUID_H__

#define PASSWORD_MIN_SIZE    9  // MIN number of chars of password, including NULL.
#define PASSWORD_MAX_SIZE    33 // MAX number of chars of password, including NULL.

#define PASSWORD_SALT_SIZE   32
#define PASSWORD_HASH_SIZE   32 // SHA256_DIGEST_SIZE

#define PASSWORD_MAX_TRY_COUNT  3
#define PASSWORD_HISTORY_CHECK_COUNT  5

//
// Name of the variable
//
#define USER_AUTHENTICATION_VAR_NAME L"Password"
#define USER_AUTHENTICATION_HISTORY_LAST_VAR_NAME L"PasswordLast"

//
// Variable storage
//
typedef struct {
  UINT8        PasswordHash[PASSWORD_HASH_SIZE];
  UINT8        PasswordSalt[PASSWORD_SALT_SIZE];
} USER_PASSWORD_VAR_STRUCT;

#define USER_AUTHENTICATION_GUID \
  { 0xf06e3ea7, 0x611c, 0x4b6b, { 0xb4, 0x10, 0xc2, 0xbf, 0x94, 0x3f, 0x38, 0xf2 } }

extern EFI_GUID gUserAuthenticationGuid;

typedef struct {
  CHAR8                                 NewPassword[PASSWORD_MAX_SIZE];
  CHAR8                                 OldPassword[PASSWORD_MAX_SIZE];
} PASSWORD_COMMUNICATE_SET_PASSWORD;

typedef struct {
  CHAR8                                 Password[PASSWORD_MAX_SIZE];
} PASSWORD_COMMUNICATE_VERIFY_PASSWORD;

typedef struct {
  BOOLEAN                               NeedReVerify;
} PASSWORD_COMMUNICATE_VERIFY_POLICY;

#endif
