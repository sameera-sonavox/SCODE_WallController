#ifndef BOOTLOADER_TYPEDEF_H
#define BOOTLOADER_TYPEDEF_H

#include <stdint.h>
#include <stdbool.h>

#define MAX_NUMBER_OF_ALLOWABLE_LOST_PACKETs    100

//Commands
typedef enum{
    eBootloader_State_Inactive = 0,
    eBootloader_State_Idle,   
    eBootloader_State_FWImgWrite_InProg,
    eBootloader_State_FWImgWrite_Completed,
    eBootloader_State_Reboot,
    eBootloader_State_Error,
    eNUMBER_OF_BOOTLOADER_STATEs
} eT_Bootloader_State;

typedef enum{
    eBootloader_CMD_FWUpReq = 20,
    eBootloader_CMD_FWUpMsg,
    eBootloader_CMD_FWUpEnd,
    eBootloader_CMD_FWUpPause,
    eBootloader_CMD_FWUpStop,
    eNUMBER_OF_BOOTLOADER_COMMANDs
} eT_Bootloader_Command;

typedef enum{
    eBootloader_Error_None = 0,
    eBootloader_Error_NullPointer,
    eBootloader_Error_NotInitialized,
    eBootloader_Error_UpdateInProgress,
    eBootloader_Error_InvalidCommand,
    eBootloader_Error_InvalidPayloadLength,
    eBootloader_Error_InvalidImageSize,
    eBootloader_Error_ImageTooLarge,
    eBootloader_Error_ImageIncomplete,
    eBootloader_Error_CrcMismatch,
    eBootloader_Error_InvalidPacketId,
    eBootloader_Error_FlashAreaOpenFail,
    eBootloader_Error_FlashAreaEraseFail,
    eBootloader_Error_FlashAreaWriteFail,
    eBootloader_Error_FlashBoundsExceeded,
    eBootloader_Error_UpgradeRequestFail,
} eT_Bootloader_ErrorCode;

typedef struct
{
    eT_Bootloader_Command eCMD;
    bool bIsMsgOk;
    bool bIsACKReq;
    eT_Bootloader_ErrorCode eErrorCode;
    uint8_t uiLen;
    uint8_t uiaData[CAN_MSG_MAX_SIZE];
} sT_Bootloader_CtrlMsg_t;

static inline void vSet_BootloaderMsgStatus(sT_Bootloader_CtrlMsg_t *pstBtlMsg,
                                            bool bIsMsgOk,
                                            eT_Bootloader_ErrorCode eErrorCode)
{
    if(pstBtlMsg != NULL)
    {
        pstBtlMsg->bIsMsgOk = bIsMsgOk;
        pstBtlMsg->eErrorCode = eErrorCode;
    }
}

typedef struct{
    bool bIsFlashWritInProgress;
    uint32_t uiReceivedByteCount;
    uint32_t uiRunningCRC;
    uint16_t uiNextId;
    uint16_t uiLastId;
    uint16_t uiaLostPacketIDs[MAX_NUMBER_OF_ALLOWABLE_LOST_PACKETs];
    uint8_t uiLostPacketCount;
} sT_FWImg_Ctrl_t;

typedef struct{
    eT_Bootloader_State eBtlState;
    bool bIsInitialized;
    size_t uiFlashAreaSize;   
    uint32_t uiFWImgSize;
    uint16_t uiLibRev;
    uint16_t uiFWRev;
    uint16_t uiCRC;
    uint8_t uiHWRev;
    sT_FWImg_Ctrl_t stTFWImgCtrl_t;
} sT_Bootloader_Mgmt_t;

#endif
