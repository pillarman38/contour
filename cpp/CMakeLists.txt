cmake_minimum_required(VERSION 3.18)
project(golf_sim_inference LANGUAGES CXX CUDA)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CUDA_STANDARD 17)

# ── Find Dependencies ────────────────────────────────────────────────────────
find_package(CUDA REQUIRED)
find_package(OpenCV REQUIRED)

# TensorRT – search common install locations
find_path(TENSORRT_INCLUDE_DIR NvInfer.h
    HINTS
        /usr/include/x86_64-linux-gnu
        /usr/local/TensorRT/include
        $ENV{TENSORRT_DIR}/include
        /usr/include
)

find_library(NVINFER_LIB nvinfer
    HINTS
        /usr/lib/x86_64-linux-gnu
        /usr/local/TensorRT/lib
        $ENV{TENSORRT_DIR}/lib
        /usr/lib
)

find_library(NVINFER_PLUGIN_LIB nvinfer_plugin
    HINTS
        /usr/lib/x86_64-linux-gnu
        /usr/local/TensorRT/lib
        $ENV{TENSORRT_DIR}/lib
        /usr/lib
)

if(NOT TENSORRT_INCLUDE_DIR OR NOT NVINFER_LIB)
    message(FATAL_ERROR
        "TensorRT not found. Set TENSORRT_DIR or install TensorRT.\n"
        "  TENSORRT_INCLUDE_DIR = ${TENSORRT_INCLUDE_DIR}\n"
        "  NVINFER_LIB          = ${NVINFER_LIB}")
endif()

message(STATUS "TensorRT include: ${TENSORRT_INCLUDE_DIR}")
message(STATUS "TensorRT lib:     ${NVINFER_LIB}")
message(STATUS "OpenCV version:   ${OpenCV_VERSION}")

# ── Sources ──────────────────────────────────────────────────────────────────
set(SOURCES
    src/main.cpp
    src/trt_engine.cpp
    src/frame_pipeline.cpp
    src/tracker.cpp
    src/unreal_sender.cpp
    src/putt_stats.cpp
    src/stats_api.cpp
)

# ── Executable ───────────────────────────────────────────────────────────────
add_executable(golf_sim ${SOURCES})

target_include_directories(golf_sim PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${TENSORRT_INCLUDE_DIR}
    ${CUDA_INCLUDE_DIRS}
    ${OpenCV_INCLUDE_DIRS}
)

target_link_libraries(golf_sim PRIVATE
    ${NVINFER_LIB}
    ${NVINFER_PLUGIN_LIB}
    ${CUDA_LIBRARIES}
    ${OpenCV_LIBS}
    cudart
    pthread
)

# ── Install ──────────────────────────────────────────────────────────────────
install(TARGETS golf_sim DESTINATION bin)
