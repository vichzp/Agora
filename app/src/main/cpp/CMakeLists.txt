cmake_minimum_required(VERSION 3.22)
project("agora_llama" LANGUAGES C CXX)

set(LLAMA_CPP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../../thirdparty/llama.cpp" CACHE PATH "llama.cpp source directory")
get_filename_component(LLAMA_CPP_DIR "${LLAMA_CPP_DIR}" REALPATH)

add_compile_options(
    -ffile-prefix-map=${CMAKE_SOURCE_DIR}=.
    -ffile-prefix-map=${LLAMA_CPP_DIR}=llama.cpp
    -g0
    -fno-ident
)

set(LLAMA_BUILD_NUMBER 0)
set(LLAMA_BUILD_COMMIT "unknown")
set(LLAMA_INSTALL_VERSION "0.0.0")
set(LLAMA_BUILD_COMMON ON CACHE BOOL "" FORCE)
set(LLAMA_BUILD_EXAMPLES OFF CACHE BOOL "")
set(LLAMA_BUILD_TESTS OFF CACHE BOOL "")
set(LLAMA_BUILD_SERVER OFF CACHE BOOL "")
set(BUILD_SHARED_LIBS ON CACHE BOOL "" FORCE)
set(GGML_NATIVE OFF CACHE BOOL "" FORCE)
set(GGML_BACKEND_DL ON CACHE BOOL "" FORCE)
set(GGML_CPU_ALL_VARIANTS ON CACHE BOOL "" FORCE)
# Arm's optimised Q4_0 kernels (dotprod/i8mm/SME2), picked per CPU variant at runtime.
set(GGML_CPU_KLEIDIAI ON CACHE BOOL "" FORCE)
set(GGML_VULKAN OFF CACHE BOOL "" FORCE)
set(GGML_OPENMP OFF CACHE BOOL "" FORCE)

# Adreno GPU backend as its own libggml-opencl.so. It links against the Khronos ICD loader
# only to resolve symbols; the loader is not packaged, so at runtime libOpenCL.so is the
# phone's own driver (declared with <uses-native-library> in the manifest). Phones without
# one simply do not list an OpenCL device.
option(AGORA_OPENCL "Build the OpenCL (Adreno) backend" ON)
if(AGORA_OPENCL AND ANDROID_ABI STREQUAL "arm64-v8a")
    include(FetchContent)
    FetchContent_Declare(opencl_headers
        URL https://github.com/KhronosGroup/OpenCL-Headers/archive/refs/tags/v2024.10.24.tar.gz
        URL_HASH SHA256=159f2a550592bae49859fee83d372acd152328fdf95c0dcd8b9409f8fad5db93
    )
    FetchContent_MakeAvailable(opencl_headers)
    set(OPENCL_ICD_LOADER_HEADERS_DIR "${opencl_headers_SOURCE_DIR}" CACHE PATH "" FORCE)
    set(OPENCL_ICD_LOADER_BUILD_TESTING OFF CACHE BOOL "" FORCE)
    set(ENABLE_OPENCL_LAYERINFO OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(opencl_icd_loader
        URL https://github.com/KhronosGroup/OpenCL-ICD-Loader/archive/refs/tags/v2024.10.24.tar.gz
        URL_HASH SHA256=95f2f0cda375b13d2760290df044ebea9c6ff954a7d7faa0867422442c9174dc
    )
    FetchContent_MakeAvailable(opencl_icd_loader)
    set(OpenCL_INCLUDE_DIR "${opencl_headers_SOURCE_DIR}" CACHE PATH "" FORCE)
    set(OpenCL_LIBRARY OpenCL CACHE STRING "" FORCE)  # the loader target above
    set(GGML_OPENCL ON CACHE BOOL "" FORCE)
    set(GGML_OPENCL_EMBED_KERNELS ON CACHE BOOL "" FORCE)
    set(GGML_OPENCL_USE_ADRENO_KERNELS ON CACHE BOOL "" FORCE)
endif()

add_subdirectory(${LLAMA_CPP_DIR} ${CMAKE_CURRENT_BINARY_DIR}/llama)
add_subdirectory(${LLAMA_CPP_DIR}/tools/mtmd ${CMAKE_CURRENT_BINARY_DIR}/mtmd)

if(ANDROID_ABI STREQUAL "arm64-v8a")
    set(AGORA_ANDROID_CPU_BACKENDS
        ggml-cpu-android_armv8.0_1
        ggml-cpu-android_armv8.2_1
        ggml-cpu-android_armv8.2_2
        ggml-cpu-android_armv8.6_1
        ggml-cpu-android_armv9.0_1
        ggml-cpu-android_armv9.2_1
        ggml-cpu-android_armv9.2_2
    )
elseif(ANDROID_ABI STREQUAL "x86_64")
    set(AGORA_ANDROID_CPU_BACKENDS ggml-cpu-x64)
else()
    message(FATAL_ERROR "Unsupported Android ABI: ${ANDROID_ABI}")
endif()
# Optional accelerator backends are dlopened like the CPU variants, so they ship beside them.
# The OpenCL ICD loader is only a link-time stand-in for the phone's driver: keep it out of
# the packaged directory (the Gradle packaging rules exclude it as well).
if(TARGET ggml-opencl)
    set_target_properties(ggml-opencl PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}
    )
endif()
if(TARGET OpenCL)
    set_target_properties(OpenCL PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/opencl-link-stub
    )
endif()
foreach(backend IN LISTS AGORA_ANDROID_CPU_BACKENDS)
    if(NOT TARGET ${backend})
        message(FATAL_ERROR "Required Android CPU backend target is missing: ${backend}")
    endif()
    set_target_properties(${backend} PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}
    )
endforeach()

foreach(cli llama-mtmd-cli llama-mtmd-debug
         llama-llava-cli llama-gemma3-cli llama-minicpmv-cli llama-qwen2vl-cli)
    if(TARGET ${cli})
        set_target_properties(${cli} PROPERTIES EXCLUDE_FROM_ALL TRUE)
    endif()
endforeach()
foreach(cli llama-llava-cli llama-gemma3-cli llama-minicpmv-cli llama-qwen2vl-cli)
    if(TARGET ${cli})
        target_include_directories(${cli} PRIVATE ${LLAMA_CPP_DIR}/common)
    endif()
endforeach()
if(TARGET llama-mtmd-cli)
    target_include_directories(llama-mtmd-cli PRIVATE ${LLAMA_CPP_DIR}/common ${LLAMA_CPP_DIR}/vendor)
endif()
if(TARGET llama-mtmd-debug)
    target_include_directories(llama-mtmd-debug PRIVATE ${LLAMA_CPP_DIR}/common ${LLAMA_CPP_DIR}/vendor)
endif()

target_compile_definitions(ggml-base PRIVATE GGML_COMMIT="unknown")

# Build llama JNI wrapper
add_library(agora_llama SHARED
    llama_jni.cpp
    llama_chat_jni.cpp
    llama_chat_callbacks.cpp
    llama_chat_template.cpp
    llama_chat_generation.cpp
)
target_link_libraries(agora_llama llama llama-common mtmd log)
target_include_directories(agora_llama PRIVATE ${LLAMA_CPP_DIR}/include ${LLAMA_CPP_DIR}/tools/mtmd)
target_link_options(agora_llama PRIVATE
    -Wl,--build-id=none -Wl,--hash-style=gnu -Wl,--icf=all -Wl,--threads=1
)

# ── objcopy for reproducible builds ──────────────────────
find_program(LLVM_OBJCOPY llvm-objcopy
    HINTS ${ANDROID_NDK}/toolchains/llvm/prebuilt/*/bin NO_DEFAULT_PATH
)

# ── Minimal JNI stub for proot ───────────────────────────
# System.loadLibrary("agora_proot") triggers APK extraction of native libs.
# Actual proot execution uses ProcessBuilder on libproot_exec.so (GNUmakefile-built).
add_library(agora_proot SHARED proot_jni.cpp)
target_link_options(agora_proot PRIVATE
    -Wl,--build-id=none -Wl,--hash-style=gnu -Wl,--icf=all
)

# ── Reproducible: strip .comment ─────────────────────────
if(DEFINED ENV{SOURCE_DATE_EPOCH})
    add_definitions(-DSOURCE_DATE_EPOCH=$ENV{SOURCE_DATE_EPOCH})
endif()
if(LLVM_OBJCOPY)
    add_custom_command(TARGET agora_llama POST_BUILD
        COMMAND ${LLVM_OBJCOPY} --remove-section .comment $<TARGET_FILE:agora_llama>
    )
    add_custom_command(TARGET agora_proot POST_BUILD
        COMMAND ${LLVM_OBJCOPY} --remove-section .comment $<TARGET_FILE:agora_proot>
    )
endif()
