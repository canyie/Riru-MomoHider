cmake_minimum_required(VERSION 3.4.1)

add_definitions(-DRIRU_NEW_MODULE_API_VERSION=${RIRU_NEW_MODULE_API_VERSION})
add_definitions(-DRIRU_MODULE_VERSION_CODE=${RIRU_MODULE_VERSION_CODE})
add_definitions(-DRIRU_MODULE_VERSION_NAME="${RIRU_MODULE_VERSION_NAME}")

set(C_FLAGS "-Wall -Wextra -fvisibility=hidden -fvisibility-inlines-hidden -fno-exceptions -fno-rtti -flto=thin")
set(LINKER_FLAGS "-fuse-ld=lld -flto=thin -Wl,--unresolved-symbols=ignore-in-shared-libs")

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${C_FLAGS}")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${C_FLAGS}")

set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} ${LINKER_FLAGS}")
set(CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS} ${LINKER_FLAGS}")

add_library(momohider
        SHARED
        main.cpp
        external/magisk/hide_policy.cpp)

find_library(log-lib log)

find_package(xhook REQUIRED CONFIG)
find_package(cxx REQUIRED CONFIG)

target_link_libraries(momohider ${log-lib} cxx::cxx xhook::xhook)
