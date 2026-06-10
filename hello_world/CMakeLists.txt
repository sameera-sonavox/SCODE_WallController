# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

set(DTS_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/dts)
list(APPEND DTC_OVERLAY_FILE "${CMAKE_CURRENT_SOURCE_DIR}/src/CustomOverlay/GPIOCtrl.overlay")
list(APPEND DTC_OVERLAY_FILE "${CMAKE_CURRENT_SOURCE_DIR}/src/CustomOverlay/PWMCtrl.overlay")
list(APPEND DTC_OVERLAY_FILE "${CMAKE_CURRENT_SOURCE_DIR}/src/CustomOverlay/DACCtrl.overlay")

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(hello_world)

set(MCUX_SDK_NG_DIR "${WEST_TOPDIR}/modules/hal/nxp/mcux/mcux-sdk-ng")

target_include_directories(app PRIVATE
                ${MCUX_SDK_NG_DIR}/drivers/lpadc)

target_sources(app PRIVATE 
                src/main.c 
                src/Lib/PWM/NXP_PWM_API.c 
                src/GPIO/Amp_GPIO.c 
                src/Lib/PWM/NXP_PWM_ProjDef.c
                src/Lib/CAN/NXP_CAN_API.c 
                src/Lib/CAN/NXP_CAN_ProjDef.c 
                src/CAN_Controller/CAN_Controller.c 
                src/Bootloader_Controller/Bootloader_Ctrl.c
                src/UART_CAN_Bridge/UART_CAN_Bridge.c 
                src/Lib/DAC/NXP_DAC_API.c 
                src/Lib/DAC/NXP_DAC_DMAConfig.c 
                src/Lib/CPULoad/NXP_CPU_LoadMon.c 
                src/Lib/ADC/NXP_ADC_API.c 
                src/Lib/ADC/NXP_ADC_LinkedList.c 
                src/ADC_Controller/ADC_Controller.c 
                src/Lib/ADC/NXP_ADC_CHMap.c 
                ${MCUX_SDK_NG_DIR}/drivers/lpadc/fsl_lpadc.c)
