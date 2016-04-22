################################################################################
# ReadMe.MD for EDKII new feature staging branch.. 
#  It contains several parts to describe the staging branch.
#    1. Introduction 
#    2. Branch Owner
#    3. Feature Summary
#    4. Timeline
#    5. Related Modules
#    6. Related Links
#    7. Misc
#
###############################################################################
   
1. Introduction
   This staging branch is requested by Jeremiah Cox<jerecox@microsoft.com> for ECR 1263 Customized Secure Boot feature. 
 This ECR has some conflicting language/figures that may result in in consistent implementations 

2. Branch Owner
  Chao Zhang <chao.b.zhang@intel.com>

3. Feature Summary
  Customized Secure Boto feature provides capabilities for automated platform deployment by enterprises, OEMs, system integrators, and
  enthusiasts into custom, higher security Secure Boot configurations.  This can mitigate chain of custody concerns in the supply chain
  of a given hardware platform. It further provides the ability to manage multiple UEFI certificate signers and image revocations from 
  multiple signers.  It also provides a viable solution to enterprise, enthusiast, and OS vendor signing of images while maintaining overall
  security of the pre-boot environment.  Finally, it provides for a consistent programmatic and secure re-deployment of already-deployed systems.

4. Timeline
  N/A

5. Related Modules
  MdePkg
    Include/Guid/GlobalVariable.h
    Include/Guid/ImageAuthentication.h
  MdeModulePkg
    VarCheckUefiLib
  SecuritPkg
    AuthVariableLib
    SecureBootConfigDxe
    
6. Related Links
  https://mantis.uefi.org/mantis/view.php?id=1263

7. Misc
  N/A