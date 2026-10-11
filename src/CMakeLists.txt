# LoopMaxPro - sources of the ESP-IDF builds.
# The other frameworks select the same folders with build_src_filter in platformio.ini.
file(GLOB_RECURSE lmp_sources
  ${CMAKE_CURRENT_LIST_DIR}/main.cpp
  ${CMAKE_CURRENT_LIST_DIR}/app/*.cpp
  ${CMAKE_CURRENT_LIST_DIR}/base/*.cpp
  ${CMAKE_CURRENT_LIST_DIR}/kernel/*.cpp
  ${CMAKE_CURRENT_LIST_DIR}/services/*.cpp
  ${CMAKE_CURRENT_LIST_DIR}/sdk/*.cpp
  ${CMAKE_CURRENT_LIST_DIR}/modules/*.cpp
  ${CMAKE_CURRENT_LIST_DIR}/drivers/*.cpp
  ${CMAKE_CURRENT_LIST_DIR}/hal/api/*.cpp
  ${CMAKE_CURRENT_LIST_DIR}/hal/common/*.cpp
  ${CMAKE_CURRENT_LIST_DIR}/hal/esp32/*.cpp)

# The private modules, when the project has them: the same as in the Arduino builds.
if(EXISTS ${CMAKE_CURRENT_LIST_DIR}/../modules_private)
  # The sources of a module, also in folders inside its src. A firmware of
  # its own inside a module has no src: it is never taken.
  file(GLOB_RECURSE lmp_private_sources
    ${CMAKE_CURRENT_LIST_DIR}/../modules_private/*/src/*.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../modules_private/*/src/*/*.cpp)
  list(APPEND lmp_sources ${lmp_private_sources})
endif()

idf_component_register(
  SRCS ${lmp_sources}
  INCLUDE_DIRS "." "../include"
  PRIV_REQUIRES
    app_update
    esp-tls
    esp_adc
    esp_driver_gpio
    esp_driver_i2c
    esp_driver_ledc
    esp_driver_rmt
    esp_driver_spi
    esp_driver_uart
    esp_driver_usb_serial_jtag
    esp_event
    esp_http_client
    esp_http_server
    esp_hw_support
    esp_netif
    esp_system
    esp_timer
    esp_wifi
    littlefs
    lwip
    mbedtls
    mdns
    mqtt
    nvs_flash
    esp_partition
    spi_flash)
