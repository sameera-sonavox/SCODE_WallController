#ifndef LITTLEFS_CONTROLLER_H
#define LITTLEFS_CONTROLLER_H

#include "LittleFs_DataTypes.h"

#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>

extern bool bInit_ExtFlash_FsController( void );
extern int iCreate_File(const char *pcafilePath, const char *pcafileName);
extern const sT_FileInfo_t *const pstGetFileInfo( void );
extern int iDelete_File(const char *pcaFilePath);
extern bool bDelete_Directory(const char *pcaDirectoryPath);
extern int iWrite_ToFileBin(const char *pcFileName, const uint8_t *puiDataBuff, size_t uiLen, uint16_t uiMaxWriteSize);

extern bool bStart_BulkDataTransfer(sT_BulkTransferData stTTransferData);
extern void vEnd_BulkDataTransfer( sT_TransferResult_t *pstTransferResult );
extern int iSend_BulkTransferData( sT_MsgData_t stTMsg );

#endif
