cmake_minimum_required(VERSION 3.4.1)

add_definitions(-DRIRU_NEW_MODULE_API_VERSION=${RIRU_NEW_MODULE_API_VERSION})
add_definitions(-DRIRU_MODULE_VERSION_CODE=${RIRU_MODULE_VERSION_CODE})
add_definitions(-DRIRU_MODULE_VERSION_NAME=${RIRU_MODULE_VERSION_NAME})

add_library(riru_isolatedmagiskhider SHARED main.cpp external/magisk/hide_policy.cpp)

find_library(log-lib log)

target_link_libraries(riru_isolatedmagiskhider ${log-lib})
