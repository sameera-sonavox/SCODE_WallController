#include "LVGL_ZephyrFS_Adapter.h"
#include "../../ExtFlash_Controller/ExtFlash_ProjDef.h"
#include "GenericMacro.h"

#include <lvgl.h>
#include <lvgl_zephyr.h>
#include <errno.h>
#include <zephyr/fs/fs.h>
#include <stdio.h>

typedef struct
{
    struct fs_file_t stFile;
} sT_LVGL_ZephyrFile_t;

static bool bBuild_ZephyrFilePath(const char *pcaLVGLPath, char *pcaZephyrPath, size_t uiZephyrPathSize);
static lv_fs_res_t eMap_ZephyrError(int iError);

//Callback Functions
static void *pvOpen(lv_fs_drv_t *pstDriver, const char *pcaPath, lv_fs_mode_t eMode);
static lv_fs_res_t eClose( lv_fs_drv_t *pstDriver, void *pvFile);
static lv_fs_res_t eRead(lv_fs_drv_t *pstDriver, void *pvFile, void *pvBuffer, uint32_t uiBytesToRead, uint32_t *puiBytesRead);
static lv_fs_res_t eSeek(lv_fs_drv_t *pstDriver, void *pvFile, uint32_t uiPosition, lv_fs_whence_t eWhence);
static lv_fs_res_t eTell(lv_fs_drv_t *pstDriver, void *pvFile, uint32_t *puiPosition);

static lv_fs_drv_t stTLVGLFSDriver;
static bool bIsDriverRegistered;

bool bInit_LVGL_ZephyrFSAdapter( void )
{
    if(bIsDriverRegistered)
    {
        return true;
    }

    lvgl_lock();

    //Check whether a driver with same name has already been assigned
    if(lv_fs_get_drv(LVGL_ZEPHYR_FS_DRIVE_LETTER) != NULL)
    {
        lvgl_unlock();
        return false;
    }

    lv_fs_drv_init(&stTLVGLFSDriver);

    stTLVGLFSDriver.letter = LVGL_ZEPHYR_FS_DRIVE_LETTER;
    stTLVGLFSDriver.open_cb = pvOpen;
    stTLVGLFSDriver.close_cb = eClose;
    stTLVGLFSDriver.read_cb = eRead;
    stTLVGLFSDriver.seek_cb = eSeek;
    stTLVGLFSDriver.tell_cb = eTell;

    stTLVGLFSDriver.write_cb = NULL;
    stTLVGLFSDriver.dir_open_cb = NULL;
    stTLVGLFSDriver.dir_close_cb = NULL;
    stTLVGLFSDriver.dir_read_cb = NULL;

    lv_fs_drv_register(&stTLVGLFSDriver);
    bIsDriverRegistered = true;
    lvgl_unlock();

    return true;
}

static bool bBuild_ZephyrFilePath(const char *pcaLVGLPath, char *pcaZephyrPath, size_t uiZephyrPathSize)
{
    if(pcaLVGLPath == NULL || pcaZephyrPath == NULL)
    {
        FHALT("LVGL: Null Pointer Reference");
        return false;
    }
    if(uiZephyrPathSize == 0U)
    {
        FHALT("LVGL: Invalid Zephyr Path Size : %zu", uiZephyrPathSize);
        return false;        
    }
    if(pcaLVGLPath[0] == '\0')
    {
        FHALT("LVGL: Invalid LVGL Path : %s", pcaLVGLPath);
        return false;         
    }

    const char *pcaSeparator = (pcaLVGLPath[0] == '/')? "":"/";

    int iPathLen = snprintf(pcaZephyrPath, uiZephyrPathSize, "%s%s%s",
                            EXT_LITTLEFS_MOUNT_POINT, pcaSeparator, pcaLVGLPath);
    
    if(iPathLen < 0 || ((size_t)iPathLen >= uiZephyrPathSize))
    {
        return false;
    }

    return true;
}

static lv_fs_res_t eMap_ZephyrError(int iError)
{
    switch(iError)
    {
        case -ENOENT:
            return LV_FS_RES_NOT_EX;

        case -ENOMEM:
            return LV_FS_RES_OUT_OF_MEM;

        case -EACCES:
        case -EPERM:
        case -EROFS:
            return LV_FS_RES_DENIED;

        case -EBUSY:
            return LV_FS_RES_BUSY;

        case -ETIMEDOUT:
            return LV_FS_RES_TOUT;

        case -ENOSPC:
            return LV_FS_RES_FULL;

        case -EINVAL:
            return LV_FS_RES_INV_PARAM;

        case -ENOTSUP:
            return LV_FS_RES_NOT_IMP;

        default:
            return LV_FS_RES_UNKNOWN;        
    }
}

#pragma region Callback Functions

static void *pvOpen(lv_fs_drv_t *pstDriver, const char *pcaPath, lv_fs_mode_t eMode)
{
    (void)pstDriver;//Tell the compiler that 'pstDriver' is not used in the code

    if(pcaPath == NULL)
    {
        return NULL;
    }
    if(eMode != LV_FS_MODE_RD)
    {
        return NULL;
    }

    sT_LVGL_ZephyrFile_t *pstFile = (sT_LVGL_ZephyrFile_t *)lv_malloc_zeroed(sizeof(sT_LVGL_ZephyrFile_t));
    if(pstFile == NULL)
    {
        return NULL;
    }

    char caZephyrFilePath[MAX_FILE_PATH_LENGTH] = {'\0'};
    if(!bBuild_ZephyrFilePath(pcaPath, caZephyrFilePath, sizeof(caZephyrFilePath)))
    {
        lv_free(pstFile);
        return NULL;
    }

    fs_file_t_init(&pstFile->stFile);

    int iResult = fs_open(&pstFile->stFile, caZephyrFilePath, FS_O_READ);
    if(iResult != 0)
    {
        lv_free(pstFile);
        return NULL;        
    }

    return pstFile;
}

static lv_fs_res_t eClose( lv_fs_drv_t *pstDriver, void *pvFile)
{
    (void)pstDriver;//Tell the compiler that 'pstDriver' is not used in the code

    if(pvFile == NULL)
    {
        return LV_FS_RES_INV_PARAM;
    }

    sT_LVGL_ZephyrFile_t *pstFile = (sT_LVGL_ZephyrFile_t *)pvFile;

    int iResult = fs_close(&pstFile->stFile);
    lv_free(pstFile);

    if(iResult < 0)
    {
        return eMap_ZephyrError(iResult);
    }
    return LV_FS_RES_OK;
}

static lv_fs_res_t eRead(lv_fs_drv_t *pstDriver, void *pvFile, void *pvBuffer, uint32_t uiBytesToRead, uint32_t *puiBytesRead)
{
    (void)pstDriver;//Tell the compiler that 'pstDriver' is not used in the code

    if(pvFile == NULL || pvBuffer == NULL || puiBytesRead == NULL)
    {
        return LV_FS_RES_INV_PARAM;
    }

    *puiBytesRead = 0U;
    if(uiBytesToRead == 0U)
    {
        return LV_FS_RES_OK;
    }

    sT_LVGL_ZephyrFile_t *pstFile = (sT_LVGL_ZephyrFile_t *)pvFile;

    ssize_t iResult = fs_read(&pstFile->stFile, pvBuffer, uiBytesToRead);
    if(iResult < 0)
    {
        return eMap_ZephyrError((int)iResult);
    }

    *puiBytesRead = (uint32_t)iResult;
    return LV_FS_RES_OK;
}

static lv_fs_res_t eSeek(lv_fs_drv_t *pstDriver, void *pvFile, uint32_t uiPosition, lv_fs_whence_t eWhence)
{
    (void)pstDriver;

    if(pvFile == NULL)
    {
        return LV_FS_RES_INV_PARAM;
    }

    int iZephyrWhence;

    switch(eWhence)
    {
        case LV_FS_SEEK_SET:
            iZephyrWhence = FS_SEEK_SET;
            break;

        case LV_FS_SEEK_CUR:
            iZephyrWhence = FS_SEEK_CUR;
            break;

        case LV_FS_SEEK_END:
            iZephyrWhence = FS_SEEK_END;
            break;

        default:
            return LV_FS_RES_INV_PARAM;
    }
    
    sT_LVGL_ZephyrFile_t *pstFile = (sT_LVGL_ZephyrFile_t *)pvFile;

    int iResult = fs_seek(&pstFile->stFile, (off_t)uiPosition, iZephyrWhence);
    if(iResult < 0)
    {
        return eMap_ZephyrError(iResult);
    }

    return LV_FS_RES_OK;
}

static lv_fs_res_t eTell(lv_fs_drv_t *pstDriver, void *pvFile, uint32_t *puiPosition)
{
    (void)pstDriver;

    if(pvFile == NULL || puiPosition == NULL)
    {
        return LV_FS_RES_INV_PARAM;
    }

    sT_LVGL_ZephyrFile_t *pstFile = (sT_LVGL_ZephyrFile_t *)pvFile;

    off_t iPosition = fs_tell(&pstFile->stFile);
    if(iPosition < 0)
    {
        return eMap_ZephyrError((int)iPosition);
    }

    if((uint64_t)iPosition > UINT32_MAX)
    {
        return LV_FS_RES_UNKNOWN;
    }

    *puiPosition = (uint32_t)iPosition;
    return LV_FS_RES_OK;
}

#pragma endregion Callback Functions