cmake_minimum_required(VERSION 3.10)

project(video_anonymizer VERSION 1.0 LANGUAGES CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Get project root directory
get_filename_component(PROJECT_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR} ABSOLUTE)
get_filename_component(CPP_DIR ${PROJECT_ROOT_DIR}/.. ABSOLUTE)
get_filename_component(ROOT_DIR ${CPP_DIR}/.. ABSOLUTE)

# Optional configurations
option(BUILD_DEBUG "Build in debug mode" ON)

# Set build type
if(BUILD_DEBUG)
    set(CMAKE_BUILD_TYPE Debug)
else()
    set(CMAKE_BUILD_TYPE Release)
endif()

# External libraries for desktop build
# Add YOLOs-CPP include directories
include_directories(${CPP_DIR}/external/YOLOs-CPP/include)

# Add ONNX Runtime include directories
set(ONNXRUNTIME_DIR "${CPP_DIR}/external/YOLOs-CPP/onnxruntime-linux-x64-1.20.1")
include_directories(${ONNXRUNTIME_DIR}/include)
set(ONNXRUNTIME_LIB "${ONNXRUNTIME_DIR}/lib/libonnxruntime.so")

# For desktop, use conda environment OpenCV
set(OpenCV_DIR $ENV{CONDA_PREFIX}/lib/cmake/opencv4)
find_package(OpenCV REQUIRED)
if(NOT OpenCV_FOUND)
    message(FATAL_ERROR "OpenCV not found. Please ensure it's installed in your conda environment.")
endif()

set(DETECTOR_FILE ${PROJECT_ROOT_DIR}/yoloscpp_detector.cpp)
set(DETECTOR_HEADER ${PROJECT_ROOT_DIR}/yoloscpp_detector.h)


# Common source files from common directory
set(COMMON_SOURCES 
    ${CPP_DIR}/common/video_anonymizer.cpp
    ${CPP_DIR}/common/detector_factory.cpp
)

# Define sources for the library
add_library(anonymizer STATIC 
    ${COMMON_SOURCES}
    ${DETECTOR_FILE}
)

# Set up include directories
target_include_directories(anonymizer PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CPP_DIR}/common
    ${OpenCV_INCLUDE_DIRS}
)

# For desktop, link against OpenCV and ONNXRuntime
target_link_libraries(anonymizer PUBLIC ${OpenCV_LIBS})


# Link against ONNXRuntime
if(EXISTS "${ONNXRUNTIME_LIB}")
    message(STATUS "Using ONNXRuntime from ${ONNXRUNTIME_DIR}")
else()
    message(WARNING "ONNXRuntime library not found at ${ONNXRUNTIME_LIB}. Build may fail.")
endif()
target_link_libraries(anonymizer PUBLIC ${ONNXRUNTIME_LIB})

# Desktop applications
add_executable(video_anonymizer main.cpp)
target_link_libraries(video_anonymizer anonymizer)

# Install targets to bin directory
install(TARGETS video_anonymizer
        RUNTIME DESTINATION bin)

# Copy model files for desktop
if(EXISTS "${ROOT_DIR}/models/yolo11n-seg.onnx")
    file(COPY ${ROOT_DIR}/models/yolo11n-seg.onnx DESTINATION ${CMAKE_BINARY_DIR})
    message(STATUS "Copied yolo11n-seg.onnx model from models directory")
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/yolo11n-seg.onnx")
    file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/yolo11n-seg.onnx DESTINATION ${CMAKE_BINARY_DIR})
    message(STATUS "Copied yolo11n-seg.onnx model from current directory")
endif()

if(EXISTS "${ROOT_DIR}/models/coco.names")
    file(COPY ${ROOT_DIR}/models/coco.names DESTINATION ${CMAKE_BINARY_DIR})
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/coco.names")
    file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/coco.names DESTINATION ${CMAKE_BINARY_DIR})
endif()
