#include <stdint.h>
#include <stdbool.h>
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/sys/reboot.h>

#include "../Lib/CAN/NXP_CAN_API.h"
#include "Bootloader_Ctrl.h"

#if defined(DEBUG_BOOTLOADER)
    #define Bootloader_Print                    printk
#else
    #define Bootloader_Print(...)
#endif

sT_Bootloader_Mgmt_t stTBtlMgmt_t = {0};
const struct flash_area *flashArea;
static struct k_work_delayable stBtlStateTimeOut_Work_t;

#define IS_FWUpdate_InProgress()                (stTBtlMgmt_t.eBtlState == eBootloader_State_FWImgWrite_InProg)
#define sTFWImg                                 stTBtlMgmt_t.stTFWImgCtrl_t

#define vSet_FlashWriteFlag()                   sTFWImg.bIsFlashWritInProgress = true
#define vClear_FlashWriteFlag()                 sTFWImg.bIsFlashWritInProgress = false
#define bIsFlashWriteInProgress()               sTFWImg.bIsFlashWritInProgress

void vSet_BootloaderState( eT_Bootloader_State eState );
eT_Bootloader_State eGet_BootloaderState( void );
void vBtlState_TimeOutHandler( struct k_work *work );
bool bIsValidFWImgWrite_Command( sT_Bootloader_CtrlMsg_t * pstTBootMsg );
void vErase_FlashArea( sT_Bootloader_CtrlMsg_t * pstTBootMsg );

void vExecute_Bootloader_IdleState( sT_Bootloader_CtrlMsg_t * pstTBootMsg );
void vExecute_Bootloader_FWImgWrite_State( sT_Bootloader_CtrlMsg_t * pstTBootMsg );
void vExecute_Bootloader_FWImgWrite_Completed( sT_Bootloader_CtrlMsg_t * pstTBootMsg );
void vExecute_Bootloader_Reboot( sT_Bootloader_CtrlMsg_t * pstTBootMsg );
void vAbort_BootloaderSession( void );
void vUpdate_FWImage( sT_Bootloader_CtrlMsg_t * pstTBootMsg );
void vFinalize_FWImageWrite( sT_Bootloader_CtrlMsg_t * pstTBootMsg );
void vStop_FWUpgradeProcess( sT_Bootloader_CtrlMsg_t * pstTBootMsg );

static uint16_t u16CRC16_CCITT_Update(uint16_t crc, const uint8_t *data, uint32_t len);

void vUpdateBootloader( sT_Bootloader_CtrlMsg_t * pstTBootMsg)
{
    switch(eGet_BootloaderState())
    {
        case eBootloader_State_Inactive:
            FHALT("Bootloader Not Initialized");
            break;
        case eBootloader_State_Idle:
            vExecute_Bootloader_IdleState(pstTBootMsg);
            break;
        case eBootloader_State_FWImgWrite_InProg:
            vExecute_Bootloader_FWImgWrite_State(pstTBootMsg);
            break;
        case eBootloader_State_FWImgWrite_Completed:
            vExecute_Bootloader_FWImgWrite_Completed(pstTBootMsg);
            break;
        case eBootloader_State_Reboot:
            vExecute_Bootloader_Reboot(pstTBootMsg);
            break;
        case eBootloader_State_Error:
            //If the bootloader goes to this state it latched here. Until it is cleared by the host
            break;
        default:
            FHALT("Invalid Bootloader State");
            break;   
    }
}

void vExecute_Bootloader_Reboot( sT_Bootloader_CtrlMsg_t * pstTBootMsg )
{
    k_work_cancel_delayable(&stBtlStateTimeOut_Work_t);
    //It is better to indicate to the host via a dedicated HW pin here
    //to be done
    Bootloader_Print("System rebooting for new FW update...\n\r");
    sys_reboot(SYS_REBOOT_COLD);
}

void vExecute_Bootloader_FWImgWrite_Completed( sT_Bootloader_CtrlMsg_t * pstTBootMsg )
{
    int count = 0;
    if(pstTBootMsg == NULL)
    {
        FHALT("Null pointer reference");
        return;        
    }

    k_work_cancel_delayable(&stBtlStateTimeOut_Work_t);
    if(flashArea != NULL)
    {
        flash_area_close(flashArea);
        flashArea = NULL;
    }

    while(count < 3)
    {
        int ret = boot_request_upgrade(BOOT_UPGRADE_TEST);
        if(ret != 0)
        {
            FHALT("FW Image Upgrade Request Fail. Retrying : %d", count);
            count++;
            k_msleep(10);
            continue;
        }

        break;
    }
    
    if(count >= 3)
    {
        vSet_BootloaderMsgStatus(pstTBootMsg, false, eBootloader_Error_UpgradeRequestFail);
        vAbort_BootloaderSession();
        return;        
    }

    vSet_BootloaderMsgStatus(pstTBootMsg, true, eBootloader_Error_None);
    vSet_BootloaderState(eBootloader_State_Reboot);
    k_work_reschedule(&stBtlStateTimeOut_Work_t, K_MSEC(TIMEOUT_REBOOT_CONFIRMATION_ms));
}

void vExecute_Bootloader_FWImgWrite_State( sT_Bootloader_CtrlMsg_t * pstTBootMsg )
{
    if(!bIsBootloader_Initialized())
    {
        FHALT("Bootloader not initialized");
        return;
    }
    if(pstTBootMsg == NULL)
    {
        FHALT("Null pointer reference");
        return;
    }
    if(!bIsValidFWImgWrite_Command(pstTBootMsg))
    {
        FHALT("Bootloader not initialized");
        vSet_BootloaderMsgStatus(pstTBootMsg, false, eBootloader_Error_InvalidCommand);
        return;
    }

    switch(pstTBootMsg->eCMD)
    {
        case eBootloader_CMD_FWUpStop:
            vStop_FWUpgradeProcess(pstTBootMsg);
            break;
        case eBootloader_CMD_FWUpEnd:
            vFinalize_FWImageWrite(pstTBootMsg);
            break;
        case eBootloader_CMD_FWUpPause:
            break;
        case eBootloader_CMD_FWUpMsg:
            vUpdate_FWImage(pstTBootMsg);
            break;
        default:
            FHALT("Invalid ID at FW Image Write State");
    }

}

void vStop_FWUpgradeProcess( sT_Bootloader_CtrlMsg_t * pstTBootMsg )
{
    if(pstTBootMsg == NULL)
    {
        return;
    }

    vSet_BootloaderMsgStatus(pstTBootMsg, true, eBootloader_Error_None);
    vAbort_BootloaderSession();
}

void vFinalize_FWImageWrite( sT_Bootloader_CtrlMsg_t * pstTBootMsg )
{
    if(pstTBootMsg == NULL)
    {
        FHALT("Null Pointer reference");
        return;
    }
    if(sTFWImg.uiReceivedByteCount != stTBtlMgmt_t.uiFWImgSize)
    {
        FHALT("FW Image is still not completed to mark it completed!!!");
        vSet_BootloaderMsgStatus(pstTBootMsg, false, eBootloader_Error_ImageIncomplete);
        vAbort_BootloaderSession();
        return;
    }
    if(sTFWImg.uiRunningCRC != stTBtlMgmt_t.uiCRC)
    {
        FHALT("CRC Mismatch (Calculated: %d, Received: %d)", sTFWImg.uiRunningCRC, stTBtlMgmt_t.uiCRC);
        vSet_BootloaderMsgStatus(pstTBootMsg, false, eBootloader_Error_CrcMismatch);
        vAbort_BootloaderSession();
        return;
    }

    vSet_BootloaderState(eBootloader_State_FWImgWrite_Completed);
    vUpdateBootloader(pstTBootMsg);
}

void vUpdate_FWImage( sT_Bootloader_CtrlMsg_t * pstTBootMsg )
{
    if(pstTBootMsg == NULL)
        return;
    if(bIsFlashWriteInProgress())
    {
        FHALT("Flash Update Session Active. Abort current update");
        vSet_BootloaderMsgStatus(pstTBootMsg, false, eBootloader_Error_UpdateInProgress);
        return;
    }
    if(pstTBootMsg->uiLen != FW_IMG_PACKET_DATA_LENGTH)
    {
        FHALT("Invalid Packet Length : %d", pstTBootMsg->uiLen);
        vSet_BootloaderMsgStatus(pstTBootMsg, false, eBootloader_Error_InvalidPayloadLength);
        return;
    }
    
    bool bIsFirstPacket = false;
    int ret = 0;
    uint8_t uiIndex = 0;
    uint8_t uiaWriteBlock[FW_IMG_WRITE_BYTE_LENGTH];
    uint32_t uiOffset = 0, uiWriteLen = 0;
    uint16_t uipacketId = (uint16_t)(pstTBootMsg->uiaData[0] << 8) | (uint16_t)pstTBootMsg->uiaData[1];
    uiIndex = 2;

    if(sTFWImg.uiLastId == 0 && sTFWImg.uiNextId == 0)
    {
        if(uipacketId != 0)
        {
            vSet_BootloaderMsgStatus(pstTBootMsg, false, eBootloader_Error_InvalidPacketId);
            vAbort_BootloaderSession();
            return;
        }
        bIsFirstPacket = true;
    }

    if(sTFWImg.uiNextId != uipacketId)
    {
        FHALT("Invalid Packet Id. Expected: %d, Received: %d", sTFWImg.uiNextId, uipacketId);
        vSet_BootloaderMsgStatus(pstTBootMsg, false, eBootloader_Error_InvalidPacketId);
        vAbort_BootloaderSession();
        return;
    }
    
    vSet_FlashWriteFlag();
    sTFWImg.uiLastId = uipacketId;
    sTFWImg.uiNextId = uipacketId + 1;
    if(bIsFirstPacket)
    {
        ret = flash_area_open(FLASH_AREA_FW_IMAGE_STORE_ID, &flashArea);
        if(ret != 0)
        {
            FHALT("Flash Area Couldn't be opened");
            vSet_BootloaderMsgStatus(pstTBootMsg, false, eBootloader_Error_FlashAreaOpenFail);
            vAbort_BootloaderSession();
            return;
        }
    }

    for(; uiIndex < pstTBootMsg->uiLen; uiIndex += FW_IMG_WRITE_BYTE_LENGTH)
    {
        uiOffset = sTFWImg.uiReceivedByteCount;
        if(uiOffset > stTBtlMgmt_t.uiFlashAreaSize)
        {
            FHALT("FW Bytes exceed available flash area");
            vSet_BootloaderMsgStatus(pstTBootMsg, false, eBootloader_Error_FlashBoundsExceeded);
            vAbort_BootloaderSession();
            return;
        }
        if(uiOffset >= stTBtlMgmt_t.uiFWImgSize)
        {
            FHALT("FW Offset exceed FW Image Size");
            vSet_BootloaderMsgStatus(pstTBootMsg, false, eBootloader_Error_ImageTooLarge);
            vAbort_BootloaderSession();
            return;            
        }

        if(uiOffset + FW_IMG_WRITE_BYTE_LENGTH <= stTBtlMgmt_t.uiFWImgSize)
            uiWriteLen = FW_IMG_WRITE_BYTE_LENGTH;
        else
            uiWriteLen = stTBtlMgmt_t.uiFWImgSize - uiOffset;

        if(uiOffset + FW_IMG_WRITE_BYTE_LENGTH > stTBtlMgmt_t.uiFlashAreaSize)
        {
            FHALT("FW Bytes exceed available flash area");
            vSet_BootloaderMsgStatus(pstTBootMsg, false, eBootloader_Error_FlashBoundsExceeded);
            vAbort_BootloaderSession();
            return;
        }

        memset(uiaWriteBlock, 0xFF, sizeof(uiaWriteBlock));
        memcpy(uiaWriteBlock, &pstTBootMsg->uiaData[uiIndex], uiWriteLen);
        sTFWImg.uiRunningCRC = u16CRC16_CCITT_Update(sTFWImg.uiRunningCRC, &pstTBootMsg->uiaData[uiIndex], uiWriteLen);

        ret = flash_area_write(flashArea, uiOffset, uiaWriteBlock, sizeof(uiaWriteBlock));
        if(ret != 0)
        {
            FHALT("Flash Area write fail");
            vSet_BootloaderMsgStatus(pstTBootMsg, false, eBootloader_Error_FlashAreaWriteFail);
            vAbort_BootloaderSession();
            return;
        }
        sTFWImg.uiReceivedByteCount += uiWriteLen;
        if(sTFWImg.uiReceivedByteCount >= stTBtlMgmt_t.uiFWImgSize)
        {
            break;
        }
    }

    vClear_FlashWriteFlag();    
    k_work_reschedule(&stBtlStateTimeOut_Work_t, K_MSEC(TIMEOUT_FW_UPDATE_NextPacket_ms));
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

void vAbort_BootloaderSession( void )
{
    k_work_cancel_delayable(&stBtlStateTimeOut_Work_t);

    if(flashArea != NULL)
    {
        flash_area_close(flashArea);
        flashArea = NULL;
    }

    stTBtlMgmt_t.uiFWImgSize = 0;
    stTBtlMgmt_t.uiFWRev = 0;
    stTBtlMgmt_t.uiHWRev = 0;
    stTBtlMgmt_t.uiLibRev = 0;
    stTBtlMgmt_t.uiCRC = 0;
    
    sTFWImg.uiLastId = 0;
    sTFWImg.uiLostPacketCount = 0;
    sTFWImg.uiNextId = 0;
    sTFWImg.uiReceivedByteCount = 0;
    sTFWImg.uiRunningCRC = 0xFFFF;
    sTFWImg.bIsFlashWritInProgress = false;
    memset(sTFWImg.uiaLostPacketIDs, 0, sizeof(sTFWImg.uiaLostPacketIDs));
    vSet_BootloaderState(eBootloader_State_Error);

}

bool bIsValidFWImgWrite_Command( sT_Bootloader_CtrlMsg_t * pstTBootMsg )
{
    if(pstTBootMsg == NULL)
        return false;

    return (pstTBootMsg->eCMD == eBootloader_CMD_FWUpMsg ||
            pstTBootMsg->eCMD == eBootloader_CMD_FWUpEnd ||
            pstTBootMsg->eCMD == eBootloader_CMD_FWUpPause ||
            pstTBootMsg->eCMD == eBootloader_CMD_FWUpStop);
}

void vSet_BootloaderState( eT_Bootloader_State eState )
{
    if(eState >= eNUMBER_OF_BOOTLOADER_STATEs)
    {
        FHALT("Invalid Bootloader State Requested");
        return;
    }

    switch(eState)
    {
        case eBootloader_State_FWImgWrite_InProg:
            k_work_reschedule(&stBtlStateTimeOut_Work_t, K_MSEC(TIMEOUT_FW_UPREQ_TO_FW_UPDATE_ms));
            break;
        default:
            k_work_cancel_delayable(&stBtlStateTimeOut_Work_t);
            break;
    }
    stTBtlMgmt_t.eBtlState = eState;
}

eT_Bootloader_State eGet_BootloaderState( void )
{
    return stTBtlMgmt_t.eBtlState;
}

void vExecute_Bootloader_IdleState( sT_Bootloader_CtrlMsg_t * pstTBootMsg )
{
    if(pstTBootMsg == NULL)
    {
        FHALT("Null Pointer Reference");
        return;
    }
    if(!bIsBootloader_Initialized())
    {
        FHALT("Must call 'vInit_BootloaderController' before anything");
        vSet_BootloaderMsgStatus(pstTBootMsg, false, eBootloader_Error_NotInitialized);
        return;
    }
    if(IS_FWUpdate_InProgress())
    {
        FHALT("FW Update Already in Progress");
        vSet_BootloaderMsgStatus(pstTBootMsg, false, eBootloader_Error_UpdateInProgress);
        return;
    }
    if(pstTBootMsg->eCMD != eBootloader_CMD_FWUpReq)
    {
        FHALT("Invalid Command for '%s' @CMD: %d", __func__, pstTBootMsg->eCMD);
        vSet_BootloaderMsgStatus(pstTBootMsg, false, eBootloader_Error_InvalidCommand);
        return;
    }
    if(pstTBootMsg->uiLen != 10)
    {
        FHALT("Invalid Payload Length for CMD = %d", pstTBootMsg->eCMD);
        vSet_BootloaderMsgStatus(pstTBootMsg, false, eBootloader_Error_InvalidPayloadLength);
        return;
    }

    stTBtlMgmt_t.uiFWImgSize = ((uint32_t)pstTBootMsg->uiaData[0] << 16) | ((uint32_t)pstTBootMsg->uiaData[1] << 8) | pstTBootMsg->uiaData[2];
    if(stTBtlMgmt_t.uiFWImgSize == 0)
    {
        FHALT("Invalid FW Image (Size: %d) ", stTBtlMgmt_t.uiFWImgSize);
        vSet_BootloaderMsgStatus(pstTBootMsg, false, eBootloader_Error_InvalidImageSize);
        return;        
    }
    if(stTBtlMgmt_t.uiFWImgSize > stTBtlMgmt_t.uiFlashAreaSize)
    {
        FHALT("FW Image (Size: %d) larger than available memory(Size: %d)", 
            stTBtlMgmt_t.uiFWImgSize, stTBtlMgmt_t.uiFlashAreaSize);
        vSet_BootloaderMsgStatus(pstTBootMsg, false, eBootloader_Error_ImageTooLarge);
        return;
    }
    
    stTBtlMgmt_t.uiHWRev = pstTBootMsg->uiaData[3];
    stTBtlMgmt_t.uiLibRev = ((uint16_t)pstTBootMsg->uiaData[4] << 8) | pstTBootMsg->uiaData[5];
    stTBtlMgmt_t.uiFWRev = ((uint16_t)pstTBootMsg->uiaData[6] << 8) | pstTBootMsg->uiaData[7];
    stTBtlMgmt_t.uiCRC = ((uint16_t)pstTBootMsg->uiaData[8] << 8) | pstTBootMsg->uiaData[9];
    stTBtlMgmt_t.stTFWImgCtrl_t.uiLostPacketCount = 0;
    sTFWImg.uiRunningCRC = 0xFFFF;
    sTFWImg.bIsFlashWritInProgress = false;
    sTFWImg.uiLastId = 0;
    sTFWImg.uiLostPacketCount = 0;
    sTFWImg.uiNextId = 0;
    sTFWImg.uiReceivedByteCount = 0;
    memset(stTBtlMgmt_t.stTFWImgCtrl_t.uiaLostPacketIDs, 0, sizeof(stTBtlMgmt_t.stTFWImgCtrl_t.uiaLostPacketIDs));

    vErase_FlashArea(pstTBootMsg);
    if(!pstTBootMsg->bIsMsgOk)
        return;
    vSet_BootloaderState(eBootloader_State_FWImgWrite_InProg);
}

void vErase_FlashArea( sT_Bootloader_CtrlMsg_t * pstTBootMsg )
{
    int ret = flash_area_open(FLASH_AREA_FW_IMAGE_STORE_ID, &flashArea);
    if(ret != 0)
    {
        FHALT("Flash Area couldn't be opened with Err: %d", ret);
        vSet_BootloaderMsgStatus(pstTBootMsg, false, eBootloader_Error_FlashAreaOpenFail);
        return;
    }

    ret = flash_area_erase(flashArea, 0, flashArea->fa_size);
    if(ret != 0)
    {
        flash_area_close(flashArea);
        flashArea = NULL;
        FHALT("Flash Area couldn't be erased with Err: %d", ret);
        vSet_BootloaderMsgStatus(pstTBootMsg, false, eBootloader_Error_FlashAreaEraseFail);
        return;        
    }

    flash_area_close(flashArea);
    flashArea = NULL;
    vSet_BootloaderMsgStatus(pstTBootMsg, true, eBootloader_Error_None);
}

void vBtlState_TimeOutHandler( struct k_work *work )
{
    FHALT("FW Update Timeout Occurred at State: %d", eGet_BootloaderState());

    eT_Bootloader_State estate = eGet_BootloaderState();
    if(estate == eBootloader_State_Reboot)
        vUpdateBootloader(NULL);
    else
        vAbort_BootloaderSession();
}

void vInit_BootloaderController( void )
{
    stTBtlMgmt_t.eBtlState = eBootloader_State_Inactive;
    stTBtlMgmt_t.bIsInitialized = false;
    stTBtlMgmt_t.uiCRC = 0;
    stTBtlMgmt_t.uiFWImgSize = 0;
    stTBtlMgmt_t.uiFWRev = 0;
    stTBtlMgmt_t.uiHWRev = 0;
    stTBtlMgmt_t.uiLibRev = 0;
    stTBtlMgmt_t.uiFlashAreaSize = 0;
    
    sTFWImg.uiLastId = 0;
    sTFWImg.uiLostPacketCount = 0;
    sTFWImg.uiNextId = 0;
    sTFWImg.uiReceivedByteCount = 0;
    sTFWImg.uiRunningCRC = 0xFFFF;    
    sTFWImg.bIsFlashWritInProgress = false;
    memset(sTFWImg.uiaLostPacketIDs, 0, sizeof(sTFWImg.uiaLostPacketIDs));

    int ret = flash_area_open(FLASH_AREA_FW_IMAGE_STORE_ID, &flashArea);
    if(ret != 0)
    {
        FHALT("Failed to open Slot1 flash region");
        return;
    }
    stTBtlMgmt_t.uiFlashAreaSize = flashArea->fa_size;
    flash_area_close(flashArea);
    flashArea = NULL;

    k_work_init_delayable(&stBtlStateTimeOut_Work_t, vBtlState_TimeOutHandler);
    stTBtlMgmt_t.eBtlState = eBootloader_State_Idle;
    stTBtlMgmt_t.bIsInitialized = true;
}

bool bIsBootloader_Initialized( void )
{
    return stTBtlMgmt_t.bIsInitialized;
}

bool bIsFW_ImageWrite_InProgress( void )
{
    return (stTBtlMgmt_t.eBtlState == eBootloader_State_FWImgWrite_InProg);
}
