cmake_minimum_required(VERSION 3.4.1)

add_definitions(-DRIRU_NEW_MODULE_API_VERSION=${RIRU_NEW_MODULE_API_VERSION})
add_definitions(-DRIRU_MODULE_VERSION_CODE=${RIRU_MODULE_VERSION_CODE})
add_definitions(-DRIRU_MODULE_VERSION_NAME=${RIRU_MODULE_VERSION_NAME})

add_library(riru_momohider
        SHARED
        main.cpp
        external/magisk/hide_policy.cpp
        external/xhook/xhook.c
        external/xhook/xh_core.c
        external/xhook/xh_elf.c
        external/xhook/xh_log.c
        external/xhook/xh_util.c
        external/xhook/xh_version.c
        )

find_library(log-lib log)

target_link_libraries(riru_momohider ${log-lib})
