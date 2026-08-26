#ifndef WELCOMESCREEN_UIPARAM_H
#define WELCOMESCREEN_UIPARAM_H

#include "../../Screen_Parameters/LVGL_AudioSources_UIParam.h"
#include "../../FileSystem_Adapter/LVGL_ZephyrFS_Adapter.h"
#include "../../../ExtFlash_Controller/ExtFlash_DeviceTreeEntries.h"

#define WELCOMESCRN_TOP_LEFT_X                      0U
#define WELCOMESCRN_TOP_LEFT_Y                      0U
#define WELCOMESCRN_IMAGE_CONTAINER_HEIGHT          80U
#define WELCOME_IMAGE_FADE_DURATION_ms              1000U

#define WELCOMESCRN_TEXT_CONTAINER_POS_Y            (WELCOMESCRN_IMAGE_CONTAINER_HEIGHT + 1U)
#define WELCOMESCRN_TEXT_CONTAINER_HEIGHT           (DISPLAY_HEIGHT - WELCOMESCRN_TEXT_CONTAINER_POS_Y)

#define IMAGE_DRIVE_LETTER                          "L"
#define IMAGE_DRIVE_SEPARATOR                       ":"
#define IMAGE_FILE_NAME                             "Logo_Resize.bin"
#define WELCOME_IMAGE_PATH                          (IMAGE_DRIVE_LETTER IMAGE_DRIVE_SEPARATOR "/Images/" IMAGE_FILE_NAME)

#define WELCOMESCRN_LABELCONTAINER_HEIGHT           20U
#define WELCOMESCRN_LABELCONTAINER_WIDTH            DISPLAY_WIDTH
#define WELCOMESCRN_LABEL_TEXT_COLOR                0xF8F9FB
#define WELCOMESCRN_LABEL_HEIGHT                    20U
#define WELCOMESCRN_LOADING_TEXT_WIDTH              90U
#define WELCOMESCRN_LABEL_DEFAULT_TEXT              "Loading"

#endif
