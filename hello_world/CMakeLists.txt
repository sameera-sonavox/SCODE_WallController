# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

set(DTS_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/dts)
set(MCXA_LIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../MCXA_Lib/Lib" CACHE PATH "Path to the reusable MCXA library sources")
get_filename_component(MCXA_LIB_DIR "${MCXA_LIB_DIR}" ABSOLUTE)

if(NOT EXISTS "${MCXA_LIB_DIR}/PWM/NXP_PWM_API.c")
    message(FATAL_ERROR "MCXA_LIB_DIR is invalid: ${MCXA_LIB_DIR}")
endif()

set(DTC_OVERLAY_FILE
    "${CMAKE_CURRENT_SOURCE_DIR}/src/CustomOverlay/GPIOCtrl.overlay"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/CustomOverlay/PWMCtrl.overlay"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/CustomOverlay/DACCtrl.overlay"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/CustomOverlay/ADCCtrl.overlay"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/CustomOverlay/SPIPinCtrl.overlay"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/CustomOverlay/LVGLDisplayctrl.overlay"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/CustomOverlay/eQDCCtrl.overlay"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/CustomOverlay/SPI1_ExtFlashCtrl.overlay"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/CustomOverlay/FlashPartitions.overlay"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/CustomOverlay/ExtFlash_Ctrl.overlay"
    CACHE STRING "Application devicetree overlays" FORCE)
#list(APPEND DTC_OVERLAY_FILE "${CMAKE_CURRENT_SOURCE_DIR}/src/CustomOverlay/ExtFlash_Ctrl.overlay")

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(hello_world)

zephyr_compile_options("-ffile-prefix-map=${MCXA_LIB_DIR}=MCXA_LIB")

# Keep the generated external-IRQ vectors immediately after the Cortex-M
# exception vectors. Zephyr 4.4 registers both snippets with the same legacy
# linker sort key, which can omit irq-vector-table-section.ld from ROM_START.
zephyr_linker_sources(
    ROM_START
    SORT_KEY 0x0vectors_irq
    ${ZEPHYR_BASE}/include/zephyr/linker/irq-vector-table-section.ld)

set(MCUX_SDK_NG_DIR "${WEST_TOPDIR}/modules/hal/nxp/mcux/mcux-sdk-ng")

target_include_directories(app PRIVATE
                ${MCUX_SDK_NG_DIR}/drivers/lpadc)
target_include_directories(app PRIVATE
                ${MCUX_SDK_NG_DIR}/drivers/lpspi)
target_include_directories(app PRIVATE
                ${MCUX_SDK_NG_DIR}/drivers/eqdc)
target_include_directories(app PRIVATE
                ${MCXA_LIB_DIR})

target_sources(app PRIVATE 
                ${MCXA_LIB_DIR}/PWM/NXP_PWM_API.c 
                ${MCXA_LIB_DIR}/PWM/NXP_PWM_ProjDef.c
                ${MCXA_LIB_DIR}/CAN/NXP_CAN_API.c 
                ${MCXA_LIB_DIR}/CAN/NXP_CAN_ProjDef.c 
                ${MCXA_LIB_DIR}/DAC/NXP_DAC_API.c 
                ${MCXA_LIB_DIR}/DAC/NXP_DAC_DMAConfig.c 
                ${MCXA_LIB_DIR}/CPULoad/NXP_CPU_LoadMon.c 
                ${MCXA_LIB_DIR}/ADC/NXP_ADC_API.c 
                ${MCXA_LIB_DIR}/ADC/NXP_ADC_DMAConfig.c 
                ${MCXA_LIB_DIR}/ADC/NXP_ADC_LinkedList.c 
                ${MCXA_LIB_DIR}/ADC/NXP_ADC_CHMap.c 
                ${MCXA_LIB_DIR}/TrigSrcControl/TrigSrcControl.c 
                ${MCXA_LIB_DIR}/SPI/NXP_SPI_API.c 
                ${MCXA_LIB_DIR}/SPI/NXP_SPI_ConfigValidation.c 
                ${MCXA_LIB_DIR}/SPI/NXP_SPI_LinkedList.c 
                ${MCXA_LIB_DIR}/QDC/NXP_eQDC_API.c 
                src/main.c 
                src/CAN_Controller/CAN_Controller.c 
                src/Bootloader_Controller/Bootloader_Ctrl.c
                src/PC_UART_API/PC_UART_API.c
                src/PC_UART_API/PC_UART_FWBridgeService.c
                src/PC_UART_API/PC_UART_BulkFileService.c
                src/PC_UART_API/PC_UART_API_ValidateTest.c
                src/ADC_Controller/ADC_Controller.c 
                src/LVGL_Controller/LVGL_Display_Controller.c 
                src/LVGL_Controller/Screen_Parameters/LVGL_AudioSources_UIParam.c 
                src/LVGL_Controller/Screen_Parameters/LVGL_AudioSource_UIScreen.c 
                src/LVGL_Controller/FileSystem_Adapter/LVGL_ZephyrFS_Adapter.c 
                src/LVGL_Controller/Screens/Screen_Welcome/WelcomeScreen.c 
                src/LVGL_Controller/LVGL_TextAnimator/LVGL_TextAnimate.c 
                ${MCUX_SDK_NG_DIR}/drivers/lpadc/fsl_lpadc.c 
                ${MCUX_SDK_NG_DIR}/drivers/lpspi/fsl_lpspi.c 
                ${MCUX_SDK_NG_DIR}/drivers/eqdc/fsl_eqdc.c
                src/ExtFlash_Controller/ExtFlash_Controller.c 
                src/ExtFlash_Controller/SPIController/SPIController.c 
                src/ExtFlash_Controller/ExtFlashInitialSetup/ExtFlash_InitialSetupCtrl.c 
                src/ExtFlash_Controller/ExtFlashReadWrite/ExtFlash_ReadWriteControl.c
                src/ExtFlash_Controller/Flash_Memory_ValidateTest/Flash_Memory_ValidateTest.c 
                src/ExtFlash_Controller/ZephyrFlashMiddleWare/ZephyrFlashMiddleWare.c
                src/ExtFlash_Controller/LittleFsController/LittleFs_Controller.c)
