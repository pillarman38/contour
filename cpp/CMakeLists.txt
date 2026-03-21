cmake_minimum_required(VERSION 3.18)

# CUDA 13.x only officially supports VS 2019–2022. If using VS 2025 (MSVC 19.50+),
# nvcc must be allowed to use the unsupported host compiler (e.g. from Developer
# PowerShell for VS 2025). Prefer using "Developer PowerShell for VS 2022" when
# available so this flag is not needed.
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
  set(CMAKE_CUDA_FLAGS "${CMAKE_CUDA_FLAGS} -allow-unsupported-compiler")
endif()

project(golf_sim_inference LANGUAGES CXX CUDA)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CUDA_STANDARD 17)

# ── Find Dependencies ────────────────────────────────────────────────────────
find_package(CUDA REQUIRED)
find_package(OpenCV REQUIRED)

# TensorRT – TENSORRT_DIR (from -DTENSORRT_DIR= or $ENV{TENSORRT_DIR}) takes precedence
if(DEFINED TENSORRT_DIR)
    set(TENSORRT_HINT "${TENSORRT_DIR}")
elseif(DEFINED ENV{TENSORRT_DIR})
    set(TENSORRT_HINT "$ENV{TENSORRT_DIR}")
else()
    set(TENSORRT_HINT "")
endif()

find_path(TENSORRT_INCLUDE_DIR NvInfer.h
    HINTS
        ${TENSORRT_HINT}/include
        /usr/include/x86_64-linux-gnu
        /usr/local/TensorRT/include
        /usr/include
)

# TensorRT 10 uses nvinfer_10.lib, nvinfer_plugin_10.lib; TensorRT 8 uses nvinfer.lib, nvinfer_plugin.lib
if(TENSORRT_HINT)
    find_library(NVINFER_LIB nvinfer_10 nvinfer
        HINTS ${TENSORRT_HINT}/lib
        NO_DEFAULT_PATH
    )
    find_library(NVINFER_PLUGIN_LIB nvinfer_plugin_10 nvinfer_plugin
        HINTS ${TENSORRT_HINT}/lib
        NO_DEFAULT_PATH
    )
endif()
if(NOT NVINFER_LIB)
    find_library(NVINFER_LIB nvinfer
        HINTS
            ${TENSORRT_HINT}/lib
            /usr/lib/x86_64-linux-gnu
            /usr/local/TensorRT/lib
            /usr/lib
    )
endif()
if(NOT NVINFER_PLUGIN_LIB)
    find_library(NVINFER_PLUGIN_LIB nvinfer_plugin
        HINTS
            ${TENSORRT_HINT}/lib
            /usr/lib/x86_64-linux-gnu
            /usr/local/TensorRT/lib
            /usr/lib
    )
endif()

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
)
if(NOT WIN32)
    target_link_libraries(golf_sim PRIVATE cudart)
endif()

if (WIN32)
    target_link_libraries(golf_sim PRIVATE Ws2_32)
else()
    target_link_libraries(golf_sim PRIVATE pthread)
endif()

# ── Install ──────────────────────────────────────────────────────────────────
install(TARGETS golf_sim DESTINATION bin)
