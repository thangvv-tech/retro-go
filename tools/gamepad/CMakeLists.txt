cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(gamepad)

# Generate merged all-in-one image for 1-step flashing at offset 0x0
# Automatically adapts to any ESP-IDF target chip (esp32c3, esp32s3, esp32...)
add_custom_target(merged_img ALL
    COMMAND esptool.py --chip ${IDF_TARGET} merge_bin -o ${CMAKE_BINARY_DIR}/gamepad-${IDF_TARGET}.img
            --flash_mode dio --flash_size 4MB
            0x0 ${CMAKE_BINARY_DIR}/bootloader/bootloader.bin
            0x8000 ${CMAKE_BINARY_DIR}/partition_table/partition-table.bin
            0x10000 ${CMAKE_BINARY_DIR}/${CMAKE_PROJECT_NAME}.bin
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_BINARY_DIR}/gamepad-${IDF_TARGET}.img ${CMAKE_BINARY_DIR}/gamepad.img
    COMMENT "Generated all-in-one flash image: build/gamepad-${IDF_TARGET}.img and build/gamepad.img (flash at offset 0x0)"
)
add_dependencies(merged_img gen_project_binary)
