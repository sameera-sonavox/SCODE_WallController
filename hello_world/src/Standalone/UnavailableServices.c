/* Explicitly unavailable services for a board without external flash. */
#include <errno.h>
#include "../Bootloader_Controller/Bootloader_Ctrl.h"
#include "../ExtFlash_Controller/LittleFsController/LittleFs_Controller.h"

bool bInit_BootloaderController(void) { return false; }
bool bIsBootloader_Initialized(void) { return false; }
bool bIsFW_ImageWrite_InProgress(void) { return false; }
void vConfirm_MCUbootImage(void) { }
void vUpdateBootloader(sT_Bootloader_CtrlMsg_t *message)
{
    vSet_BootloaderMsgStatus(message, false, eBootloader_Error_NotInitialized);
}
void vHandle_HostAcknowledgements(sT_Bootloader_CtrlMsg_t *message)
{
    vUpdateBootloader(message);
}

bool bInit_ExtFlash_FsController(void) { return false; }
const sT_FileInfo_t *const pstGetFileInfo(void) { return NULL; }
int iCreate_File(const char *path, const char *name)
{ (void)path; (void)name; return -ENODEV; }
int iDelete_File(const char *path)
{ (void)path; return -ENODEV; }
bool bDelete_Directory(const char *path)
{ (void)path; return false; }
int iWrite_ToFileBin(const char *path, const uint8_t *data, size_t len, uint16_t max)
{ (void)path; (void)data; (void)len; (void)max; return -ENODEV; }
int iRead_FromFileBin(const char *path, uint8_t *data, size_t len)
{ (void)path; (void)data; (void)len; return -ENODEV; }
bool bStart_BulkDataTransfer(sT_BulkTransferData transfer)
{ (void)transfer; return false; }
void vEnd_BulkDataTransfer(sT_TransferResult_t *result)
{ (void)result; }
int iSend_BulkTransferData(const sT_MsgData_t *message)
{ (void)message; return -ENODEV; }
