/*
 * Copyright (C) 2024 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdlib.h>

#include <hardware/sound_trigger.h>

static const struct sound_trigger_properties hw_properties = {
    "The LineageOS Project", // implementor
    "Dummy SoundTrigger HAL", // description
    1, // version
    { 0x1817de20, 0xfa3b, 0x11e5, 0xbef2, { 0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b } }, // uuid
    2, // max_sound_models
    1, // max_key_phrases
    1, // max_users
    RECOGNITION_MODE_VOICE_TRIGGER, // recognition_modes
    true, // capture_transition
    0, // max_buffer_ms
    false, // concurrent_capture
    false, // trigger_in_event
    0 // power_consumption_mw
};

static struct sound_trigger_properties_extended_1_3 hw_properties_1_3 = {
    {
        SOUND_TRIGGER_DEVICE_API_VERSION_1_3, // API version
        320, // rough total size
    },
    {
        "The LineageOS Project", // implementor
        "Dummy SoundTrigger HAL", // description
        1, // version
        { 0x1817de20, 0xfa3b, 0x11e5, 0xbef2, { 0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b } }, // uuid
        2, // max_sound_models
        1, // max_key_phrases
        1, // max_users
        RECOGNITION_MODE_VOICE_TRIGGER, // recognition_modes
        true, // capture_transition
        0, // max_buffer_ms
        false, // concurrent_capture
        false, // trigger_in_event
        0, // power_consumption_mw
    },
    "374236809, 4ac4bfe8-a4c9-3f5c-8f02-6db7c7123cb6", // supportedModelArch
    0 // audioCapabilities
};

static int stdev_get_properties(const struct sound_trigger_hw_device* dev,
                                struct sound_trigger_properties *properties) {
    if (properties == NULL)
        return -EINVAL;
    memcpy(properties, &hw_properties, sizeof(struct sound_trigger_properties));

    (void)dev;
    return 0;
}

static const struct sound_trigger_properties_header* stdev_get_properties_extended(
        const struct sound_trigger_hw_device *dev) {
    (void)dev;
    return (struct sound_trigger_properties_header*)&hw_properties_1_3;
}

static int stdev_unimplemented() {
    return -EINVAL;
}

static int stdev_close(hw_device_t *device) {
    free(device);
    return 0;
}

static int stdev_open(const hw_module_t* module, const char* name,
                      hw_device_t** device) {
    struct sound_trigger_hw_device *stdev;

    if (strcmp(name, SOUND_TRIGGER_HARDWARE_INTERFACE) != 0)
        return -EINVAL;

    stdev = calloc(1, sizeof(struct sound_trigger_hw_device));

    stdev->common.tag = HARDWARE_DEVICE_TAG;
    stdev->common.version = SOUND_TRIGGER_DEVICE_API_VERSION_1_3;
    stdev->common.module = (struct hw_module_t*)module;
    stdev->common.close = stdev_close;
    stdev->get_properties = stdev_get_properties;
    stdev->load_sound_model = stdev_unimplemented;
    stdev->unload_sound_model = stdev_unimplemented;
    stdev->start_recognition = stdev_unimplemented;
    stdev->stop_recognition = stdev_unimplemented;
    stdev->stop_all_recognitions = stdev_unimplemented;
    stdev->get_model_state = stdev_unimplemented;
    stdev->set_parameter = stdev_unimplemented;
    stdev->get_parameter = stdev_unimplemented;
    stdev->query_parameter = stdev_unimplemented;
    stdev->get_properties_extended = stdev_get_properties_extended;
    stdev->start_recognition_extended = stdev_unimplemented;

    *device = &stdev->common;

    return 0;
};

static struct hw_module_methods_t hal_module_methods = {
    .open = stdev_open,
};

struct sound_trigger_module HAL_MODULE_INFO_SYM = {
    .common = {
        .tag = HARDWARE_MODULE_TAG,
        .module_api_version = SOUND_TRIGGER_MODULE_API_VERSION_1_0,
        .hal_api_version = HARDWARE_HAL_API_VERSION,
        .id = SOUND_TRIGGER_HARDWARE_MODULE_ID,
        .name = "Dummy SoundTrigger HAL",
        .author = "The LineageOS Project",
        .methods = &hal_module_methods,
    },
};
