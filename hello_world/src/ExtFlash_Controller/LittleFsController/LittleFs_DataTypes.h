#ifndef LITTLEFS_DATATYPES_H
#define LITTLEFS_DATATYPES_H


#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdatomic.h>
#include <zephyr/fs/fs.h>
#include "../ExtFlash_ProjDef.h"

typedef enum
{
    eFile_Error,
    eFile_Config,
    eFile_Data,
    eFile_Image,
    eFile_Icon,
    eNUMBER_OF_FILE_TYPEs
} eFileType_t;

typedef enum
{
    eFileTransfer_Idle = 0,
    eFileTransfer_Receiving,
    eFileTransfer_Finalizing,
    eFileTransfer_Failed,
    eFileTransfer_Success,
    eNUMBER_OF_TRANFER_STATEs
} eFileTransfer_State_t;

typedef enum
{
    eErrorState_None = 0,
    eErrorState_TimeOut,
    eErrorState_WriteError,
    eErrorState_CRCMismatch,
    eErrorState_ReceivedByteCountMismatch,
    eErrorState_FileDelete,
    eErrorState_FileSync,
    eErrorState_FileClose,
    eErrorState_RefreshFileCatalog,
    eErrorState_InvalidRequest,
    eErrorState_QueueError,
    eErrorState_FrameCRCMismatch,
    eErrorState_FrameSequence,
    eNUMBER_OF_ERROR_STATEs
} eErrorState_t;

typedef struct
{
/*     uint8_t uiId;
    eFileType_t eType; */

    char caFileName[MAX_FILE_NAME_LENGTH];
    char caFilePath[MAX_FILE_PATH_LENGTH];

    uint8_t uiFileNameLen;
    size_t uiFileSize;
} sT_FileInfo_t;

typedef struct
{
    sT_FileInfo_t staFileInfo[MAX_FILE_COUNT];
    uint8_t uiFileCount;
} sT_FileCatalog_t;

typedef struct
{
    eFileType_t eFileType;
    char pcaFileName[MAX_FILE_NAME_LENGTH];
    uint32_t uiFileSize;
    uint16_t uiCRC;
} sT_BulkTransferData;

typedef struct
{
    _Atomic eFileTransfer_State_t eTransferState;
    eFileType_t eFileType;
    char pcaDirPath[MAX_FILE_PATH_LENGTH];
    char pcaTotalFilePath[MAX_FILE_PATH_LENGTH];

    uint32_t uiFileSize;
    uint32_t uiReceivedSize;
    uint32_t uiAcceptedSize;
    uint32_t uiNextFrameId;
    uint16_t uiRunningCRC;
    uint16_t uiCRC;
    uint32_t uiOffset;
    uint32_t uiLastActivityTime_ms;

    _Atomic bool bMsgQ_Initialized;
    _Atomic bool bMsg_ThreadInitialized;
    _Atomic eErrorState_t eError;

    bool bFileOpen;
    struct fs_file_t stTFileData;
} sT_BulkDataControl_t;

typedef struct
{
    uint32_t uiFrameId;
    uint8_t uiMsg[MAX_FRAME_LENGTH];
    uint16_t uiDataLen;
    uint16_t uiFrameCRC;
} sT_MsgData_t;

typedef struct
{
    eFileTransfer_State_t eTransferState;
    eErrorState_t eErrorState;    
} sT_TransferResult_t;

#endif//LITTLEFS_DATATYPES_H
