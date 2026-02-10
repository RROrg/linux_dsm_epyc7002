include /env.mak

ifeq ($(SYNO_PLATFORM),BROMOLOW)
export SUBDIR := bromolow
endif
ifeq ($(SYNO_PLATFORM),GRANTLEY)
export SUBDIR := grantley
endif
ifeq ($(SYNO_PLATFORM),CEDARVIEW)
export SUBDIR := cedarview
endif
ifeq ($(SYNO_PLATFORM),EVANSPORT)
export SUBDIR := evansport
endif
ifeq ($(SYNO_PLATFORM),AVOTON)
export SUBDIR := avoton
endif
ifeq ($(SYNO_PLATFORM),BRASWELL)
export SUBDIR := braswell
endif
ifeq ($(SYNO_PLATFORM),APOLLOLAKE)
export SUBDIR := apollolake
endif
ifeq ($(SYNO_PLATFORM),MARVELL_ARMADAXP)
export SUBDIR := armada
endif
ifeq ($(SYNO_PLATFORM),MARVELL_ARMADA370)
export SUBDIR := armada
endif
ifeq ($(SYNO_PLATFORM),MARVELL_ARMADA375)
export SUBDIR := armada
endif
ifeq ($(SYNO_PLATFORM),MINDSPEED_COMCERTO2K)
export SUBDIR := comcerto2k
endif
ifeq ($(SYNO_PLATFORM),ALPINE)
export SUBDIR := alpine
endif
ifeq ($(SYNO_PLATFORM),STM_MONACO)
export SUBDIR := monaco
endif
ifeq ($(SYNO_PLATFORM),KVMX64)
export SUBDIR := kvmx64
include Makefile_vdsm.inc
endif
ifeq ($(SYNO_PLATFORM),KVMX64SOFS)
export SUBDIR := kvmx64sofs
include Makefile_vdsm.inc
endif
ifeq ($(SYNO_PLATFORM),KVMX64V2)
export SUBDIR := kvmx64v2
include Makefile_vdsm.inc
endif
ifeq ($(SYNO_PLATFORM),BROADWELL)
export SUBDIR := broadwell
endif
ifeq ($(SYNO_PLATFORM),MARVELL_ARMADA38X)
export SUBDIR := armada38x
endif
ifeq ($(SYNO_PLATFORM),REALTEK_RTD1296)
export SUBDIR := rtd1296
endif
ifeq ($(SYNO_PLATFORM),DENVERTON)
export SUBDIR := denverton
endif
ifeq ($(SYNO_PLATFORM),BROADWELLNK)
export SUBDIR := broadwellnk
endif
ifeq ($(SYNO_PLATFORM),MARVELL_ARMADA37XX)
export SUBDIR := armada37xx
endif
ifeq ($(SYNO_PLATFORM),PURLEY)
export SUBDIR := purley
endif
ifeq ($(SYNO_PLATFORM),COFFEELAKE)
export SUBDIR := coffeelake
endif
ifeq ($(SYNO_PLATFORM),BROADWELLNTB)
export SUBDIR := broadwellntb
endif
ifeq ($(SYNO_PLATFORM),NEXTKVMX64)
export SUBDIR := nextkvmx64
include Makefile_vdsm.inc
endif
ifeq ($(SYNO_PLATFORM),GEMINILAKE)
export SUBDIR := geminilake
endif
ifeq ($(SYNO_PLATFORM),REALTEK_RTD1619)
export SUBDIR := rtd1619
endif
ifeq ($(SYNO_PLATFORM),SKYLAKED)
export SUBDIR := skylaked
endif
ifeq ($(SYNO_PLATFORM),V1000)
export SUBDIR := v1000
endif
ifeq ($(SYNO_PLATFORM),V1000SOFS)
export SUBDIR := v1000sofs
endif
ifeq ($(SYNO_PLATFORM),BROADWELLNTBAP)
export SUBDIR := broadwellntbap
endif
ifeq ($(SYNO_PLATFORM),KVMCLOUD)
export SUBDIR := kvmcloud
include Makefile_vdsm.inc
endif
ifeq ($(SYNO_PLATFORM),EPYC7002)
export SUBDIR := epyc7002
endif
ifeq ($(SYNO_PLATFORM),EPYC7002SOFS)
export SUBDIR := epyc7002sofs
endif
ifeq ($(SYNO_PLATFORM),R1000)
export SUBDIR := r1000
endif
ifeq ($(SYNO_PLATFORM),ICELAKED)
export SUBDIR := icelaked
endif
ifeq ($(SYNO_PLATFORM),REALTEK_RTD1619B)
export SUBDIR := rtd1619b
endif
ifeq ($(SYNO_PLATFORM),BROADWELLNKV2)
export SUBDIR := broadwellnkv2
endif
ifeq ($(SYNO_PLATFORM),RYZEN5K)
export SUBDIR := ryzen5k
endif
ifeq ($(SYNO_PLATFORM),GEMINILAKEVS)
export SUBDIR := geminilakevs
endif
ifeq ($(SYNO_PLATFORM),EPYC7003NTB)
export SUBDIR := epyc7003ntb
endif
