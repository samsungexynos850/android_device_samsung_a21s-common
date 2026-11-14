#
# SPDX-FileCopyrightText: The LineageOS Project
# SPDX-License-Identifier: Apache-2.0
#

COMMON_PATH := device/samsung/a21s-common

# A/B
AB_OTA_UPDATER := false

# Architecture
TARGET_ARCH := arm64
TARGET_ARCH_VARIANT := armv8-a
TARGET_CPU_ABI := arm64-v8a
TARGET_CPU_VARIANT := generic

TARGET_2ND_ARCH := arm
TARGET_2ND_ARCH_VARIANT := armv8-a
TARGET_2ND_CPU_ABI := armeabi-v7a
TARGET_2ND_CPU_ABI2 := armeabi
TARGET_2ND_CPU_VARIANT := generic

# Audio
$(call soong_config_set,android_hardware_audio,run_64bit,true)
$(call soong_config_set,exynos_audio,PREDEFINED_LOW_CAPTURE_DURATION,20)
$(call soong_config_set,exynos_audio,PROXY_LIBRARY,//device/samsung/a21s-common:libaudioproxy)

# Boot Image
BOARD_CUSTOM_BOOTIMG := true
BOARD_MKBOOTIMG_ARGS := --base 0x10000000
BOARD_MKBOOTIMG_ARGS += --dtb_offset 0x00000000
BOARD_MKBOOTIMG_ARGS += --header_version 2
BOARD_MKBOOTIMG_ARGS += --kernel_offset 0x00008000
BOARD_MKBOOTIMG_ARGS += --pagesize 2048
BOARD_MKBOOTIMG_ARGS += --ramdisk_offset 0x01000000
BOARD_MKBOOTIMG_ARGS += --tags_offset 0x00000100

# Camera - (ultra wide SS3) - (macro, SS4)
$(call soong_config_set_bool,samsungCameraVars,usage_64bit,true)
$(call soong_config_set,exynos_camera,target_soc_base,exynos3830)
$(call soong_config_set,exynos_camera,back_camera_sensor,SENSOR_NAME_S5KGM2)
$(call soong_config_set,exynos_camera,back_2_camera_sensor,SENSOR_NAME_S5K4HA)
$(call soong_config_set,exynos_camera,back_3_camera_sensor,SENSOR_NAME_GC02M1)
$(call soong_config_set,exynos_camera,front_camera_sensor,SENSOR_NAME_HI1336)
$(call soong_config_set,exynos_camera,back_camera_rotation,90)
$(call soong_config_set,exynos_camera,front_camera_rotation,270)
$(call soong_config_set,exynos_camera,secure_camera_rotation,0)
$(call soong_config_set_bool,exynos_camera,use_camera_exynos3830_meta,true)
$(call soong_config_set_bool,exynos_camera,uses_slsi_plugin,true)
$(call soong_config_set_bool,exynos_camera,uses_slsi_vendor_tags,true)

# Compatability Matrix
DEVICE_MATRIX_FILE := $(COMMON_PATH)/compatibility_matrix.xml

# Display
BOARD_MINIMUM_DISPLAY_BRIGHTNESS := 1
TARGET_SCREEN_DENSITY := 280

# DTB
BOARD_DTB_CFG := $(COMMON_PATH)/configs/kernel/exynos850.cfg
BOARD_INCLUDE_DTB_IN_BOOTIMG := true

# DTBO
BOARD_DTBO_CFG := $(COMMON_PATH)/configs/kernel/a21s.cfg
BOARD_KERNEL_SEPARATED_DTBO := true

# Dynamic Partitions
BOARD_SUPER_PARTITION_GROUPS := samsung_dynamic_partitions
BOARD_SUPER_PARTITION_SIZE := 5557452800
BOARD_SAMSUNG_DYNAMIC_PARTITIONS_SIZE := 5556404224
BOARD_SAMSUNG_DYNAMIC_PARTITIONS_PARTITION_LIST := \
    odm \
    product \
    system \
    system_ext \
    vendor

-include vendor/lineage/config/BoardConfigReservedSize.mk

# Filesystem
BOARD_CACHEIMAGE_FILE_SYSTEM_TYPE := ext4
BOARD_ODMIMAGE_FILE_SYSTEM_TYPE := erofs
BOARD_PRODUCTIMAGE_FILE_SYSTEM_TYPE := ext4
BOARD_SYSTEMIMAGE_FILE_SYSTEM_TYPE := ext4
BOARD_SYSTEM_EXTIMAGE_FILE_SYSTEM_TYPE := ext4
BOARD_VENDORIMAGE_FILE_SYSTEM_TYPE := erofs
TARGET_COPY_OUT_ODM := odm
TARGET_COPY_OUT_PRODUCT := product
TARGET_COPY_OUT_SYSTEM_EXT := system_ext
TARGET_COPY_OUT_VENDOR := vendor

# Framework Matrix
DEVICE_FRAMEWORK_COMPATIBILITY_MATRIX_FILE += hardware/samsung/vintf/samsung_framework_compatibility_matrix.xml

# Graphics
TARGET_USES_VULKAN := true

# HIDL Manifest
DEVICE_MANIFEST_FILE += $(COMMON_PATH)/manifest.xml

# Init
$(call soong_config_set,libinit,vendor_init_lib,//device/samsung/a21s-common:libinit_exynos3830)

# Kernel
BOARD_KERNEL_IMAGE_NAME := Image
TARGET_KERNEL_SOURCE := kernel/samsung/exynos850

# Lineage Health
$(call soong_config_set,lineage_health,charging_control_charging_disabled,1)
$(call soong_config_set,lineage_health,charging_control_charging_enabled,0)
$(call soong_config_set,lineage_health,charging_control_charging_path,/sys/class/power_supply/battery/batt_slate_mode)
$(call soong_config_set,lineage_health,fast_charge_node,/sys/class/sec/switch/afc_disable)
$(call soong_config_set,lineage_health,fast_charge_value_fast_charge,0)
$(call soong_config_set,lineage_health,fast_charge_value_none,1)
$(call soong_config_set_bool,lineage_health,charging_control_supports_bypass,false)
$(call soong_config_set_bool,lineage_health,charging_control_supports_deadline,false)
$(call soong_config_set_bool,lineage_health,charging_control_supports_toggle,true)

# Metadata
BOARD_USES_METADATA_PARTITION := true

# Partitions
BOARD_BOOTIMAGE_PARTITION_SIZE := 37748736
BOARD_CACHEIMAGE_PARTITION_SIZE := 209715200
BOARD_DTBOIMG_PARTITION_SIZE := 8388608
BOARD_FLASH_BLOCK_SIZE := 4096
BOARD_RECOVERYIMAGE_PARTITION_SIZE := 55574528
BOARD_ROOT_EXTRA_FOLDERS := efs

# Platform
BOARD_VENDOR := samsung
TARGET_BOARD_PLATFORM := universal3830
TARGET_BOOTLOADER_BOARD_NAME := exynos850
TARGET_SOC := exynos850

# Properties
TARGET_VENDOR_PROP += $(COMMON_PATH)/vendor.prop

# Recovery
BOARD_INCLUDE_RECOVERY_DTBO := true
TARGET_RECOVERY_FSTAB := $(COMMON_PATH)/configs/init/fstab.exynos850
TARGET_RECOVERY_PIXEL_FORMAT := ABGR_8888
TARGET_USERIMAGES_USE_F2FS := true
TARGET_USERIMAGES_USE_EXT4 := true

# Releasetools
TARGET_RECOVERY_UPDATER_LIBS := librecovery_updater_exynos850
TARGET_RELEASETOOLS_EXTENSIONS := $(COMMON_PATH)/releasetools

# RIL
ENABLE_VENDOR_RIL_SERVICE := true
$(call soong_config_set,cbd,protocol,sipc)

# Security
VENDOR_SECURITY_PATCH := 2024-05-01

# SELinux
BOARD_SEPOLICY_TEE_FLAVOR := teegris
BOARD_VENDOR_SEPOLICY_DIRS += $(COMMON_PATH)/sepolicy/vendor
include device/lineage/sepolicy/exynos/sepolicy.mk
include device/lineage/sepolicy/libion/sepolicy.mk
include device/samsung_slsi/sepolicy/sepolicy.mk

# USB
$(call soong_config_set,samsungUsbGadgetVars,gadget_name,13600000.dwc3)

# Verified Boot
BOARD_AVB_ENABLE := true
BOARD_AVB_MAKE_VBMETA_IMAGE_ARGS += --flags 0 --algorithm NONE
BOARD_AVB_RECOVERY_ALGORITHM := SHA256_RSA4096
BOARD_AVB_RECOVERY_KEY_PATH := external/avb/test/data/testkey_rsa4096.pem
BOARD_AVB_RECOVERY_ROLLBACK_INDEX := 1
BOARD_AVB_RECOVERY_ROLLBACK_INDEX_LOCATION := 1
BOARD_AVB_ROLLBACK_INDEX := $(PLATFORM_SECURITY_PATCH_TIMESTAMP)

# Wi-Fi
BOARD_WLAN_DEVICE                := slsi
BOARD_HOSTAPD_DRIVER             := NL80211
BOARD_HOSTAPD_PRIVATE_LIB        := lib_driver_cmd_slsi
BOARD_WPA_SUPPLICANT_DRIVER      := NL80211
BOARD_WPA_SUPPLICANT_PRIVATE_LIB := lib_driver_cmd_slsi
WIFI_HIDL_FEATURE_DUAL_INTERFACE := true
WIFI_HIDL_UNIFIED_SUPPLICANT_SERVICE_RC_ENTRY := true
WPA_SUPPLICANT_VERSION           := VER_0_8_X

# Inherit Samsung LSI board support package
include hardware/samsung_slsi-linaro/config/BoardConfig850.mk

# Inherit proprietary vendor configuartion
include vendor/samsung/a21s-common/BoardConfigVendor.mk
