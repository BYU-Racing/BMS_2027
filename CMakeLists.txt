cmake_minimum_required(VERSION 3.22)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()
set(CMAKE_PROJECT_NAME BMSRework)
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)
project(${CMAKE_PROJECT_NAME} LANGUAGES C ASM)

# Blink-only firmware: no BMS application, FreeRTOS, USB or peripheral setup.
set(HAL_DIR Drivers/STM32G4xx_HAL_Driver)
add_executable(${CMAKE_PROJECT_NAME}
    Core/Src/main.c
    Core/Src/stm32g4xx_it.c
    Core/Src/system_stm32g4xx.c
    startup_stm32g474xx.s
    ${HAL_DIR}/Src/stm32g4xx_hal.c
    ${HAL_DIR}/Src/stm32g4xx_hal_cortex.c
    ${HAL_DIR}/Src/stm32g4xx_hal_gpio.c
    ${HAL_DIR}/Src/stm32g4xx_hal_rcc.c
    ${HAL_DIR}/Src/stm32g4xx_hal_rcc_ex.c
    ${HAL_DIR}/Src/stm32g4xx_hal_flash.c
    ${HAL_DIR}/Src/stm32g4xx_hal_flash_ex.c
    ${HAL_DIR}/Src/stm32g4xx_hal_pwr.c
    ${HAL_DIR}/Src/stm32g4xx_hal_pwr_ex.c
)
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    Core/Inc
    ${HAL_DIR}/Inc
    ${HAL_DIR}/Inc/Legacy
    Drivers/CMSIS/Device/ST/STM32G4xx/Include
    Drivers/CMSIS/Include
)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    USE_HAL_DRIVER STM32G474xx $<$<CONFIG:Debug>:DEBUG>
)

# Images for STM32CubeProgrammer; the application starts at 0x08000000.
add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
            ${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_PROJECT_NAME}.hex
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
            ${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_PROJECT_NAME}.bin
    BYPRODUCTS
        ${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_PROJECT_NAME}.hex
        ${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_PROJECT_NAME}.bin
    VERBATIM
)
