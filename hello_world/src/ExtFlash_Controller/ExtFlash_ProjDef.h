#ifndef EXTFLASH_PROJDEF_H
#define EXTFLASH_PROJDEF_H

#include <zephyr/devicetree.h>

#define DATA_PARTITION_START_ADDRESS                DT_REG_ADDR(DT_NODELABEL(ext_storage_partition))
#define DATA_PARTITION_END_ADDRESS                  DT_REG_SIZE(DT_NODELABEL(ext_storage_partition))

#define JEDEC_ID_0                                  0xC2
#define JEDEC_ID_1                                  0x28
#define JEDEC_ID_2                                  0x17

//Read Buffer Lengths
#define DEFAULT_READ_BUFF_LENGTH                    256U

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


#endif//EXTFLASH_PROJDEF_H
