# LittleFS from the Arduino core's own prebuilt library (built with IDF 4.4.7 like the board image), so the file
# system is byte for byte the board's. The core's LittleFS library links it when this component is present.
set(sdk "$ENV{HOME}/.platformio/packages/framework-arduinoespressif32/tools/sdk/esp32")
idf_component_register(INCLUDE_DIRS "${sdk}/include/esp_littlefs/include" REQUIRES spi_flash vfs)
target_link_libraries(${COMPONENT_LIB} INTERFACE "${sdk}/lib/libesp_littlefs.a")
