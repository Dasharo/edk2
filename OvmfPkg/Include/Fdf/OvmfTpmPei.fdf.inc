##
#    SPDX-License-Identifier: BSD-2-Clause-Patent
##

!if $(TPM2_ENABLE) == TRUE
INF  OvmfPkg/Tcg/TpmMmioSevDecryptPei/TpmMmioSevDecryptPei.inf
!if $(TPM1_ENABLE) == TRUE
INF  OvmfPkg/Tcg/Tcg2Config/Tcg12ConfigPei.inf
INF  SecurityPkg/Tcg/TcgPei/TcgPei.inf
!else
INF  OvmfPkg/Tcg/Tcg2Config/Tcg2ConfigPei.inf
!endif
INF  SecurityPkg/Tcg/Tcg2Pei/Tcg2Pei.inf
INF  SecurityPkg/Tcg/Tcg2PlatformPei/Tcg2PlatformPei.inf
!if $(OPAL_PASSWORD_ENABLE) == TRUE
INF SecurityPkg/Tcg/Opal/OpalPassword/OpalPasswordPei.inf
!endif
!endif
