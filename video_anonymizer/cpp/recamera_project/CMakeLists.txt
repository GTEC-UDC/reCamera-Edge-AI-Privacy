# This file is an adaptation of the different CMakeLists.txt files from the sscma-example-sg200x repository.
# All this code is licensed under the Apache License 2.0 included in the sscma-example-sg200x repository: https://github.com/Seeed-Studio/sscma-example-sg200x/blob/main/LICENSE

cmake_minimum_required(VERSION 3.5.0)

project(anonymize_recamera C CXX)

# Get project paths
get_filename_component(PROJECT_DIR ${CMAKE_CURRENT_LIST_DIR} ABSOLUTE)
get_filename_component(CPP_DIR ${PROJECT_DIR}/.. ABSOLUTE)
get_filename_component(ROOT_DIR ${CPP_DIR}/.. ABSOLUTE)

message(STATUS "PROJECT_DIR: ${PROJECT_DIR}")

get_filename_component(ROOT_DIR ${CMAKE_CURRENT_LIST_DIR} ABSOLUTE)

message(STATUS "ROOT_DIR: ${ROOT_DIR}")

include(${CMAKE_CURRENT_LIST_DIR}/cmake/toolchain-riscv64-linux-musl-x86_64.cmake)

set(SOPHGO_PLATFORM ON)

include(${ROOT_DIR}/cmake/macro.cmake)

set(CMAKE_CXX_STANDARD 17)

if("${SG200X_SDK_PATH}" STREQUAL "")
    message(FATAL_ERROR "SG200X_SDK_PATH environment variable must be set to the sg2002_recamera_emmc output directory. Example: export SG200X_SDK_PATH=/path/to/sg2002_recamera_emmc/")
endif()

set(COMPILER_PATH "${SG200X_SDK_PATH}/../../host-tools/gcc/riscv64-linux-musl-x86_64/bin")

set(CMAKE_C_COMPILER "${COMPILER_PATH}/riscv64-unknown-linux-musl-gcc")
set(CMAKE_CXX_COMPILER "${COMPILER_PATH}/riscv64-unknown-linux-musl-g++")

# Check if the compiler is available
if(NOT EXISTS "${CMAKE_C_COMPILER}" OR NOT EXISTS "${CMAKE_CXX_COMPILER}")
    message(FATAL_ERROR "Compiler ${CMAKE_C_COMPILER} or ${CMAKE_CXX_COMPILER} not found")
endif()

message(STATUS "SG200X_SDK_PATH: ${SG200X_SDK_PATH}")

if("${SYSROOT}" STREQUAL "")
    set(SYSROOT ${SG200X_SDK_PATH}/buildroot-2021.05/output/cvitek_CV181X_musl_riscv64/host/riscv64-buildroot-linux-musl/sysroot)
endif()

message(STATUS "SYSROOT: ${SYSROOT}")

include_directories("${SYSROOT}/usr/include")
link_directories("${SYSROOT}/usr/lib")

include_directories("${SG200X_SDK_PATH}/install/soc_sg2002_recamera_emmc/tpu_musl_riscv64/cvitek_tpu_sdk/include")
link_directories("${SG200X_SDK_PATH}/install/soc_sg2002_recamera_emmc/tpu_musl_riscv64/cvitek_tpu_sdk/lib")
link_directories("${SG200X_SDK_PATH}/install/soc_sg2002_recamera_emmc/rootfs/mnt/system/lib")

# rtsp
include_directories("${SG200X_SDK_PATH}/install/soc_sg2002_recamera_emmc/tpu_musl_riscv64/cvitek_ai_sdk/sample/3rd/rtsp/include/cvi_rtsp")
link_directories("${SG200X_SDK_PATH}/install/soc_sg2002_recamera_emmc/tpu_musl_riscv64/cvitek_ai_sdk/sample/3rd/rtsp/lib")

include_directories("${SG200X_SDK_PATH}/install/soc_sg2002_recamera_emmc/tpu_musl_riscv64/cvitek_tpu_sdk/include/liveMedia")
include_directories("${SG200X_SDK_PATH}/install/soc_sg2002_recamera_emmc/tpu_musl_riscv64/cvitek_tpu_sdk/include/groupsock")
include_directories("${SG200X_SDK_PATH}/install/soc_sg2002_recamera_emmc/tpu_musl_riscv64/cvitek_tpu_sdk/include/UsageEnvironment")
include_directories("${SG200X_SDK_PATH}/install/soc_sg2002_recamera_emmc/tpu_musl_riscv64/cvitek_tpu_sdk/include/BasicUsageEnvironment")

# Add include directories for TransportRTSP

set(COMPONENTS_DIR ${CPP_DIR}/external/sscma-example-sg200x/components/)

include_directories(${COMPONENTS_DIR}/sscma-micro/include)
include_directories(${COMPONENTS_DIR}/sscma-micro/porting/sophgo/sg200x/)
include_directories(${COMPONENTS_DIR}/sscma-micro/core)
include_directories(${COMPONENTS_DIR}/sscma-micro/transport)

file(GLOB COMPONENTS LIST_DIRECTORIES true ${COMPONENTS_DIR}/*)


# Here we define our targets
set(RECAMERA_SRC
    ${CMAKE_CURRENT_LIST_DIR}/recamera_main.cpp
    ${CMAKE_CURRENT_LIST_DIR}/recamera_detector.cpp
    ${CMAKE_CURRENT_LIST_DIR}/frame_capturer.cpp
    ${CMAKE_CURRENT_LIST_DIR}/cvi_h264_streamer.cpp
    ${CMAKE_CURRENT_LIST_DIR}/cvi_system.cpp
    ${CPP_DIR}/common/video_anonymizer.cpp
    ${CPP_DIR}/common/detector_factory.cpp
)

set(incs
    ${CMAKE_CURRENT_LIST_DIR}
)

# Define preprocessor definition TARGET_RECAMERA
add_definitions(-DTARGET_RECAMERA -DMA_USE_TRANSPORT_RTSP)

# Make sure to link against transport libraries
component_register(
    COMPONENT_NAME main
    SRCS ${RECAMERA_SRC}
    INCLUDE_DIRS ${incs}
    #PRIVATE_REQUIREDS sscma-micro cvi_rtsp sophgo 
    PRIVATE_REQUIREDS sscma-micro cvi_rtsp pthread
                    ae af atomic awb cvi_bin cvi_bin_isp gdc isp isp_algo sys venc vpss vi vo cvi_ispd2 json-c sns_ov5647 sns_sc530ai_2l
    REQUIREDS opencv_core opencv_imgcodecs opencv_imgproc 
)


set(SKIP_COMPONENTS "")

foreach(component IN LISTS COMPONENTS)
    get_filename_component(component_name ${component} NAME)
    message(STATUS "component: ${component_name}")

    if(EXISTS "${component}/CMakeLists.txt" AND component_name IN_LIST REQUIREDS)
        include("${component}/CMakeLists.txt")
    else()
        list(APPEND SKIP_COMPONENTS ${component})
    endif()

    foreach(component IN LISTS SKIP_COMPONENTS)
        get_filename_component(component_name ${component} NAME)

        if(EXISTS "${component}/CMakeLists.txt" AND component_name IN_LIST REQUIREDS)
            include("${component}/CMakeLists.txt")
        endif()
    endforeach()
endforeach()


set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")

include(${ROOT_DIR}/cmake/build.cmake)


# copy the test_on_recamera.sh to the build directory
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/test_on_recamera.sh
    ${CMAKE_BINARY_DIR}/test_on_recamera.sh
    COPYONLY
)
