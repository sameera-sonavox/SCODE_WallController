#ifndef EXTFLASH_PROJDEF_H
#define EXTFLASH_PROJDEF_H

#include <zephyr/devicetree.h>

#define DATA_PARTITION_START_ADDRESS                DT_REG_ADDR(DT_NODELABEL(ext_storage_partition))
#define DATA_PARTITION_SIZE_BYTES                   DT_REG_SIZE(DT_NODELABEL(ext_storage_partition))
#define DATA_PARTITION_END_ADDRESS                  (DATA_PARTITION_START_ADDRESS + DATA_PARTITION_SIZE_BYTES)

/* Bring-up only: enabling this suite erases its reserved validation area. */
#define EXT_FLASH_ENABLE_VALIDATION_TESTS           0U

#define JEDEC_ID_0                                  0xC2
#define JEDEC_ID_1                                  0x28
#define JEDEC_ID_2                                  0x17

//Read Buffer Lengths
#define DEFAULT_READ_BUFF_LENGTH                    256U
#define QUAD_READ_MAX_TRANSACTION_LENGTH            4096U// 256U

//Commands
#define JEDEC_ID_REQ_CMD                            0x9F
#define STATUS_REG_READ_CMD                         0x05
#define CONFIG_REG_READ_CMD                         0x15
#define WRITE_ENABLE_CMD                            0x06
#define WRITE_DISABLE_CMD                           0x04

#define SFDP_MODE_CMD                               0x5A
#define SFDP_MODE_ADDRESS                           0x000000
#define SFDP_RECV_BYTE0                             0x53//S
#define SFDP_RECV_BYTE1                             0x46//F
#define SFDP_RECV_BYTE2                             0x44//D
#define SFDP_RECV_BYTE3                             0x50//P

#define EXT_FLASH_READ_CMD                          0x03
#define EXT_FLASH_4KB_SECTOR_ERASE                  0x20
#define EXT_FLASH_WRITE_CMD                         0x02
#define EXT_FLASH_QUAD_READ_CMD                     0x6B

//FLash Geometry
#define EXT_FLASH_TOTAL_SIZE_BYTEs                  (8U * 1024U * 1024U)
#define EXT_FLASH_PAGE_SIZE_BYTEs                   256U
#define EXT_FLASH_ERASE_BLOCK_SIZE_BYTEs            4096U//Page Size
#define EXT_FLASH_PAGE_COUNT                        2048U
#define EXT_FLASH_WRITE_BLOCK_SIZE_BYTEs            1U
#define EXT_FLASH_ERASE_VALUE                       0xFFU


//File System
#define MAX_FILE_NAME_LENGTH                        30U
#define MAX_FILE_COUNT                              10U
#define MAX_FILE_PATH_LENGTH                        100U
#define MAX_DIRECTORY_COUNT                         10U
#define MAX_FRAME_LENGTH                            256U
#define MAX_MSGQ_DEPTH                              10U
#define DATA_TRANSFER_THREAD_STACK_SIZE             1024U
#define DATA_TRANSFER_THREAD_PRIORITY               4U
#define DATA_TRANSFER_TIMEOUT_ms                    500U
#define DATA_TRANSFER_THREAD_TIMEOUT_ms             2000U

#endif//EXTFLASH_PROJDEF_H
