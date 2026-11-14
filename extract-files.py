#!/usr/bin/env -S PYTHONPATH=../../../tools/extract-utils python3
#
# SPDX-FileCopyrightText: The LineageOS Project
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
    'vendor/bin/hw/gpsd': blob_fixup()
        .binary_regex_replace(b'libcrypto.so', b'libcryptx.so')
        .binary_regex_replace(b'libssl.so', b'libssx.so'),
    'vendor/lib64/libsec-ril-impl.so': blob_fixup()
        # Change fallback value of ro.build.version.oneui in SimManager (3 matches)
        .sig_replace('00 A4 2E 91 E1 03 1F 2A', '00 A4 2E 91 81 58 9D 52')
        .sig_replace('00 A4 2E 91 E1 03 1F 2A', '00 A4 2E 91 81 58 9D 52')
        .sig_replace('00 A4 2E 91 E1 03 1F 2A', '00 A4 2E 91 81 58 9D 52'),
    'vendor/lib64/libssx.so': blob_fixup()
        .replace_needed('libcrypto.so', 'libcryptx.so'),
    (
        'vendor/lib64/libkeymaster_helper.so',
        'vendor/lib64/libskeymaster4device.so',
    ) : blob_fixup()
        .replace_needed('libcrypto.so', 'libcryptx.so')
        .add_needed('libshim_crypto.so'),
    (
        'vendor/lib/sensors.grip.so',
        'vendor/lib/sensors.sensorhub.so',
        'vendor/lib64/sensors.grip.so',
        'vendor/lib64/sensors.sensorhub.so',
    ) : blob_fixup()
        .add_needed('libutils-v32.so')
        .binary_regex_replace(b'_ZN7android6Thread3runEPKcim', b'_ZN7utils326Thread3runEPKcim'),
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
