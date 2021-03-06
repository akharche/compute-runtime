/*
 * Copyright (C) 2018 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "runtime/os_interface/linux/allocator_helper.h"

namespace OCLRT {
size_t getSizeToMap() {
    return static_cast<size_t>(1 * 1024 * 1024u);
}
} // namespace OCLRT
