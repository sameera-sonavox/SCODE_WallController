#ifndef EXTFLASH_READWRITE_CONTROL_H
#define EXTFLASH_READWRITE_CONTROL_H

#include <stdint.h>
#include <stdlib.h>

#include "../Lib/SPI/NXP_SPI_API.h"
#include "../ExtFlash_ProjDef.h"

/// @brief Read data from the external flash into a caller-provided buffer.
/// @param uiAddr Data reading start address.
/// @param puiRecvData Buffer that receives the data.
/// @param uiDataLen Number of bytes to read.
/// @return 0 on success, or a negative errno value on failure.
extern int iRead_DataFromFlash_Normal(uint32_t uiAddr, uint8_t *puiRecvData, size_t uiDataLen);

/// @brief Read data from the external flash in quad mode into a caller-provided buffer
/// @param uiAddr Data reading start address.
/// @param puiRecvData Buffer that receives the data.
/// @param uiDataLen Number of bytes to read.
/// @return 0 on success, or a negative errno value on failure.
extern int iRead_DataFromFlash_Quad(uint32_t uiAddr, uint8_t *puiRecvData, size_t uiDataLen);

/// @brief Erase one 4-KiB sector from the external flash.
/// @param uiAddr Erase start address; it must be 4-KiB aligned.
/// @return 0 on success, or a negative errno value on failure.
extern int iErase_Flash_4KB(uint32_t uiAddr);

/// @brief Write data to the external flash and wait for programming to finish.
/// @param uiAddr Write start address.
/// @param puiData Buffer containing the data to write.
/// @param uiDataLen Number of bytes to write.
/// @return 0 on success, or a negative errno value on failure.
extern int iWrite_DataToFlash(uint32_t uiAddr, const uint8_t *puiData, size_t uiDataLen);

#endif//EXTFLASH_READWRITE_CONTROL_H
