# Copyright (C) 2019 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

LOCAL_PATH := $(call my-dir)
SOC_BASE_PATH := $(TOP)/hardware/samsung_slsi/exynos

include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
	audio_proxy.c

LOCAL_C_INCLUDES += \
	$(SOC_BASE_PATH)/include/libaudio/audiohal_comv1 \
	$(SOC_BASE_PATH)/libaudio/audiohal_comv1/odm_specific \
	external/tinyalsa/include \
	external/tinycompress/include \
	external/kernel-headers/original/uapi/sound \
	$(call include-path-for, audio-utils) \
	$(call include-path-for, audio-route) \
	$(call include-path-for, alsa-utils) \
	external/expat/lib

LOCAL_HEADER_LIBRARIES := libhardware_headers
LOCAL_SHARED_LIBRARIES := liblog libcutils libtinyalsa_sec libtinycompress libaudioutils libaudioroute_sec libalsautils_sec libexpat

LOCAL_CFLAGS += -Wno-unused-parameter -Wno-unused-variable -Wno-unused-function

ifeq (true,$(call spf_check,SEC_PRODUCT_FEATURE_AUDIO_COMMON,TRUE))
#ADD_FOR SEC_AUDIO_RESAMPLER
LOCAL_C_INCLUDES += vendor/samsung/frameworks/audio/postprocessconvertor
LOCAL_SHARED_LIBRARIES += libSamsungPostProcessConvertor

# samsung audio feature
#ADD_FOR SEC_AUDIO_DUMP
ifneq (,$(filter userdebug eng, $(TARGET_BUILD_VARIANT))) # end or userdebug bin
    LOCAL_C_INCLUDES += vendor/samsung/frameworks/audio/audiocoreutils
    LOCAL_SHARED_LIBRARIES += libsecaudiocoreutils
else ifeq ($(SEC_PRODUCT_SHIP),false) # noship user bin
    LOCAL_C_INCLUDES += vendor/samsung/frameworks/audio/audiocoreutils
    LOCAL_SHARED_LIBRARIES += libsecaudiocoreutils
endif
else
# To use MCD specific definitions
LOCAL_CFLAGS += -DSUPPORT_MCD_FEATURE
endif

ifeq ($(ENABLE_R_FEATURE),true)
    LOCAL_CFLAGS += -DNO_USE_DEBUGFS
endif

# To use the headers from vndk-ext libs
LOCAL_CFLAGS += -D__ANDROID_VNDK_SEC__

ifeq ($(BOARD_USE_DIRECT_RCVSPK_PATH),true)
LOCAL_CFLAGS += -DSUPPORT_DIRECT_RCVSPK_PATH
endif

LOCAL_MODULE := libaudioproxy
LOCAL_MODULE_TAGS := optional

LOCAL_PROPRIETARY_MODULE := true

include $(BUILD_SHARED_LIBRARY)
