#
# Copyright (C) 2017-2018 Intel Corporation
#
# SPDX-License-Identifier: MIT
#

if(WIN32)
  set(IGDRCL_SRCS_tests_sharings_gl
    ${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt
    ${CMAKE_CURRENT_SOURCE_DIR}/gl_arb_sync_event_tests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/gl_create_from_texture_tests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/gl_reused_buffers_tests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/gl_sharing_enable_tests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/gl_sharing_tests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/gl_texture_tests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/gl_types_tests.cpp
  )
  target_sources(igdrcl_tests PRIVATE ${IGDRCL_SRCS_tests_sharings_gl})
endif()
