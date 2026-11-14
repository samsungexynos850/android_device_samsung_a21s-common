#
# SPDX-FileCopyrightText: The LineageOS Project
# SPDX-License-Identifier: Apache-2.0
#

# Enable project quotas and casefolding for emulated storage without sdcardfs
$(call inherit-product, $(SRC_TARGET_DIR)/product/emulated_storage.mk)

# Audio
PRODUCT_PACKAGES += \
    android.hardware.audio.effect@7.0-impl \
    android.hardware.audio.service \
    android.hardware.audio@7.0-impl \
    android.hardware.bluetooth.audio-impl \
    audio.bluetooth.default \
    audio.primary.universal3830 \
    audio.r_submix.default \
    audio.usb.default \
    libsamsungSoundbooster_plus \
    SamsungDAP \
    SoundBoosterStage

$(call soong_config_set,samsungAudioVars,soundbooster_dsp_library,//vendor/samsung/a21s-common:lib_SoundBooster_ver1050)

PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/configs/audio/audio_effects.xml:$(TARGET_COPY_OUT_VENDOR)/etc/audio_effects.xml \
    $(LOCAL_PATH)/configs/audio/audio_policy_configuration.xml:$(TARGET_COPY_OUT_VENDOR)/etc/audio_policy_configuration.xml \
    frameworks/av/services/audiopolicy/config/audio_policy_volumes.xml:$(TARGET_COPY_OUT_VENDOR)/etc/audio_policy_volumes.xml \
    frameworks/av/services/audiopolicy/config/bluetooth_audio_policy_configuration_7_0.xml:$(TARGET_COPY_OUT_VENDOR)/etc/bluetooth_audio_policy_configuration_7_0.xml \
    frameworks/av/services/audiopolicy/config/default_volume_tables.xml:$(TARGET_COPY_OUT_VENDOR)/etc/default_volume_tables.xml \
    frameworks/av/services/audiopolicy/config/r_submix_audio_policy_configuration.xml:$(TARGET_COPY_OUT_VENDOR)/etc/r_submix_audio_policy_configuration.xml \
    frameworks/av/services/audiopolicy/enginedefault/config/example/phone/audio_policy_engine_configuration.xml:$(TARGET_COPY_OUT_VENDOR)/etc/audio_policy_engine_configuration.xml \
    frameworks/av/services/audiopolicy/enginedefault/config/example/phone/audio_policy_engine_default_stream_volumes.xml:$(TARGET_COPY_OUT_VENDOR)/etc/audio_policy_engine_default_stream_volumes.xml \
    frameworks/av/services/audiopolicy/enginedefault/config/example/phone/audio_policy_engine_product_strategies.xml:$(TARGET_COPY_OUT_VENDOR)/etc/audio_policy_engine_product_strategies.xml \
    frameworks/av/services/audiopolicy/enginedefault/config/example/phone/audio_policy_engine_stream_volumes.xml:$(TARGET_COPY_OUT_VENDOR)/etc/audio_policy_engine_stream_volumes.xml

# Bluetooth
PRODUCT_PACKAGES += \
    android.hardware.bluetooth@1.0-impl:64 \
    android.hardware.bluetooth@1.0-service \
    libbt-vendor:64 \
    bt_did.slsi.conf

# Camera
PRODUCT_PACKAGES += \
    android.hardware.camera.provider-service.samsung \
    camera.universal3830

# CarrierSettings
PRODUCT_PACKAGES += \
    CarrierSettingsResCommon

# Charger
PRODUCT_PACKAGES += \
    charger_res_images_vendor

# Codec2
PRODUCT_PACKAGES += \
    samsung.hardware.media.c2-service

PRODUCT_PACKAGES += \
    libExynosC2H263Dec \
    libExynosC2H263Enc \
    libExynosC2H264Dec \
    libExynosC2H264Enc \
    libExynosC2HevcDec \
    libExynosC2HevcEnc \
    libExynosC2Mpeg4Dec \
    libExynosC2Mpeg4Enc \
    libExynosC2Vp8Dec \
    libExynosC2Vp8Enc

PRODUCT_PACKAGES += \
    codec2.vendor.base.policy \
    codec2.vendor.ext.policy

# Display
PRODUCT_PACKAGES += \
    android.hardware.composer.hwc3-service.slsi \
    android.hardware.graphics.allocator@4.0-service \
    android.hardware.graphics.mapper@4.0-impl

# Doze
PRODUCT_PACKAGES += \
    SamsungDoze

# DRM
PRODUCT_PACKAGES += \
    android.hardware.drm-service.clearkey

# Dynamic Partitions
PRODUCT_USE_DYNAMIC_PARTITIONS := true

# Fingerprint
PRODUCT_PACKAGES += \
    android.hardware.biometrics.fingerprint-service.samsung

# Gatekeeper
PRODUCT_PACKAGES += \
    android.hardware.gatekeeper-service.teegris

# GNSS
PRODUCT_PACKAGES += \
    android.hardware.gnss@2.1-service.samsung

# Graphics
# Device uses high-density artwork where available
PRODUCT_AAPT_CONFIG := normal
PRODUCT_AAPT_PREF_CONFIG := 280dpi
PRODUCT_AAPT_PREBUILT_DPI := xxxhdpi xxhdpi xhdpi hdpi

# Health
PRODUCT_PACKAGES += \
    android.hardware.health-service.samsung \
    android.hardware.health-service.samsung-recovery

# HIDL
PRODUCT_PACKAGES += \
    libhwbinder \
    libhwbinder.vendor

# Init
PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/configs/init/fstab.exynos850:$(TARGET_COPY_OUT_RAMDISK)/fstab.exynos850 \
    $(LOCAL_PATH)/configs/init/fstab.exynos850:$(TARGET_COPY_OUT_VENDOR)/etc/fstab.exynos850 \
    $(LOCAL_PATH)/configs/init/init.exynos850.rc:$(TARGET_COPY_OUT_VENDOR)/etc/init/init.exynos850.rc \
    $(LOCAL_PATH)/configs/init/init.exynos850.usb.rc:$(TARGET_COPY_OUT_VENDOR)/etc/init/init.exynos850.usb.rc \
    $(LOCAL_PATH)/configs/init/init.recovery.exynos850.rc:$(TARGET_COPY_OUT_RECOVERY)/root/init.recovery.exynos850.rc \
    $(LOCAL_PATH)/configs/init/ueventd.rc:$(TARGET_COPY_OUT_VENDOR)/etc/ueventd.rc

# IMS
$(call inherit-product, hardware/lineage/generic-ims/ims.mk)

# Kernel
PRODUCT_OTA_ENFORCE_VINTF_KERNEL_REQUIREMENTS := false
PRODUCT_SET_DEBUGFS_RESTRICTIONS := true

# Keymaster
PRODUCT_PACKAGES += \
    android.hardware.keymaster@4.0-service.samsung

$(call soong_config_set,samsungVars,target_keymaster4_library,//vendor/samsung/a21s-common:libskeymaster4device)

# Lineage Health
PRODUCT_PACKAGES += \
    vendor.lineage.health-service.default

# Media
PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/configs/media/media_codecs_c2.xml:$(TARGET_COPY_OUT_VENDOR)/etc/media_codecs_c2.xml

# Memtrack
PRODUCT_PACKAGES += \
    android.hardware.memtrack-service.samsung-mali

# Mixer
PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/configs/mixer/mixer_paths.xml:$(TARGET_COPY_OUT_VENDOR)/etc/mixer_paths.xml

# NFC and Secure Element packages
PRODUCT_PACKAGES += \
    android.hardware.nfc-service.sec \
    com.android.nfc_extras \
    Tag

PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/sku/permissions/unavail.android.hardware.nfc.xml:$(TARGET_COPY_OUT_ODM)/etc/permissions/sku_disabled/unavail.hardware.samsung.nfc.xml

# Overlays
PRODUCT_PACKAGES += \
    ApertureOverlayCommon \
    FrameworkResOverlayCommon \
    LineageSDKOverlayCommon \
    SettingsOverlayCommon \
    SystemUIOverlayCommon \
    TelephonyResCommon

PRODUCT_ENFORCE_RRO_TARGETS += *

# Permissions
PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/android.hardware.audio.low_latency.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.audio.low_latency.xml \
    frameworks/native/data/etc/android.hardware.audio.pro.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.audio.pro.xml \
    frameworks/native/data/etc/android.hardware.fingerprint.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.fingerprint.xml \
    frameworks/native/data/etc/android.hardware.nfc.ese.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.nfc.ese.xml \
    frameworks/native/data/etc/android.hardware.nfc.hce.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.nfc.hce.xml \
    frameworks/native/data/etc/android.hardware.nfc.hcef.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.nfc.hcef.xml \
    frameworks/native/data/etc/android.hardware.nfc.uicc.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.nfc.uicc.xml \
    frameworks/native/data/etc/android.hardware.nfc.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.nfc.xml \
    frameworks/native/data/etc/android.hardware.bluetooth.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.bluetooth.xml \
    frameworks/native/data/etc/android.hardware.bluetooth_le.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.bluetooth_le.xml \
    frameworks/native/data/etc/android.hardware.camera.ar.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.camera.ar.xml \
    frameworks/native/data/etc/android.hardware.camera.autofocus.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.camera.autofocus.xml \
    frameworks/native/data/etc/android.hardware.camera.flash-autofocus.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.camera.flash-autofocus.xml \
    frameworks/native/data/etc/android.hardware.camera.front.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.camera.front.xml \
    frameworks/native/data/etc/android.hardware.camera.full.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.camera.full.xml \
    frameworks/native/data/etc/android.hardware.camera.raw.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.camera.raw.xml \
    frameworks/native/data/etc/android.hardware.camera.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.camera.xml \
    frameworks/native/data/etc/android.hardware.location.gps.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.location.gps.xml \
    frameworks/native/data/etc/android.hardware.opengles.aep.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.opengles.aep.xml \
    frameworks/native/data/etc/android.hardware.sensor.accelerometer.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.sensor.accelerometer.xml \
    frameworks/native/data/etc/android.hardware.sensor.hifi_sensors.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.sensor.hifi_sensors.xml \
    frameworks/native/data/etc/android.hardware.sensor.gyroscope.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.sensor.gyroscope.xml \
    frameworks/native/data/etc/android.hardware.sensor.compass.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.sensor.compass.xml \
    frameworks/native/data/etc/android.hardware.sensor.proximity.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.sensor.proximity.xml \
    frameworks/native/data/etc/android.hardware.sensor.stepcounter.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.sensor.stepcounter.xml \
    frameworks/native/data/etc/android.hardware.sensor.stepdetector.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.sensor.stepdetector.xml \
    frameworks/native/data/etc/android.hardware.telephony.gsm.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.telephony.gsm.xml \
    frameworks/native/data/etc/android.hardware.telephony.ims.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.telephony.ims.xml \
    frameworks/native/data/etc/android.hardware.touchscreen.multitouch.jazzhand.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.touchscreen.multitouch.jazzhand.xml \
    frameworks/native/data/etc/android.hardware.usb.accessory.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.usb.accessory.xml \
    frameworks/native/data/etc/android.hardware.usb.host.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.usb.host.xml \
    frameworks/native/data/etc/android.hardware.vulkan.compute-0.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.vulkan.compute.xml \
    frameworks/native/data/etc/android.hardware.vulkan.level-1.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.vulkan.level.xml \
    frameworks/native/data/etc/android.hardware.vulkan.version-1_1.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.vulkan.version.xml \
    frameworks/native/data/etc/android.hardware.wifi.direct.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.wifi.direct.xml \
    frameworks/native/data/etc/android.hardware.wifi.passpoint.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.wifi.passpoint.xml \
    frameworks/native/data/etc/android.hardware.wifi.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.wifi.xml \
    frameworks/native/data/etc/android.software.app_widgets.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.software.app_widgets.xml \
    frameworks/native/data/etc/android.software.device_id_attestation.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.software.device_id_attestation.xml \
    frameworks/native/data/etc/android.software.freeform_window_management.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.software.freeform_window_management.xml \
    frameworks/native/data/etc/android.software.ipsec_tunnels.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.software.ipsec_tunnels.xml \
    frameworks/native/data/etc/android.software.midi.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.software.midi.xml \
    frameworks/native/data/etc/android.software.opengles.deqp.level-2020-03-01.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.software.opengles.deqp.level.xml \
    frameworks/native/data/etc/android.software.picture_in_picture.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.software.picture_in_picture.xml \
    frameworks/native/data/etc/android.software.sip.voip.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.software.sip.voip.xml \
    frameworks/native/data/etc/android.software.vulkan.deqp.level-2020-03-01.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.software.vulkan.deqp.level.xml \
    frameworks/native/data/etc/com.nxp.mifare.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/com.nxp.mifare.xml \
    frameworks/native/data/etc/com.android.nfc_extras.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/com.android.nfc_extras.xml \
    frameworks/native/data/etc/handheld_core_hardware.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/handheld_core_hardware.xml

# Power
PRODUCT_PACKAGES += \
    android.hardware.power-service.pixel-libperfmgr

PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/configs/power/powerhint.json:$(TARGET_COPY_OUT_VENDOR)/etc/powerhint.json

# Public Libraries
PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/configs/linker/public.libraries.txt:$(TARGET_COPY_OUT_VENDOR)/etc/public.libraries.txt

# RIL
PRODUCT_PACKAGES += \
    libsec-ril \
    secril_config_svc \
    sehradiomanager \
    cbd

PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/configs/ril/sehradiomanager.conf:$(TARGET_COPY_OUT_VENDOR)/etc/sehradiomanager.conf

# Sensors
PRODUCT_PACKAGES += \
    android.hardware.sensors-service.samsung-multihal

# Soong Namespaces
PRODUCT_SOONG_NAMESPACES += \
    $(LOCAL_PATH) \
    hardware/google/interfaces \
    hardware/google/pixel/pixelstats \
    hardware/google/pixel/power-libperfmgr \
    hardware/google/pixel/thermal \
    hardware/samsung \
    hardware/samsung_slsi/libbt \
    hardware/samsung_slsi/scsc_wifibt/wpa_supplicant_lib \
    hardware/samsung_slsi-linaro/camera/N23 \
    hardware/samsung_slsi-linaro/exynos/gralloc/gralloc4 \
    hardware/samsung_slsi-linaro/exynos/libaudio/audiohal_comv1 \
    hardware/samsung_slsi-linaro/exynos/cpboot_v3

# Thermal
PRODUCT_PACKAGES += \
    android.hardware.thermal-service.pixel \
    thermal_symlinks

PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/configs/thermal/thermal_info_config.json:$(TARGET_COPY_OUT_VENDOR)/etc/thermal_info_config.json

# USB
PRODUCT_PACKAGES += \
    android.hardware.usb-service.samsung \
    android.hardware.usb.gadget-service.samsung

# Vibrator
PRODUCT_PACKAGES += \
    android.hardware.vibrator-service.samsung

# WiFi
PRODUCT_PACKAGES += \
    WifiOverlay \
    android.hardware.wifi-service \
    hostapd \
    wpa_supplicant \
    wpa_supplicant.conf

PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/configs/wifi/wpa_supplicant_overlay.conf:$(TARGET_COPY_OUT_VENDOR)/etc/wifi/wpa_supplicant_overlay.conf

PRODUCT_CFI_INCLUDE_PATHS += hardware/samsung_slsi/scsc_wifibt/wpa_supplicant_lib

# Call Samsung LSI board support package makefiles
$(call inherit-product, hardware/samsung_slsi-linaro/config/config.mk)

# Call the proprietary setup
$(call inherit-product, vendor/samsung/a21s-common/a21s-common-vendor.mk)
