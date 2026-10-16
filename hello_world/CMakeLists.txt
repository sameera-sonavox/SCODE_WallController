# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

set(DTS_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/dts)
set(MCXA_LIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../MCXA_Lib/Lib" CACHE PATH "Path to the reusable MCXA library sources")
get_filename_component(MCXA_LIB_DIR "${MCXA_LIB_DIR}" ABSOLUTE)

if(NOT EXISTS "${MCXA_LIB_DIR}/cmake/MCXAComponents.cmake")
    message(FATAL_ERROR
        "MCXA_LIB_DIR is invalid: ${MCXA_LIB_DIR}"
    )
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

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(hello_world)

set(SELECTED_AUDIO_GUI_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/src/LVGL_Controller/Screens/SelectedAudioSource/GUI_Guider_Generated")
set(SELECTED_AUDIO_GUI_SCREEN_SOURCE
    "${SELECTED_AUDIO_GUI_DIR}/generated/screens/gg_Home_revised.c")
set(SELECTED_AUDIO_GUI_EVENT_SOURCE
    "${SELECTED_AUDIO_GUI_DIR}/generated/events/gg_event_Home_revised.c")

zephyr_compile_options("-ffile-prefix-map=${MCXA_LIB_DIR}=MCXA_LIB")

# Keep the generated external-IRQ vectors immediately after the Cortex-M
# exception vectors. Zephyr 4.4 registers both snippets with the same legacy
# linker sort key, which can omit irq-vector-table-section.ld from ROM_START.
zephyr_linker_sources(
    ROM_START
    SORT_KEY 0x0vectors_irq
    ${ZEPHYR_BASE}/include/zephyr/linker/irq-vector-table-section.ld)

include("${MCXA_LIB_DIR}/cmake/MCXAComponents.cmake")

#This is required to add specific library classes. Refer to 'APIList.h' in library for all the keywords
#required to enable specific library classes. For example, to enable CAN, SPI, QDC and AK4148, the following is required.
mcxa_target_components(
    TARGET app
    CONFIG_DIR "${CMAKE_CURRENT_SOURCE_DIR}/config/mcxa"
    COMPONENTS
        CAN
        SPI
        QDC
        AK4148
)

target_sources(app PRIVATE  
                src/main.c 

                src/CAN_Controller/CAN_Controller.c 
                src/Bootloader_Controller/Bootloader_Ctrl.c 
                
                src/WallController/WallController_StateMachine/WallController_StateMachine.c
                src/WallController/WallController_Comm_Manager/WallController_Comm_Manager.c
                src/LVGL_Controller/UI_InputController/UI_InputController.c
                src/LVGL_Controller/UI_ScreenManager/UI_ScreenManager.c                
                src/LVGL_Controller/LVGL_Display_Controller.c 
                src/LVGL_Controller/FileSystem_Adapter/LVGL_ZephyrFS_Adapter.c  
                src/LVGL_Controller/LVGL_TextAnimator/LVGL_TextAnimate.c 

                #Welcome Screen
                src/LVGL_Controller/Screens/Screen_Welcome/WelcomeScreen.c
                src/LVGL_Controller/Screens/Screen_Welcome/GUI_Guider_Generated/generated/screens/gg_Sleep_Screen.c
                src/LVGL_Controller/Screens/Screen_Welcome/GUI_Guider_Generated/generated/events/gg_event_Sleep_Screen.c
                #Selected Audio Source
                src/LVGL_Controller/Screens/SelectedAudioSource/SelectedAudioSrcCtrl.c
                ${SELECTED_AUDIO_GUI_SCREEN_SOURCE}
                ${SELECTED_AUDIO_GUI_EVENT_SOURCE}
                ${SELECTED_AUDIO_GUI_DIR}/generated/assets/fonts/lv_font_montserratMedium_45.c
                ${SELECTED_AUDIO_GUI_DIR}/generated/assets/fonts/lv_font_Roboto_Bold_9.c
                ${SELECTED_AUDIO_GUI_DIR}/generated/assets/fonts/lv_font_Roboto_Bold_10.c
                ${SELECTED_AUDIO_GUI_DIR}/generated/assets/fonts/lv_font_Roboto_Bold_12.c
                ${SELECTED_AUDIO_GUI_DIR}/generated/assets/fonts/lv_font_Roboto_Bold_13.c

                src/PC_UART_API/PC_UART_API.c
                src/PC_UART_API/PC_UART_FWBridgeService.c
                src/PC_UART_API/PC_UART_BulkFileService.c
                src/PC_UART_API/PC_UART_API_ValidateTest.c 

                src/ExtFlash_Controller/ExtFlash_Controller.c 
                src/ExtFlash_Controller/SPIController/SPIController.c 
                src/ExtFlash_Controller/ExtFlashInitialSetup/ExtFlash_InitialSetupCtrl.c 
                src/ExtFlash_Controller/ExtFlashReadWrite/ExtFlash_ReadWriteControl.c
                src/ExtFlash_Controller/Flash_Memory_ValidateTest/Flash_Memory_ValidateTest.c 
                src/ExtFlash_Controller/ZephyrFlashMiddleWare/ZephyrFlashMiddleWare.c
                src/ExtFlash_Controller/LittleFsController/LittleFs_Controller.c
                
                src/Temp/Temp.c)

target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/LVGL_Controller/Screens/Screen_Welcome/GUI_Guider_Generated/generated
    ${CMAKE_CURRENT_SOURCE_DIR}/src/LVGL_Controller/Screens/Screen_Welcome/GUI_Guider_Generated/custom
)

# Both GUI Guider exports contain headers with the same names. Keep the
# SelectedAudioSource include search local to its generated implementation so
# it cannot accidentally include the Welcome screen's gui_guider.h.
set_property(SOURCE
    ${SELECTED_AUDIO_GUI_SCREEN_SOURCE}
    ${SELECTED_AUDIO_GUI_EVENT_SOURCE}
    APPEND PROPERTY COMPILE_OPTIONS
        "-iquote${SELECTED_AUDIO_GUI_DIR}/generated"
        "-iquote${SELECTED_AUDIO_GUI_DIR}/custom"
)

