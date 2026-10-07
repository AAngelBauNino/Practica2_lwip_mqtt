# Copyright 2024-2025 NXP
#
# SPDX-License-Identifier: BSD-3-Clause

cmake_minimum_required(VERSION 3.22.0)

include(${SdkRootDirPath}/cmake/extension/mcux.cmake)

project(lwip_mqtt_freertos LANGUAGES C CXX ASM PROJECT_BOARD_PORT_PATH ${board_root}/${board}/lwip_examples/lwip_mqtt/freertos)

include(${SdkRootDirPath}/CMakeLists.txt)
mcux_add_include(
    BASE_PATH ${CMAKE_CURRENT_LIST_DIR}
    INCLUDES .
)

mcux_add_source(
    BASE_PATH ${CMAKE_CURRENT_LIST_DIR}
    SOURCES
        lwip_mqtt_freertos.c
        mqtt_freertos.c
        mqtt_freertos.h
        practica2_config.h
)

mcux_add_macro(
    CC "-DUSE_RTOS=1\
       -DPRINTF_ADVANCED_ENABLE=1"
)

mcux_add_macro(
    TOOLCHAINS armgcc
    CC "-D_POSIX_SOURCE"
)

mcux_add_linker_symbol(
    SYMBOLS "__stack_size__=1000\
             __heap_size__=25600"
)

include(${SdkRootDirPath}/${board_root}/${board}/lwip_examples/lwip_mqtt/freertos/reconfig.cmake OPTIONAL)

mcux_convert_binary(BINARY ${APPLICATION_BINARY_DIR}/${MCUX_SDK_PROJECT_NAME}.bin)