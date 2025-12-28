add_library(usermod_st7701 INTERFACE)

target_sources(usermod_st7701 INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/st7701.c)

target_include_directories(usermod_st7701 INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}
    $ENV{IDF_PATH}/components/esp_lcd/include
    $ENV{IDF_PATH}/components/esp_lcd/rgb/include
)

target_link_libraries(usermod INTERFACE usermod_st7701)
