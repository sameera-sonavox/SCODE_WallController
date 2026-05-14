#ifndef BOOTLOADER_PROJDEF_H
#define BOOTLOADER_PROJDEF_H

#include <zephyr/storage/flash_map.h>

#define FLASH_AREA_FW_IMAGE_STORE_ID                PARTITION_ID(slot1_partition)
#define FW_IMAGE_HOST_DEVICE_ID                     CAN_NODE_0_ID

#define DATA_PAYLOAD_PER_ONE_IMAGE_FRAME            48
#define FW_IMG_WRITE_BYTE_LENGTH                    16
#define FW_IMG_PACKET_DATA_LENGTH                   50
#define MAX_NUMBER_OF_ALLOWABLE_LOST_PACKETs        100

#define TIMEOUT_FW_UPREQ_TO_FW_UPDATE_ms            500
#define TIMEOUT_FW_UPDATE_NextPacket_ms             300
#define TIMEOUT_REBOOT_CONFIRMATION_ms              100
#define TIMEOUT_LOST_PACKET_RECOVERY_PROCESS_ms     2000
#define TIMEOUT_LOST_PACKET_TX_ms                   500

//Do not change
#define FW_IMAGE_SECONDARY_SLOT_WRITE_OFFSET        0x2000

#define BOOTLOADER_TX_Retry_MAX_COUNT               3

#define DEBUG_BOOTLOADER

#endif