# components/sub0h264/CMakeLists.txt
#
# H.264 Baseline+High decoder library for ESP32-P4 and desktop.
# Dual build path: ESP-IDF component (cmakev2) or standalone static lib.

# Platform source selection
if(DEFINED IDF_TARGET)
    set(_platform_src platform/riscv_pie/sub0h264_platform_pie.cpp)
elseif(SUB0H264_PLATFORM STREQUAL "x86")
    set(_platform_src platform/x86/sub0h264_platform_x86.cpp)
else()
    set(_platform_src platform/generic/sub0h264_platform_generic.cpp)
endif()

set(_core_sources
    src/sub0h264.cpp
    ${_platform_src}
)

if(DEFINED IDF_TARGET)
    # -- ESP-IDF component path (cmakev2) ------------------------------------------
    idf_component_include(log)

    add_library(${COMPONENT_TARGET} STATIC ${_core_sources})
    target_include_directories(${COMPONENT_TARGET} PUBLIC include)
    target_link_libraries(${COMPONENT_TARGET} PRIVATE idf::log)
else()
    # -- Desktop / standalone path -------------------------------------------------
    add_library(sub0h264 STATIC ${_core_sources})

    target_compile_features(sub0h264 PUBLIC cxx_std_17)

    target_compile_options(sub0h264 PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/W4 /Zc:preprocessor>
        $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Wpedantic>
    )

    target_include_directories(sub0h264
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/include>
            $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    )
endif()
