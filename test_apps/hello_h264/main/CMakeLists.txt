# test_apps/hello_h264/main/CMakeLists.txt

idf_component_include(sub0h264)
idf_component_include(log)
idf_component_include(esp_hw_support)

add_library(${COMPONENT_TARGET} STATIC hello_main.cpp)
target_link_libraries(${COMPONENT_TARGET} PRIVATE
    idf::sub0h264
    idf::log
    idf::esp_hw_support
)
