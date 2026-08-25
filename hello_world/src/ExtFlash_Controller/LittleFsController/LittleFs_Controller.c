#include "LittleFs_Controller.h"
#include "../Lib/GenericMacro.h"

#include <zephyr/devicetree.h>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdatomic.h>
#include "../ExtFlash_DeviceTreeEntries.h"

//#define EXT_LITTLEFS_NODE            DT_NODELABEL(ext_littlefs)
FS_FSTAB_DECLARE_ENTRY(EXT_LITTLEFS_NODE);

static sT_BulkDataControl_t stTBulkDataCtrl = {0};
static sT_FileCatalog_t stTFileCatalog = {0};
static sT_FileInfo_t *pstFileInfo = stTFileCatalog.staFileInfo;

typedef enum
{
    eBulkQueueMessage_Data = 0,
    eBulkQueueMessage_End,
} eBulkQueueMessageType_t;

typedef struct
{
    eBulkQueueMessageType_t eType;
    sT_MsgData_t stData;
} sT_BulkQueueMessage_t;

static struct k_msgq stMsgQueue_FileTransfer;
static sT_BulkQueueMessage_t staMsgData[MAX_MSGQ_DEPTH];
static struct k_work stMsgWorkQUeue;

K_THREAD_STACK_DEFINE(lfs_ThreadStack, DATA_TRANSFER_THREAD_STACK_SIZE);
struct k_thread kthread_DataTranfer;
k_tid_t kthread_DTransferId;

K_MUTEX_DEFINE(kMutex_FileAccess);
K_MUTEX_DEFINE(kMutex_BulkTransfer);
K_SEM_DEFINE(kSem_BulkWorkerDone, 0, 1);

static bool bMount_LittleFs( void );
static int iGetFileInfo( const char *pcaRootorFolder );
static int iCheck_FileAvailability(const char *caFullPath, bool *pbIsAvailable);
static bool bBuild_Validate_FullPathName( const char *pcafilePath, const char *pcafileName, char *caFullPath, size_t uiFilePathLen, size_t uiFileNameLen );
static bool bContains_DotPathComponent(const char *pcaPath);
static int iCheckOrCreateDirectory(const char *caDirectoryPath, bool *pbIsAvailable);
static sT_FileInfo_t *pstGetFile_ByFileName(const char *pcaFileName);
static sT_FileInfo_t *pstGetFile_ByFilePath(const char *pcaFilePath);
static int iDelete_AllFiles_InsideDirectory(const char *pcaDirectoryPath);
static int iDelete_FileInternal(const char *pcaFilePath);

static int iBulkWrite_ToFile(const sT_MsgData_t *pstMsg);
static void vDataTransfer_TimeOutHandler(struct k_work *work);
static void vLittleFs_FileTransferMonitor(void *d1, void *d2, void *d3);
static uint16_t u16CRC16_CCITT_Update(uint16_t crc, const uint8_t *data, uint32_t len);
static bool bCanProcess_QueuedBulkData(void);
static void vSignal_BulkWorkerDone(bool bSubmitCleanupWork);

static void vSet_BulkkTransferData_ToDefault( void );
static inline void vSet_BulkTransferState( eFileTransfer_State_t eState );
static inline eFileTransfer_State_t eGetTransferState( void );
static inline bool bIsBulkDataReceiving(void);
static inline bool bIsBulkTransferActive(void);
static inline bool bCanStart_BulkTransfer(void);
static inline void vSet_MsgQ_InitFlag( void );
static inline void vClear_MsgQ_InitFlag( void );
static inline bool bIs_MsgQInitialized( void );
static inline void vSet_MsgThread_InitFlag( void );
static inline void vClear_MsgThread_InitFlag( void );
static inline bool bIs_MsgThread_Initialized( void );
static inline void vSet_Transfer_ErrorState( eErrorState_t eError );
static inline eErrorState_t eGet_MsgQueue_ErrorState( void );
static inline bool bIs_MsgQueueInError( void );

bool bInit_ExtFlash_FsController( void )
{
    if(!bMount_LittleFs())
        return false;    
    if(iGetFileInfo(EXT_LITTLEFS_MOUNT_POINT) != 0)
    {
        return false;
    }
    return true;
}

#pragma region Bulk Data Transfer

bool bStart_BulkDataTransfer(sT_BulkTransferData stTTransferData)
{
    if(stTTransferData.eFileType <= eFile_Error ||
       stTTransferData.eFileType >= eNUMBER_OF_FILE_TYPEs)
    {
        FHALT("LittleFS: Invalid FileType");
        return false;
    }
    if(stTTransferData.uiFileSize == 0U)
    {
        FHALT("LittleFS: Invalid Parameters for FileSize");
        return false;        
    }
    if(k_mutex_lock(&kMutex_BulkTransfer, K_MSEC(100)) != 0)
    {
        return false;
    }
    if(!bCanStart_BulkTransfer())
    {
        k_mutex_unlock(&kMutex_BulkTransfer);
        FHALT("LittleFS: BulkTransfer in Progress");
        return false;        
    }

    vSet_BulkkTransferData_ToDefault();

    switch (stTTransferData.eFileType)
    {
        case eFile_Image:
            memcpy(stTBulkDataCtrl.pcaDirPath,EXT_LITTLEFS_IMAGE_DIRECTORY, sizeof(EXT_LITTLEFS_IMAGE_DIRECTORY));
            break;
        case eFile_Icon:
            memcpy(stTBulkDataCtrl.pcaDirPath, EXT_LITTLEFS_ICONS_DIRECTORY, sizeof(EXT_LITTLEFS_ICONS_DIRECTORY));
            break;
        case eFile_Config:
            memcpy(stTBulkDataCtrl.pcaDirPath, EXT_LITTLEFS_CONFIG_DIRECTORY, sizeof(EXT_LITTLEFS_CONFIG_DIRECTORY));
            break;
        case eFile_Data:
            memcpy(stTBulkDataCtrl.pcaDirPath, EXT_LITTLEFS_DATA_DIRECTORY, sizeof(EXT_LITTLEFS_DATA_DIRECTORY));
            break;
        case eFile_Error:
        default:
            k_mutex_unlock(&kMutex_BulkTransfer);
            return false;
    }

    size_t uiFilePathLen = strnlen(stTBulkDataCtrl.pcaDirPath, MAX_FILE_PATH_LENGTH);
    size_t uiFileNameLen = strnlen(stTTransferData.pcaFileName, MAX_FILE_NAME_LENGTH);
    if(!bBuild_Validate_FullPathName(stTBulkDataCtrl.pcaDirPath, 
                                 stTTransferData.pcaFileName, 
                                 stTBulkDataCtrl.pcaTotalFilePath, 
                                 uiFilePathLen, uiFileNameLen))
    {
        vSet_BulkTransferState(eFileTransfer_Idle);
        k_mutex_unlock(&kMutex_BulkTransfer);
        return false;
    }

    int iResult = iDelete_File(stTBulkDataCtrl.pcaTotalFilePath);
    if(iResult != 0 && iResult != -ENOENT)
    {
        k_mutex_unlock(&kMutex_BulkTransfer);
        FHALT("LittleFS: File Could not be deleted @Path: %s", stTBulkDataCtrl.pcaTotalFilePath);
        return false;        
    }

    iResult = iCreate_File(stTBulkDataCtrl.pcaDirPath, stTTransferData.pcaFileName);
    if(iResult < 0)
    {
        k_mutex_unlock(&kMutex_BulkTransfer);
        FHALT("LittleFS: File Could not be created with Name: %s", stTTransferData.pcaFileName);
        return false;
    }

    fs_file_t_init(&stTBulkDataCtrl.stTFileData);
    iResult = fs_open(&stTBulkDataCtrl.stTFileData, stTBulkDataCtrl.pcaTotalFilePath, FS_O_WRITE | FS_O_TRUNC);
    if(iResult < 0)
    {
        (void)iDelete_FileInternal(stTBulkDataCtrl.pcaTotalFilePath);
        k_mutex_unlock(&kMutex_BulkTransfer);
        FHALT("LittleFS: File ['%s']Could not be opened @Error: %d", stTBulkDataCtrl.pcaTotalFilePath, iResult);
        return false;        
    }
    stTBulkDataCtrl.bFileOpen = true;

    stTBulkDataCtrl.eFileType = stTTransferData.eFileType;
    stTBulkDataCtrl.uiFileSize = stTTransferData.uiFileSize;
    stTBulkDataCtrl.uiCRC = stTTransferData.uiCRC;
    stTBulkDataCtrl.uiRunningCRC = 0xFFFFU;
    stTBulkDataCtrl.uiLastActivityTime_ms = k_uptime_get_32();

    k_msgq_init(&stMsgQueue_FileTransfer,
                (char *)staMsgData,
                sizeof(staMsgData[0]),
                ARRAY_SIZE(staMsgData));
    k_sem_reset(&kSem_BulkWorkerDone);
    k_work_init(&stMsgWorkQUeue, vDataTransfer_TimeOutHandler);
    kthread_DTransferId = k_thread_create(&kthread_DataTranfer, lfs_ThreadStack, K_THREAD_STACK_SIZEOF(lfs_ThreadStack),
                                          vLittleFs_FileTransferMonitor, NULL, NULL, NULL,
                                          DATA_TRANSFER_THREAD_PRIORITY, 0, K_FOREVER);
    vSet_MsgQ_InitFlag();
    vSet_MsgThread_InitFlag();
    vSet_BulkTransferState(eFileTransfer_Receiving);
    k_thread_start(kthread_DTransferId);
    k_mutex_unlock(&kMutex_BulkTransfer);
    return true;
}

void vEnd_BulkDataTransfer( sT_TransferResult_t *pstTransferResult )
{
    if(pstTransferResult == NULL)
    {
        FHALT("LittleFS: Null Pointer reference for 'pstTransferResult'");
        return;
    }

    pstTransferResult->eTransferState = eGetTransferState();
    pstTransferResult->eErrorState = eErrorState_InvalidRequest;

    if(k_mutex_lock(&kMutex_BulkTransfer, K_MSEC(100)) != 0)
    {
        return;
    }
    if(!bIsBulkDataReceiving())
    {
        FHALT("LittleFS: No Transfer in Progress to End");
        k_mutex_unlock(&kMutex_BulkTransfer);
        return;
    }

    vSet_BulkTransferState(eFileTransfer_Finalizing);

    sT_BulkQueueMessage_t stEndMessage = {
        .eType = eBulkQueueMessage_End,
    };
    int iResult = k_msgq_put(
        &stMsgQueue_FileTransfer,
        &stEndMessage,
        K_MSEC(DATA_TRANSFER_THREAD_TIMEOUT_ms));
    if(iResult != 0)
    {
        vSet_Transfer_ErrorState(eErrorState_QueueError);
        vSet_BulkTransferState(eFileTransfer_Failed);
        goto Indicate_Failure;
    }

    iResult = k_sem_take(
        &kSem_BulkWorkerDone,
        K_MSEC(DATA_TRANSFER_THREAD_TIMEOUT_ms));
    if(iResult != 0)
    {
        vSet_Transfer_ErrorState(eErrorState_TimeOut);
        vSet_BulkTransferState(eFileTransfer_Failed);
        goto Indicate_Failure;
    }

    if(bIs_MsgQueueInError())
    {
        vSet_BulkTransferState(eFileTransfer_Failed);
        goto Indicate_Failure;
    }

    if(stTBulkDataCtrl.uiReceivedSize != stTBulkDataCtrl.uiFileSize)
    {
        vSet_Transfer_ErrorState(eErrorState_ReceivedByteCountMismatch);
        vSet_BulkTransferState(eFileTransfer_Failed);
        goto Indicate_Failure;
    }
    else if(stTBulkDataCtrl.uiRunningCRC != stTBulkDataCtrl.uiCRC)
    {
        vSet_Transfer_ErrorState(eErrorState_CRCMismatch);
        vSet_BulkTransferState(eFileTransfer_Failed);
    }
    else
    {
        vSet_Transfer_ErrorState(eErrorState_None);
        vSet_BulkTransferState(eFileTransfer_Success);
        goto Indicate_Success;
    }

    Indicate_Failure:
        if(bIs_MsgThread_Initialized())
        {
            k_thread_abort(kthread_DTransferId);
            vClear_MsgThread_InitFlag();
        }
        if(stTBulkDataCtrl.bFileOpen)
        {
            iResult = fs_close(&stTBulkDataCtrl.stTFileData);
            stTBulkDataCtrl.bFileOpen = false;
            if(iResult < 0 &&
               eGet_MsgQueue_ErrorState() == eErrorState_None)
            {
                vSet_Transfer_ErrorState(eErrorState_FileClose);
            }
        }
        iResult = iDelete_FileInternal(stTBulkDataCtrl.pcaTotalFilePath);
        if(iResult < 0 && iResult != -ENOENT)
        {
            vSet_Transfer_ErrorState(eErrorState_FileDelete);
        }
        goto Exit;
    
    Indicate_Success:
        iResult = fs_sync(&stTBulkDataCtrl.stTFileData);
        if(iResult < 0)
        {
            vSet_Transfer_ErrorState(eErrorState_FileSync);
            vSet_BulkTransferState(eFileTransfer_Failed);
            goto Indicate_Failure;
        }
        iResult = fs_close(&stTBulkDataCtrl.stTFileData);
        stTBulkDataCtrl.bFileOpen = false;
        if(iResult < 0)
        {
            vSet_Transfer_ErrorState(eErrorState_FileClose);
            vSet_BulkTransferState(eFileTransfer_Failed);
            goto Exit;
        }
        iResult = iGetFileInfo(EXT_LITTLEFS_MOUNT_POINT);
        if(iResult < 0)
        {
            vSet_Transfer_ErrorState(eErrorState_RefreshFileCatalog);
            vSet_BulkTransferState(eFileTransfer_Failed);
        }        
        goto Exit;
    
    Exit:
        pstTransferResult->eTransferState = eGetTransferState();
        pstTransferResult->eErrorState = eGet_MsgQueue_ErrorState();
        vSet_BulkkTransferData_ToDefault();
        k_mutex_unlock(&kMutex_BulkTransfer);
}

int iSend_BulkTransferData( sT_MsgData_t stTMsg )
{
    if(stTMsg.uiDataLen == 0U ||
       stTMsg.uiDataLen > MAX_FRAME_LENGTH)
    {
        return -EINVAL;
    }
    if(k_mutex_lock(&kMutex_BulkTransfer, K_MSEC(100)) != 0)
    {
        return -EBUSY;
    }
    if(!bIsBulkDataReceiving() || bIs_MsgQueueInError())
    {
        k_mutex_unlock(&kMutex_BulkTransfer);
        return -EBADE;
    }
    if(stTMsg.uiFrameId != stTBulkDataCtrl.uiNextFrameId)
    {
        k_mutex_unlock(&kMutex_BulkTransfer);
        return -EILSEQ;
    }
    if(stTBulkDataCtrl.uiAcceptedSize > stTBulkDataCtrl.uiFileSize ||
       stTMsg.uiDataLen >
           (stTBulkDataCtrl.uiFileSize -
            stTBulkDataCtrl.uiAcceptedSize))
    {
        k_mutex_unlock(&kMutex_BulkTransfer);
        return -EFBIG;
    }

    uint16_t uiFrameCRC = u16CRC16_CCITT_Update(
        0xFFFFU,
        stTMsg.uiMsg,
        stTMsg.uiDataLen);
    if(uiFrameCRC != stTMsg.uiFrameCRC)
    {
        k_mutex_unlock(&kMutex_BulkTransfer);
        return -EBADMSG;
    }

    sT_BulkQueueMessage_t stQueueMessage = {
        .eType = eBulkQueueMessage_Data,
        .stData = stTMsg,
    };
    int iRet = k_msgq_put(
        &stMsgQueue_FileTransfer,
        &stQueueMessage,
        K_NO_WAIT);
    if(iRet == 0)
    {
        stTBulkDataCtrl.uiAcceptedSize += stTMsg.uiDataLen;
        stTBulkDataCtrl.uiNextFrameId++;
    }
    k_mutex_unlock(&kMutex_BulkTransfer);
    return iRet;
}

static void vDataTransfer_TimeOutHandler(struct k_work *work)
{
    ARG_UNUSED(work);

    k_mutex_lock(&kMutex_BulkTransfer, K_FOREVER);
    if(!bIs_MsgQueueInError() || !bIsBulkTransferActive())
    {
        k_mutex_unlock(&kMutex_BulkTransfer);
        return;
    }

    vSet_BulkTransferState(eFileTransfer_Failed);
    if(stTBulkDataCtrl.bFileOpen)
    {
        int iResult = fs_close(&stTBulkDataCtrl.stTFileData);
        stTBulkDataCtrl.bFileOpen = false;
        if(iResult < 0)
        {
            vSet_Transfer_ErrorState(eErrorState_FileClose);
        }
    }

    int iResult = iDelete_FileInternal(stTBulkDataCtrl.pcaTotalFilePath);
    if(iResult < 0 && iResult != -ENOENT)
    {
        vSet_Transfer_ErrorState(eErrorState_FileDelete);
        FHALT("LittleFS: Partially Written File could not be deleted");
    }

    vSet_BulkkTransferData_ToDefault();
    k_mutex_unlock(&kMutex_BulkTransfer);
}

static void vLittleFs_FileTransferMonitor(void *d1, void *d2, void *d3)
{
    ARG_UNUSED(d1);
    ARG_UNUSED(d2);
    ARG_UNUSED(d3);

    sT_BulkQueueMessage_t stQueueMessage = {0};

    while(true)
    {
        int iRes = k_msgq_get(
            &stMsgQueue_FileTransfer,
            &stQueueMessage,
            K_MSEC(DATA_TRANSFER_TIMEOUT_ms));
        if(iRes < 0)
        {
            vSet_Transfer_ErrorState(eErrorState_TimeOut);
            vSet_BulkTransferState(eFileTransfer_Failed);
            vSignal_BulkWorkerDone(true);
            return;
        }

        if(stQueueMessage.eType == eBulkQueueMessage_End)
        {
            vSignal_BulkWorkerDone(false);
            return;
        }
        if(stQueueMessage.eType != eBulkQueueMessage_Data ||
           !bCanProcess_QueuedBulkData())
        {
            vSet_Transfer_ErrorState(eErrorState_QueueError);
            vSet_BulkTransferState(eFileTransfer_Failed);
            vSignal_BulkWorkerDone(true);
            return;
        }

        stTBulkDataCtrl.uiLastActivityTime_ms = k_uptime_get_32();
        iRes = iBulkWrite_ToFile(&stQueueMessage.stData);
        if(iRes != 0)
        {
            vSet_Transfer_ErrorState(eErrorState_WriteError);
            vSet_BulkTransferState(eFileTransfer_Failed);
            vSignal_BulkWorkerDone(true);
            return;
        }

        stTBulkDataCtrl.uiRunningCRC = u16CRC16_CCITT_Update(
            stTBulkDataCtrl.uiRunningCRC,
            stQueueMessage.stData.uiMsg,
            stQueueMessage.stData.uiDataLen);
        stTBulkDataCtrl.uiReceivedSize +=
            stQueueMessage.stData.uiDataLen;
    }
}

static void vSignal_BulkWorkerDone(bool bSubmitCleanupWork)
{
    vClear_MsgThread_InitFlag();
    k_sem_give(&kSem_BulkWorkerDone);
    if(bSubmitCleanupWork)
    {
        (void)k_work_submit(&stMsgWorkQUeue);
    }
}

static bool bCanProcess_QueuedBulkData(void)
{
    eFileTransfer_State_t eState = eGetTransferState();
    return (eState == eFileTransfer_Receiving ||
            eState == eFileTransfer_Finalizing);
}

static uint16_t u16CRC16_CCITT_Update(uint16_t crc, const uint8_t *data, uint32_t len)
{
    for (uint32_t i = 0; i < len; i++)
    {
        crc ^= ((uint16_t)data[i] << 8);

        for (uint8_t bit = 0; bit < 8; bit++)
        {
            if (crc & 0x8000)
            {
                crc = (crc << 1) ^ 0x1021;
            }
            else
            {
                crc <<= 1;
            }
        }
    }

    return crc;
}

static int iBulkWrite_ToFile(const sT_MsgData_t *pstMsg)
{
    if(pstMsg == NULL ||
       pstMsg->uiDataLen == 0U ||
       pstMsg->uiDataLen > MAX_FRAME_LENGTH ||
       !bCanProcess_QueuedBulkData())
    {
        return -EINVAL;
    }

    struct fs_file_t *pstFile = &stTBulkDataCtrl.stTFileData;

    ssize_t uiTotWrittenBytes = fs_write(pstFile, pstMsg->uiMsg, pstMsg->uiDataLen);
    if(uiTotWrittenBytes < 0 || uiTotWrittenBytes != pstMsg->uiDataLen)
    {
        return (uiTotWrittenBytes < 0)
                   ? (int)uiTotWrittenBytes
                   : -EIO;
    }

    return 0;
}

static void vSet_BulkkTransferData_ToDefault( void )
{
    memset(&stTBulkDataCtrl.pcaDirPath, '\0', sizeof(stTBulkDataCtrl.pcaDirPath));
    memset(&stTBulkDataCtrl.pcaTotalFilePath, '\0', sizeof(stTBulkDataCtrl.pcaTotalFilePath));

    stTBulkDataCtrl.uiCRC = 0U;
    stTBulkDataCtrl.uiFileSize = 0U;
    stTBulkDataCtrl.uiOffset = 0U;
    stTBulkDataCtrl.uiReceivedSize = 0U;
    stTBulkDataCtrl.uiAcceptedSize = 0U;
    stTBulkDataCtrl.uiNextFrameId = 0U;
    stTBulkDataCtrl.uiRunningCRC = 0xFFFFU;
    stTBulkDataCtrl.uiLastActivityTime_ms = 0U;

    if(bIs_MsgQInitialized())
    {
        k_msgq_purge(&stMsgQueue_FileTransfer);
    }
    if(bIs_MsgThread_Initialized())
    {
        k_thread_abort(kthread_DTransferId);
    }
    
    vClear_MsgQ_InitFlag();
    vClear_MsgThread_InitFlag();
    stTBulkDataCtrl.bFileOpen = false;
    k_sem_reset(&kSem_BulkWorkerDone);
    vSet_Transfer_ErrorState(eErrorState_None);
    vSet_BulkTransferState(eFileTransfer_Idle);
}

static inline void vSet_MsgQ_InitFlag( void )
{
    atomic_store_explicit(&stTBulkDataCtrl.bMsgQ_Initialized, true, memory_order_release);
}

static inline void vClear_MsgQ_InitFlag( void )
{
    atomic_store_explicit(&stTBulkDataCtrl.bMsgQ_Initialized, false, memory_order_release);
}

static inline bool bIs_MsgQInitialized( void )
{
    bool bRes = atomic_load_explicit(&stTBulkDataCtrl.bMsgQ_Initialized, memory_order_acquire);
    return bRes;
}

static inline void vSet_MsgThread_InitFlag( void )
{
    atomic_store_explicit(&stTBulkDataCtrl.bMsg_ThreadInitialized, true, memory_order_release);
}

static inline void vClear_MsgThread_InitFlag( void )
{
    atomic_store_explicit(&stTBulkDataCtrl.bMsg_ThreadInitialized, false, memory_order_release);
}

static inline bool bIs_MsgThread_Initialized( void )
{
    bool bRes = atomic_load_explicit(&stTBulkDataCtrl.bMsg_ThreadInitialized, memory_order_acquire);
    return bRes;
}

static inline void vSet_Transfer_ErrorState( eErrorState_t eError )
{
    atomic_store_explicit(&stTBulkDataCtrl.eError, eError, memory_order_release);
}

static inline eErrorState_t eGet_MsgQueue_ErrorState( void )
{
    eErrorState_t eError = atomic_load_explicit(&stTBulkDataCtrl.eError, memory_order_acquire);
    return eError;
}

static inline bool bIs_MsgQueueInError( void )
{
    eErrorState_t eError = atomic_load_explicit(&stTBulkDataCtrl.eError, memory_order_acquire);
    return (eError != eErrorState_None);
}

static inline void vSet_BulkTransferState( eFileTransfer_State_t eState )
{
    if(eState >= eNUMBER_OF_TRANFER_STATEs)
    {
        FHALT("LittleFS: Invalid Transfer State: %d", eState);
        return;
    }
    atomic_store_explicit(&stTBulkDataCtrl.eTransferState, eState, memory_order_release);
}

static inline bool bIsBulkDataReceiving(void)
{
    return eGetTransferState() == eFileTransfer_Receiving;
}

static inline bool bIsBulkTransferActive(void)
{
    return eGetTransferState() != eFileTransfer_Idle;
}

static inline bool bCanStart_BulkTransfer(void)
{
    return eGetTransferState() == eFileTransfer_Idle;
}

static inline eFileTransfer_State_t eGetTransferState( void )
{
    eFileTransfer_State_t eState = atomic_load_explicit(&stTBulkDataCtrl.eTransferState, memory_order_acquire);
    return eState;
}
#pragma endregion 

static bool bMount_LittleFs( void )
{
    struct fs_mount_t *pstMount = &FS_FSTAB_ENTRY(EXT_LITTLEFS_NODE);

    int iResult = fs_mount(pstMount);
    if(iResult < 0)
    {
        FHALT("LittleFs: Mount Fail with Result : %d", iResult);
        return false;
    }

    printf("LittleFs: Mount Successful\n\r");
    return true;
}

static int iGetFileInfo( const char *pcaRootorFolder )
{
    static sT_FileInfo_t staStagingFileInfo[MAX_FILE_COUNT];
    static char caDirectoryWorkList[MAX_DIRECTORY_COUNT][MAX_FILE_PATH_LENGTH];

    if(pcaRootorFolder == NULL)
    {
        FHALT("LittlFS: Null Pointer for File Name");
        return -EINVAL;
    }

    if(pcaRootorFolder[0] != '/')
    {
        return -EINVAL;
    }

    size_t uiRootPathLength = strnlen(pcaRootorFolder, MAX_FILE_PATH_LENGTH);
    if(uiRootPathLength == 0U)
    {
        return -EINVAL;
    }

    if(uiRootPathLength >= MAX_FILE_PATH_LENGTH)
    {
        return -ENAMETOOLONG;
    }

    memset(staStagingFileInfo, 0, sizeof(staStagingFileInfo));
    memset(caDirectoryWorkList, 0, sizeof(caDirectoryWorkList));
    memcpy(caDirectoryWorkList[0], pcaRootorFolder, uiRootPathLength + 1U);

    size_t uiStagingFileCount = 0U;
    size_t uiPendingDirectoryCount = 1U;
    size_t uiTotalDirectoryCount = 1U;
    int iResult = 0, iCloseResult = 0;

    if(k_mutex_lock(&kMutex_FileAccess, K_MSEC(100)) != 0)
    {
        FHALT("LittleFS: File System is accessed by another thread. Cannot proceed and exiting after time out");
        return -ENOLCK;
    }
    
    while(uiPendingDirectoryCount > 0U)
    {
        uiPendingDirectoryCount--;

        char caCurrentDirectory[MAX_FILE_PATH_LENGTH] = {'\0'};
        memcpy(caCurrentDirectory, caDirectoryWorkList[uiPendingDirectoryCount], sizeof(caCurrentDirectory));

        struct fs_dir_t stDirectory;
        struct fs_dirent stDirInfo;

        fs_dir_t_init(&stDirectory);
        iResult = fs_opendir(&stDirectory, caCurrentDirectory);
        if(iResult < 0)
        {
            k_mutex_unlock(&kMutex_FileAccess);
            return iResult;
        }

        //Access all files inside the directory
        while(true)
        {
            memset(&stDirInfo, 0, sizeof(stDirInfo));

            iResult = fs_readdir(&stDirectory, &stDirInfo);
            if(iResult < 0)
            {
                break;
            }

            if(stDirInfo.name[0] == '\0')//Empty Name
            {
                break;
            }

            if(strcmp(stDirInfo.name, ".") == 0 || strcmp(stDirInfo.name, "..") == 0)
            {
                continue;
            }

            size_t uiCurrentDirLength = strlen(caCurrentDirectory);
            const char *pcaSeparator = (uiCurrentDirLength > 0 && caCurrentDirectory[uiCurrentDirLength - 1U] == '/')? "":"/";

            char caChildPath[MAX_FILE_PATH_LENGTH] = {'\0'};
            int iChildPathLength = snprintf(caChildPath, sizeof(caChildPath), "%s%s%s", 
                                            caCurrentDirectory, pcaSeparator, stDirInfo.name);
            if(iChildPathLength < 0 || 
               ((size_t)iChildPathLength >= sizeof(caChildPath)))
            {
                iResult = -ENAMETOOLONG;
                break;
            }

            //Entry is another Directory
            if(stDirInfo.type == FS_DIR_ENTRY_DIR)
            {
                if(uiTotalDirectoryCount >= MAX_DIRECTORY_COUNT || uiPendingDirectoryCount >= MAX_DIRECTORY_COUNT)
                {
                    iResult = -ENOSPC;
                    break;
                }

                memcpy(caDirectoryWorkList[uiPendingDirectoryCount], caChildPath, (size_t)iChildPathLength + 1U);
                uiPendingDirectoryCount++;
                uiTotalDirectoryCount++;
                continue;//This forces to continue the inner loop under the root directory entry and loop through
                         //rest of the files
            }
            if(stDirInfo.type != FS_DIR_ENTRY_FILE)
            {
                continue;
            }
            if(uiStagingFileCount >= MAX_FILE_COUNT)
            {
                iResult = -ENOSPC;
                break;
            }

            size_t uiFileNameLength = strnlen(stDirInfo.name, sizeof(stDirInfo.name));
            if(uiFileNameLength >= MAX_FILE_NAME_LENGTH)
            {
                iResult = -ENAMETOOLONG;
                break;
            }

            sT_FileInfo_t *pstCurrentFileInfo = &staStagingFileInfo[uiStagingFileCount];
            memset(pstCurrentFileInfo, 0, sizeof(*pstCurrentFileInfo));

            memcpy(pstCurrentFileInfo->caFileName, stDirInfo.name, uiFileNameLength + 1U);//Copy file name
            memcpy(pstCurrentFileInfo->caFilePath, caChildPath, (size_t)iChildPathLength + 1U);//Copy full path
            pstCurrentFileInfo->uiFileNameLen = uiFileNameLength;
            pstCurrentFileInfo->uiFileSize = stDirInfo.size;
            uiStagingFileCount++;
        }

        iCloseResult = fs_closedir(&stDirectory);
        if(iResult == 0 && iCloseResult < 0)
        {
            iResult = iCloseResult;
        }

        if(iResult < 0)
        {
            k_mutex_unlock(&kMutex_FileAccess);
            return iResult;
        }
    }

    memset(pstFileInfo, 0, sizeof(*pstFileInfo) * MAX_FILE_COUNT);
    memcpy(pstFileInfo, staStagingFileInfo, uiStagingFileCount * sizeof(staStagingFileInfo[0]));
    stTFileCatalog.uiFileCount = uiStagingFileCount;
    
    k_mutex_unlock(&kMutex_FileAccess);
    return 0;
}

int iCreate_File( const char *pcafilePath, const char *pcafileName )
{
    if(pcafileName == NULL || pcafilePath == NULL)
    {
        FHALT("LittlFS: Null Pointer for File Name or Path");
        return -EINVAL;
    }
    if(bIsBulkTransferActive())
    {
        return -EBUSY;
    }

    size_t uiFilePathLen = strnlen(pcafilePath, MAX_FILE_PATH_LENGTH);
    size_t uiFileNameLen = strnlen(pcafileName, MAX_FILE_NAME_LENGTH);
    char caFullPath[MAX_FILE_PATH_LENGTH] = {'\0'};
    if(!bBuild_Validate_FullPathName(pcafilePath, pcafileName, caFullPath, uiFilePathLen, uiFileNameLen))
    {
        return -EINVAL;
    }
    
    size_t uiTotalPathLen = strlen(caFullPath);
    bool bisAvailable = false;

    int iResult = iCheckOrCreateDirectory(pcafilePath, &bisAvailable);
    if(iResult != 0)
    {
        FHALT("LittlFS: Create Directory fail with an Error = %d", iResult);
        return iResult;
    }
    
    iResult = iCheck_FileAvailability(caFullPath, &bisAvailable);
    if(bisAvailable)
    {
        FHALT("LittlFS: File already exists: %s", caFullPath);
        return -EEXIST;
    }
    if(!bisAvailable && iResult != 0)
    {
        FHALT("LittlFS: File is not available, but FS error occurred: %d", iResult);
        return iResult;
    }

    iResult = k_mutex_lock(&kMutex_FileAccess, K_MSEC(100));
    if(iResult != 0)
    {
        FHALT("LittleFS: File System is accessed by another thread. Cannot proceed and exiting after time out");
        return iResult;
    }

    struct fs_file_t stFile;
    fs_file_t_init(&stFile);

    if(stTFileCatalog.uiFileCount >= MAX_FILE_COUNT)
    {
        k_mutex_unlock(&kMutex_FileAccess);
        FHALT("LittlFS: File catalog is full");
        return -ENOSPC;
    }

    iResult = fs_open(&stFile, caFullPath, FS_O_CREATE | FS_O_WRITE);
    if(iResult < 0)
    {
        k_mutex_unlock(&kMutex_FileAccess);
        FHALT("LittlFS: File Create Error @Err: %d", iResult);
        return iResult;
    }

    int iCloseResult = fs_close(&stFile);
    if(iResult == 0 && iCloseResult < 0)
    {
        k_mutex_unlock(&kMutex_FileAccess);
        iResult = iCloseResult;
        return iResult;
    }

    sT_FileInfo_t *pstCurrentFile = &pstFileInfo[stTFileCatalog.uiFileCount];
    memset(pstCurrentFile->caFileName, 0, MAX_FILE_NAME_LENGTH);
    memset(pstCurrentFile->caFilePath, 0, MAX_FILE_PATH_LENGTH);

    memcpy(pstCurrentFile->caFileName, pcafileName, uiFileNameLen + 1U);//Copy file name
    memcpy(pstCurrentFile->caFilePath, caFullPath, uiTotalPathLen + 1U);//Copy full path
    pstCurrentFile->uiFileNameLen = uiFileNameLen;
    pstCurrentFile->uiFileSize = 0U;
    stTFileCatalog.uiFileCount++;

    k_mutex_unlock(&kMutex_FileAccess);
    return 0;
}

static bool bBuild_Validate_FullPathName( const char *pcafilePath, const char *pcafileName, char *caFullPath, size_t uiFilePathLen, size_t uiFileNameLen )
{
    const char carootDir[] = EXT_LITTLEFS_MOUNT_POINT;
    size_t uirootDirLen = sizeof(carootDir) - 1U;

    if(uiFileNameLen == 0U || uiFileNameLen >= MAX_FILE_NAME_LENGTH)
    {
        FHALT("LittlFS: Invalid Length for File Name @Len: %zu", uiFileNameLen);
        return false;        
    }
    if(uiFilePathLen == 0U || uiFilePathLen >= MAX_FILE_PATH_LENGTH)
    {
        FHALT("LittlFS: Invalid Length for Total File Name @Len: %zu", uiFilePathLen);
        return false;        
    }
    if(strchr(pcafileName, '/') != NULL)
    {
        FHALT("LittlFS: Invalid File Name: %s", pcafileName);
        return false;        
    }
    if(pcafilePath[0] != '/')
    {
        FHALT("LittlFS: Invalid File Path Name: %s", pcafilePath);
        return false;        
    }
    if(strncmp(pcafilePath, carootDir, uirootDirLen) != 0 ||
       (pcafilePath[uirootDirLen] != '\0' && pcafilePath[uirootDirLen] != '/'))
    {
        FHALT("LittlFS: Invalid File Path, since it doesnot contain '/lfs' @FilePath: %s", pcafilePath);
        return false;        
    }
    if(bContains_DotPathComponent(pcafilePath))
    {
        FHALT("LittleFS: Invalid directory path: %s", pcafilePath);
        return false;        
    }
    if(strcmp(pcafileName, ".") == 0 || strcmp(pcafileName, "..") == 0)
    {
        FHALT("LittlFS: Invalid Characters inside the Filename @FileName: %s", pcafileName);
        return false;        
    }
    if(stTFileCatalog.uiFileCount >= MAX_FILE_COUNT)
    {
        FHALT("LittlFS: Maximum number of files reached @Count: %d", stTFileCatalog.uiFileCount);
        return false;        
    }

    const char *pcaSeparator = (pcafilePath[uiFilePathLen - 1U] == '/')? "":"/";

    int iPathLength = snprintf(caFullPath, MAX_FILE_PATH_LENGTH, "%s%s%s", 
                               pcafilePath, pcaSeparator, pcafileName);
    if(iPathLength < 0 || (size_t)iPathLength >= MAX_FILE_PATH_LENGTH)
        return false;

    return true;
}

static bool bContains_DotPathComponent(const char *pcaPath)
{
    size_t uiPathLen = strlen(pcaPath);

    if(strstr(pcaPath, "/../") != NULL ||
       strstr(pcaPath, "/./") != NULL)
    {
        return true;
    }

    if((uiPathLen >= 3U &&
        strcmp(&pcaPath[uiPathLen - 3U], "/..") == 0) ||
       (uiPathLen >= 2U &&
        strcmp(&pcaPath[uiPathLen - 2U], "/.") == 0))
    {
        return true;
    }

    return false;
}

static int iCheck_FileAvailability(const char *caFullPath, bool *pbIsAvailable)
{
    struct fs_dirent stPathInfo;
    int iResult = fs_stat(caFullPath, &stPathInfo);

    if(iResult == 0)
    {
        *pbIsAvailable = true;
        return 0;
    }
    if(iResult == -ENOENT)
    {
        *pbIsAvailable = false;
        return 0;        
    }

    return iResult;
}

static int iCheckOrCreateDirectory(const char *caDirectoryPath, bool *pbIsAvailable)
{
    if(caDirectoryPath == NULL)
    {
        FHALT("LittlFS: Null Pointer for Directory Path");
        return -EINVAL;
    }

    struct fs_dirent stTDirInfo;
    int iResult = fs_stat(caDirectoryPath, &stTDirInfo);
    if(iResult == 0)
    {
        if(stTDirInfo.type != FS_DIR_ENTRY_DIR)
        {
            *pbIsAvailable = false;
            return -ENOTDIR;
        }

        *pbIsAvailable = true;
        return iResult;
    }

    if(iResult != -ENOENT)
    {
        *pbIsAvailable = false;
        return iResult;        
    }

    iResult = fs_mkdir(caDirectoryPath);
    if(iResult != 0)
    {
        *pbIsAvailable = false;
        return iResult;        
    }

    *pbIsAvailable = true;
    return 0;
}

const sT_FileInfo_t *const pstGetFileInfo( void )
{
    return stTFileCatalog.staFileInfo;
}

bool bDelete_Directory(const char *pcaDirectoryPath)
{
    if(pcaDirectoryPath == NULL)
    {
        FHALT("LittleFS: Null Pointer reference for the Directory Path");
        return false;        
    }
    if(bIsBulkTransferActive())
    {
        return false;
    }

    size_t uiDirectoryPathLength = strnlen(pcaDirectoryPath, MAX_FILE_PATH_LENGTH);
    if(uiDirectoryPathLength == 0U || uiDirectoryPathLength >= MAX_FILE_PATH_LENGTH)
    {
        FHALT("LittleFS: Invalid directory path length");
        return false;        
    }

    char caDirectoryPath[MAX_FILE_PATH_LENGTH] = {'\0'};
    memcpy(caDirectoryPath, pcaDirectoryPath, uiDirectoryPathLength + 1U);

    while(uiDirectoryPathLength > 1U &&
          caDirectoryPath[uiDirectoryPathLength - 1U] == '/')
    {
        uiDirectoryPathLength--;
        caDirectoryPath[uiDirectoryPathLength] = '\0';
    }

    static const char caRootDirectory[] = EXT_LITTLEFS_MOUNT_POINT;
    const size_t uiRootDirectoryLength = sizeof(caRootDirectory) - 1U;

    if(uiDirectoryPathLength <= uiRootDirectoryLength ||
       strncmp(caDirectoryPath, caRootDirectory, uiRootDirectoryLength) != 0 ||
       caDirectoryPath[uiRootDirectoryLength] != '/' ||
       bContains_DotPathComponent(caDirectoryPath))
    {
        FHALT("LittleFS: Invalid or protected directory path: %s", caDirectoryPath);
        return false;        
    }

    if(k_mutex_lock(&kMutex_FileAccess, K_MSEC(100)) != 0)
    {
        FHALT("LittleFS: File System is accessed by another thread. Cannot proceed and exiting after time out");
        return false;        
    }

    struct fs_dirent sTFile;

    int iResult = fs_stat(caDirectoryPath, &sTFile);
    if(iResult != 0)
    {
        k_mutex_unlock(&kMutex_FileAccess);
        FHALT("LittleFS: Directory delete failed with Error : %d", iResult);
        return false;
    }
    if(sTFile.type != FS_DIR_ENTRY_DIR)
    {
        k_mutex_unlock(&kMutex_FileAccess);
        FHALT("LittleFS: The Entry is not Folder Type");
        return false;        
    }

    iResult = iDelete_AllFiles_InsideDirectory(caDirectoryPath);
    if(iResult == 0)
    {
        iResult = fs_unlink(caDirectoryPath);
    }

    k_mutex_unlock(&kMutex_FileAccess);

    int iCatalogResult = iGetFileInfo(EXT_LITTLEFS_MOUNT_POINT);
    if(iResult != 0)
    {
        FHALT("LittleFS: Recursive directory delete failed with Error : %d", iResult);
        return false;
    }
    if(iCatalogResult != 0)
    {
        return false;
    }
    
    return true;
}

static int iDelete_AllFiles_InsideDirectory(const char *pcaDirectoryPath)
{
    static char caDirectoryList[MAX_DIRECTORY_COUNT][MAX_FILE_PATH_LENGTH];
    static struct fs_dirent stEntry;
    static char caChildPath[MAX_FILE_PATH_LENGTH];

    if(pcaDirectoryPath == NULL)
    {
        FHALT("LittleFS: Null Pointer reference for the Directory Path");
        return -EINVAL;
    }

    size_t uiRootPathLength = strnlen(pcaDirectoryPath, MAX_FILE_PATH_LENGTH);
    if(uiRootPathLength == 0U || uiRootPathLength >= MAX_FILE_PATH_LENGTH)
    {
        return -ENAMETOOLONG;
    }

    memset(caDirectoryList, 0, sizeof(caDirectoryList));
    memcpy(caDirectoryList[0], pcaDirectoryPath, uiRootPathLength + 1U);

    size_t uiTotalDirectoryCount = 1U;

    /*
     * First pass: discover every child directory without deleting anything.
     * This verifies that the bounded worklist can represent the complete tree
     * before the destructive pass begins.
     */
    for(size_t uiScanIndex = 0U;
        uiScanIndex < uiTotalDirectoryCount;
        uiScanIndex++)
    {
        struct fs_dir_t stDirectory;
        fs_dir_t_init(&stDirectory);

        int iResult = fs_opendir(&stDirectory,
                                 caDirectoryList[uiScanIndex]);
        if(iResult != 0)
        {
            return iResult;
        }

        while(true)
        {
            memset(&stEntry, 0, sizeof(stEntry));
            iResult = fs_readdir(&stDirectory, &stEntry);
            if(iResult != 0 || stEntry.name[0] == '\0')
            {
                break;
            }

            if(strcmp(stEntry.name, ".") == 0 || strcmp(stEntry.name, "..") == 0)
            {
                continue;
            }

            if(stEntry.type != FS_DIR_ENTRY_DIR)
            {
                continue;
            }

            if(uiTotalDirectoryCount >= MAX_DIRECTORY_COUNT)
            {
                iResult = -ENOSPC;
                break;
            }

            const char *pcaCurrentDirectory =
                caDirectoryList[uiScanIndex];
            const size_t uiParentPathLength =
                strlen(pcaCurrentDirectory);
            const char *pcaSeparator =
                (uiParentPathLength > 0U &&
                 pcaCurrentDirectory[uiParentPathLength - 1U] == '/') ? "" : "/";

            int iChildPathLength = snprintf(
                                            caDirectoryList[uiTotalDirectoryCount],
                                            MAX_FILE_PATH_LENGTH,
                                            "%s%s%s",
                                            pcaCurrentDirectory,
                                            pcaSeparator,
                                            stEntry.name);
            if(iChildPathLength < 0 ||
               (size_t)iChildPathLength >= MAX_FILE_PATH_LENGTH)
            {
                iResult = -ENAMETOOLONG;
                break;
            }

            uiTotalDirectoryCount++;
        }

        int iCloseResult = fs_closedir(&stDirectory);
        if(iResult == 0 && iCloseResult != 0)
        {
            iResult = iCloseResult;
        }
        if(iResult != 0)
        {
            return iResult;
        }
    }

    /*
     * Second pass: process deepest directories first. Each iteration closes
     * the directory before unlinking an entry, so no iterator remains open
     * while LittleFS metadata is modified.
     */
    for(size_t uiDirectoryIndex = uiTotalDirectoryCount;
        uiDirectoryIndex > 0U;
        uiDirectoryIndex--)
    {
        const char *pcaCurrentDirectory =
            caDirectoryList[uiDirectoryIndex - 1U];

        while(true)
        {
            struct fs_dir_t stDirectory;
            enum fs_dir_entry_type eEntryType = FS_DIR_ENTRY_FILE;
            bool bEntryFound = false;

            fs_dir_t_init(&stDirectory);
            int iResult = fs_opendir(&stDirectory,
                                     pcaCurrentDirectory);
            if(iResult != 0)
            {
                return iResult;
            }

            while(true)
            {
                memset(&stEntry, 0, sizeof(stEntry));
                iResult = fs_readdir(&stDirectory, &stEntry);
                if(iResult != 0 || stEntry.name[0] == '\0')
                {
                    break;
                }

                if(strcmp(stEntry.name, ".") == 0 ||
                   strcmp(stEntry.name, "..") == 0)
                {
                    continue;
                }

                const size_t uiParentPathLength =
                    strlen(pcaCurrentDirectory);
                const char *pcaSeparator =
                    (uiParentPathLength > 0U &&
                     pcaCurrentDirectory[uiParentPathLength - 1U] == '/') ? "" : "/";

                int iChildPathLength = snprintf(caChildPath,
                                                sizeof(caChildPath),
                                                "%s%s%s",
                                                pcaCurrentDirectory,
                                                pcaSeparator,
                                                stEntry.name);
                if(iChildPathLength < 0 ||
                   (size_t)iChildPathLength >= sizeof(caChildPath))
                {
                    iResult = -ENAMETOOLONG;
                    break;
                }

                eEntryType = stEntry.type;
                bEntryFound = true;
                break;
            }

            int iCloseResult = fs_closedir(&stDirectory);
            if(iResult == 0 && iCloseResult != 0)
            {
                iResult = iCloseResult;
            }
            if(iResult != 0)
            {
                return iResult;
            }
            if(!bEntryFound)
            {
                break;
            }
            if(eEntryType != FS_DIR_ENTRY_FILE)
            {
                return -ENOTEMPTY;
            }

            iResult = fs_unlink(caChildPath);
            if(iResult != 0)
            {
                return iResult;
            }
        }

        /* The caller removes the requested top-level directory itself. */
        if(uiDirectoryIndex > 1U)
        {
            int iResult = fs_unlink(pcaCurrentDirectory);
            if(iResult != 0)
            {
                return iResult;
            }
        }
    }

    return 0;
}

int iDelete_File(const char *pcaFilePath)
{
    if(pcaFilePath == NULL)
    {
        FHALT("LittleFS: Null pointer for file path");
        return -EINVAL;
    }
    
    if(bIsBulkTransferActive())
    {
        return -EBUSY;
    }

    return iDelete_FileInternal(pcaFilePath);
}

static int iDelete_FileInternal(const char *pcaFilePath)
{
    sT_FileInfo_t *pstFile = pstGetFile_ByFilePath(pcaFilePath);
    if(pstFile == NULL)
    {
        return -ENOENT;
    }

    if(k_mutex_lock(&kMutex_FileAccess, K_MSEC(100)) != 0)
    {
        FHALT("LittleFS: File System is accessed by another thread. Cannot proceed and exiting after time out");
        return -EIO;        
    }

    struct fs_dirent sTFile;
    int iResult = fs_stat(pstFile->caFilePath, &sTFile);
    if(iResult != 0)
    {
        k_mutex_unlock(&kMutex_FileAccess);
        FHALT("LittleFS: File access failed with Error : %d", iResult);
        return iResult;
    }

    iResult = fs_unlink(pstFile->caFilePath);
    if(iResult != 0)
    {
        k_mutex_unlock(&kMutex_FileAccess);
        FHALT("LittleFS: File access failed with Error : %d", iResult);
        return iResult;
    }

    k_mutex_unlock(&kMutex_FileAccess);
    iResult = iGetFileInfo(EXT_LITTLEFS_MOUNT_POINT);
    printf("\n\rFile Deleted!!!\n\r");
    return iResult;
}

static sT_FileInfo_t *pstGetFile_ByFileName(const char *pcaFileName)
{
    if(pcaFileName == NULL)
    {
        FHALT("LittleFS: Null Pointer reference for the File Name");
        return NULL;
    }

    for(uint8_t i = 0; i < stTFileCatalog.uiFileCount; i++)
    {
        if(strcmp(pstFileInfo[i].caFileName, pcaFileName) == 0)
        {
            return &pstFileInfo[i];
        }
    }
    return NULL;
}

static sT_FileInfo_t *pstGetFile_ByFilePath(const char *pcaFilePath)
{
    if(pcaFilePath == NULL)
    {
        FHALT("LittleFS: Null Pointer reference for the File Name");
        return NULL;
    }

    for(uint8_t i = 0; i < stTFileCatalog.uiFileCount; i++)
    {
        if(strcmp(pstFileInfo[i].caFilePath, pcaFilePath) == 0)
        {
            return &pstFileInfo[i];
        }
    }
    return NULL;
}

int iWrite_ToFileBin(const char *pcFileName, const uint8_t *puiDataBuff, size_t uiLen, uint16_t uiMaxWriteSize)
{
    if(pcFileName == NULL)
    {
        FHALT("LittleFS: Null Pointer for file name");
        return -EINVAL;
    }
    if(puiDataBuff == NULL)
    {
        FHALT("LittleFS: Null Pointer for data buffer");
        return -EINVAL;        
    }
    if(uiLen == 0U)
    {
        FHALT("LittleFS: Invalid Write Length for data buffer");
        return -EINVAL;        
    }
    if(uiMaxWriteSize == 0U)
    {
        FHALT("LittleFS: Invalid Write Length for data buffer");
        return -EINVAL;        
    }
    if(bIsBulkTransferActive())
    {
        return -EBUSY;
    }

    if(k_mutex_lock(&kMutex_FileAccess, K_MSEC(100)) != 0)
    {
        FHALT("LittleFS: File cannot be accessed, since File System is busy");
        return -EBUSY;        
    }

    sT_FileInfo_t *pstFile = pstGetFile_ByFileName(pcFileName);
    if(pstFile == NULL)
    {
        k_mutex_unlock(&kMutex_FileAccess);
        FHALT("LittleFS: Requested file does not exist. Create the file first");
        return -ENOENT;        
    }

    struct fs_file_t stTFile;
    fs_file_t_init(&stTFile);

    int iResult = fs_open(&stTFile, pstFile->caFilePath, FS_O_APPEND | FS_O_WRITE);
    if(iResult != 0)
    {
        k_mutex_unlock(&kMutex_FileAccess);
        FHALT("LittleFS: File Access Error: %d", iResult);
        return iResult;        
    }

    size_t uiWriteSize = 0U;
    size_t uiRemainingLen = uiLen - uiWriteSize;
    size_t uiIndex = 0;
    ssize_t uiTotWrittenBytes = 0;

    while(uiRemainingLen > 0U)
    {
        if(uiRemainingLen > uiMaxWriteSize)
        {
            uiWriteSize = uiMaxWriteSize;
        }
        else
        {
            uiWriteSize = uiRemainingLen;
        }

        uiTotWrittenBytes = fs_write(&stTFile, &puiDataBuff[uiIndex], uiWriteSize);
        if(uiTotWrittenBytes <= 0)
        {
            //Do we need to restore the file
            fs_close(&stTFile);
            k_mutex_unlock(&kMutex_FileAccess);

            iResult = (uiTotWrittenBytes == 0)? -EIO: uiTotWrittenBytes;
            return iResult;
        }

        uiRemainingLen -= uiTotWrittenBytes;
        uiIndex += uiTotWrittenBytes;
    }

    iResult = fs_sync(&stTFile);

    int iCloseResult = fs_close(&stTFile);
    
    struct fs_dirent stFileInfo;
    memset(&stFileInfo, 0, sizeof(stFileInfo));
    int iStatResult = fs_stat(pstFile->caFilePath, &stFileInfo);
    if(iStatResult == 0)
    {
        pstFile->uiFileSize = stFileInfo.size;
    }
    k_mutex_unlock(&kMutex_FileAccess);

    if(iResult < 0)
    {
        return iResult;
    }
    if(iCloseResult < 0)
    {
        return iCloseResult;
    }
    if(iStatResult < 0)
    {
        return iStatResult;
    }
    return 0;
}
