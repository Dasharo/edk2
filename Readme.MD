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
    Customized Secure Boot feature provides capabilities for automated platform deployment by enterprises, OEMs, system integrators, and
  enthusiasts into custom, higher security Secure Boot configurations.  This can mitigate chain of custody concerns in the supply chain
  of a given hardware platform. It further provides the ability to manage multiple UEFI certificate signers and image revocations from 
  multiple signers.  It also provides a viable solution to enterprise, enthusiast, and OS vendor signing of images while maintaining overall
  security of the pre-boot environment.  Finally, it provides for a consistent programmatic and secure re-deployment of already-deployed systems.
    To achieve customized and high assurance deployments, this proposal introduces two new modes to the UEFI Secure Boot design, Audit Mode and 
  Deployed Mode, and corresponding variables to control their state, "AuditMode" and "DeployedMode".
  Audit Mode:
      Audit Mode enables programmatic discovery of signature list combinations that successfully authenticate installed EFI images without the risk 
    of rendering a system unbootable.
  Deployed Mode:
      Deployed Mode is the most secure mode. It is the recommended mode for systems after the Secure Boot configuration has been selected, configured, 
    and tested.

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
  http://www.uefi.org/sites/default/files/resources/UEFI%20Spec%202_6.pdf 

7. Misc
  N/A