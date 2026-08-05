# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

set(DTS_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/dts)
set(MCXA_LIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../MCXA_Lib/Lib" CACHE PATH "Path to the reusable MCXA library sources")
get_filename_component(MCXA_LIB_DIR "${MCXA_LIB_DIR}" ABSOLUTE)

if(NOT EXISTS "${MCXA_LIB_DIR}/PWM/NXP_PWM_API.c")
    message(FATAL_ERROR "MCXA_LIB_DIR is invalid: ${MCXA_LIB_DIR}")
endif()

list(APPEND DTC_OVERLAY_FILE "${CMAKE_CURRENT_SOURCE_DIR}/src/CustomOverlay/GPIOCtrl.overlay")
list(APPEND DTC_OVERLAY_FILE "${CMAKE_CURRENT_SOURCE_DIR}/src/CustomOverlay/PWMCtrl.overlay")
list(APPEND DTC_OVERLAY_FILE "${CMAKE_CURRENT_SOURCE_DIR}/src/CustomOverlay/DACCtrl.overlay")
list(APPEND DTC_OVERLAY_FILE "${CMAKE_CURRENT_SOURCE_DIR}/src/CustomOverlay/ADCCtrl.overlay")
# list(APPEND DTC_OVERLAY_FILE "${CMAKE_CURRENT_SOURCE_DIR}/src/CustomOverlay/SPIPinCtrl.overlay")
list(APPEND DTC_OVERLAY_FILE "${CMAKE_CURRENT_SOURCE_DIR}/src/CustomOverlay/LVGLDisplayctrl.overlay")
list(APPEND DTC_OVERLAY_FILE "${CMAKE_CURRENT_SOURCE_DIR}/src/CustomOverlay/eQDCCtrl.overlay")

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(hello_world)

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
                src/UART_CAN_Bridge/UART_CAN_Bridge.c 
                src/ADC_Controller/ADC_Controller.c 
                src/SPI_Controller/SPI_Controller_Master.c 
                src/SPI_Controller/SPI_Controller_Slave.c 
                src/LVGL_Controller/LVGL_Display_Controller.c 
                src/LVGL_Controller/Screen_Parameters/LVGL_AudioSources_UIParam.c 
                src/LVGL_Controller/Screen_Parameters/LVGL_AudioSource_UIScreen.c
                ${MCUX_SDK_NG_DIR}/drivers/lpadc/fsl_lpadc.c 
                ${MCUX_SDK_NG_DIR}/drivers/lpspi/fsl_lpspi.c 
                ${MCUX_SDK_NG_DIR}/drivers/eqdc/fsl_eqdc.c
                src/ExtFlash_Controller/ExtFlash_Controller.c)
