#!/usr/bin/env -S PYTHONPATH=../../../tools/extract-utils python3
#
# SPDX-FileCopyrightText: 2025 The LineageOS Project
# SPDX-License-Identifier: Apache-2.0
#

from extract_utils.fixups_blob import (
    blob_fixup,
    blob_fixups_user_type,
)
from extract_utils.fixups_lib import (
    lib_fixup_vendorcompat,
    lib_fixups_user_type,
    libs_proto_3_9_1,
)
from extract_utils.main import (
    ExtractUtils,
    ExtractUtilsModule,
)

namespace_imports = [
    'device/samsung/a21s-common',
    'hardware/samsung',
    'hardware/samsung_slsi-linaro/exynos',
    'hardware/samsung_slsi-linaro/graphics',
    'hardware/samsung_slsi-linaro/exynos/gralloc/gralloc3',
]

def lib_fixup_vendor_suffix(lib: str, partition: str, *args, **kwargs):
    return f'{lib}_{partition}' if partition == 'vendor' else None

lib_fixups: lib_fixups_user_type = {
    libs_proto_3_9_1: lib_fixup_vendorcompat,
    (
        'libuuid',
    ) : lib_fixup_vendor_suffix
} # fmt: skip

blob_fixups: blob_fixups_user_type = {
    'vendor/lib64/libsec-ril.so': blob_fixup()
	.sig_replace('80 0E 40 F9 E1 03 16 AA 82 0C 80 52 E3 03 15 AA',
            '80 0E 40 F9 E1 03 16 AA 82 0C 80 52 08 00 80 D2'),
    'vendor/lib64/libcrypto-tm.so': blob_fixup()
        .replace_needed('libssl.so', 'libssl-tm.so'),
    (
    'vendor/lib/libsensorlistener.so',
    'vendor/lib64/libsensorlistener.so',
    ) : blob_fixup()
        .add_needed('libshim_sensorndkbridge.so'),
    (
        'vendor/lib64/libkeymaster_helper.so',
        'vendor/lib64/libskeymaster4device.so',
    ) : blob_fixup()
        .replace_needed('libcrypto.so', 'libcrypto-tm.so')
        .add_needed('libshim_crypto.so'),
    (
        'vendor/lib/sensors.grip.so',
        'vendor/lib64/sensors.grip.so',
        'vendor/lib/sensors.sensorhub.so',
        'vendor/lib64/sensors.sensorhub.so',
    ) : blob_fixup()
        .add_needed('libutils-v32.so')
        .binary_regex_replace(b'_ZN7android6Thread3runEPKcim', b'_ZN7utils326Thread3runEPKcim'),
    'vendor/lib/lib_SoundAlive_play_plus_ver400.so': blob_fixup()
        .add_needed('liblog.so'),
    (
    'vendor/lib/libaudioparamupdate.so',
    'vendor/lib/libaboxpcmdump.so',
    'vendor/lib/libaudioproxy.so',
    'vendor/lib/hw/audio.primary.exynos850.so',
    ) : blob_fixup()
        .replace_needed('libaudioroute.so','libaudioroute_a21s.so'),
    'vendor/lib/libaudioproxy.so': blob_fixup()
        .add_needed('libshim_audioproxy.so'),
}  # fmt: skip

module = ExtractUtilsModule(
    'a21s-common',
    'samsung',
    namespace_imports=namespace_imports,
    blob_fixups=blob_fixups,
    lib_fixups=lib_fixups,
)

if __name__ == '__main__':
    utils = ExtractUtils.device(module)
    utils.run()
