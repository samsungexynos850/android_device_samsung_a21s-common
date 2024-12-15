/*
 * Copyright (C) 2024 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdint.h>
#include <ui/GraphicBufferMapper.h>
#include <ui/Rect.h>
#include <utils/Errors.h>
using android::Rect;
using android::status_t;
extern "C" {
status_t _ZN7android19GraphicBufferMapper9lockAsyncEPK13native_handlemmRKNS_4RectEPPviPiS9_(
        void* thisptr, buffer_handle_t handle, uint64_t producerUsage, uint64_t consumerUsage,
        const Rect& bounds, void** vaddr, int fenceFd, int32_t* /*outBytesPerPixel*/,
        int32_t* /*outBytesPerStride*/) {
    auto* gpm = static_cast<android::GraphicBufferMapper*>(thisptr);
    return gpm->lockAsync(handle, producerUsage, consumerUsage, bounds, vaddr, fenceFd);
}
}
