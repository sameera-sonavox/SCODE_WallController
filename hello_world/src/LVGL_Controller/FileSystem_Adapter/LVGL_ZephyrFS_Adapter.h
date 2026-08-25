#ifndef LVGL_ZEPHYRFS_ADAPTER_H
#define LVGL_ZEPHYRFS_ADAPTER_H

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>

#include "../../ExtFlash_Controller/ExtFlash_DeviceTreeEntries.h"

#define LVGL_ZEPHYR_FS_DRIVE_LETTER             'L'

extern bool bInit_LVGL_ZephyrFSAdapter( void );

#endif