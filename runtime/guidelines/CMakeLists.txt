#
# Copyright (C) 2018 Intel Corporation
#
# SPDX-License-Identifier: MIT
#

set(RUNTIME_CODING_GUIDELINES
  ${CMAKE_CURRENT_SOURCE_DIR}/../../documentation/GUIDELINES.md
)
target_sources(${NEO_STATIC_LIB_NAME} PRIVATE ${RUNTIME_CODING_GUIDELINES})
set_property(GLOBAL PROPERTY RUNTIME_CODING_GUIDELINES ${RUNTIME_CODING_GUIDELINES})
