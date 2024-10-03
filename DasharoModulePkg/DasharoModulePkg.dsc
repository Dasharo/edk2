## @file
# Dasharo Module Package
#
# Copyright (c) 2022, 3mdeb Sp. z o.o. All rights reserved.<BR>
# SPDX-License-Identifier: BSD-2-Clause
#
##

[Defines]
  PLATFORM_NAME                  = MdeModule
  PLATFORM_GUID                  = D11BE2F6-8BD9-4099-8C73-2E09220FF8DD
  PLATFORM_VERSION               = 0.1
  DSC_SPECIFICATION              = 0x00010005
  OUTPUT_DIRECTORY               = Build/DasharoModulePkg
  SUPPORTED_ARCHITECTURES        = IA32|X64|EBC
  BUILD_TARGETS                  = DEBUG|RELEASE|NOOPT
  SKUID_IDENTIFIER               = DEFAULT

[LibraryClasses]
  PcdLib|MdePkg/Library/DxePcdLib/DxePcdLib.inf

[Components]
  !include DasharoModulePkg/DasharoModuleComponents.dsc.inc

[BuildOptions]
  *_*_*_CC_FLAGS = -D DISABLE_NEW_DEPRECATED_INTERFACES -Wno-error

[Packages]
  !include DasharoModulePkg/Include/UserAuthFeature.dsc
