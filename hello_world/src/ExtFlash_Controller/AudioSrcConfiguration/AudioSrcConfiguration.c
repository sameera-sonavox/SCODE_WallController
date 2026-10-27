#include "AudioSrcConfiguration.h"
#include "GenericMacro.h"
#include "../ExtFlash_DeviceTreeEntries.h"
#include "../LittleFsController/LittleFs_Controller.h"
#include "../../LVGL_Controller/Screens/SelectedAudioSource/SelectedAudioSrc_UIParam.h"

#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/crc.h>
#include <zephyr/sys/util.h>
#include <zephyr/toolchain.h>

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#ifdef DEBUG_AUDIOSRC_CONFIG_FILE_ACCESS
    #define Debug_AudioSrcConf_Printf           printf
#else
    #define Debug_AudioSrcConf_Printf(...)
#endif

#define AUDIO_SOURCE_CONFIG_MAGIC               "SRCF"
#define AUDIO_SOURCE_CONFIG_VERSION             2U
#define AUDIO_SOURCE_CONFIG_HEADER_SIZE         24U
#define AUDIO_SOURCE_CONFIG_CRC_OFFSET          16U

#define AUDIO_SOURCE_CONFIG_FLAG_ACTIVE         (1U << 0)
#define AUDIO_SOURCE_CONFIG_FLAG_MUTED          (1U << 1)
#define AUDIO_SOURCE_CONFIG_FLAG_SELECTED       (1U << 2)

#define AUDIO_SOURCE_CONFIG_KNOWN_FLAGS         (AUDIO_SOURCE_CONFIG_FLAG_ACTIVE | \
                                                 AUDIO_SOURCE_CONFIG_FLAG_MUTED | \
                                                 AUDIO_SOURCE_CONFIG_FLAG_SELECTED)
#define AUDIO_SOURCE_CONFIG_FILE_FORMAT         EXT_LITTLEFS_CONFIG_DIRECTORY "source_%02u.bin"         

typedef struct __packed
{
    uint8_t au8Magic[4];

    uint8_t au8VersionLE[2];
    uint8_t au8HeaderSizeLE[2];
    uint8_t au8TotalSizeLE[4];
    uint8_t au8PayloadCRC32LE[4];

    uint8_t uiSourceType;
    uint8_t uiSourceId;
    uint8_t uiFlags;
    uint8_t uiVolume;
    uint8_t uiSourceNameLength;

    uint8_t au8Reserved[3];
    char caSourceName[AUDIO_SOURCE_CONFIG_NAME_SIZE];
} sT_AudioSourceConfigRecord_t;


BUILD_ASSERT(sizeof(sT_AudioSourceConfigRecord_t) == AUDIO_SOURCE_CONFIG_RECORD_SIZE, "Invalid Source Configuration Record Size");
BUILD_ASSERT(offsetof(sT_AudioSourceConfigRecord_t, uiSourceType) == AUDIO_SOURCE_CONFIG_CRC_OFFSET, "Invalid Source Configuration CRC Offset");
BUILD_ASSERT(offsetof(sT_AudioSourceConfigRecord_t, caSourceName) == AUDIO_SOURCE_CONFIG_HEADER_SIZE, "Invalid Source Configuration Header Size");

static sT_AudioSourceConfig_t staAudioSrcConfigs[eNUMBER_OF_AUDIO_SOURCES];

/* Independent field/file updates use only the corresponding source lock. */
static struct k_mutex staAudioSrcMutexes[eNUMBER_OF_AUDIO_SOURCES];

/* Only selection transitions use this lock to enforce one selected source. */
K_MUTEX_DEFINE(kMutex_AudioSrcSelection);

static _Atomic bool bConfigurationsLoaded = false;
static bool bAudioSrcMutexesInitialized = false;
static bool bDecode_AudioSourceConfiguration(const sT_AudioSourceConfigRecord_t *pstRecord,
                                             eAudioSrc_Id_t eExpectedSourceId,
                                             sT_AudioSourceConfig_t *pstConfiguration);

static bool bValidate_SourceName(const sT_AudioSourceConfigRecord_t *pstRecord);

static bool bValidate_AllSourceConfigurations(const sT_AudioSourceConfig_t *pstaConfigurations);
static bool bEncode_AudioSrcConfiguration(const sT_AudioSourceConfig_t *pstAudioSrcConfig,
                                          sT_AudioSourceConfigRecord_t *pstRecord);
static bool bInit_AudioSrcMutexes( void );
static eAudioSrc_Id_t eFind_SelectedAudioSrc( void );
static int iLock_AudioSources(eAudioSrc_Id_t eFirstSrcId,
                              eAudioSrc_Id_t eSecondSrcId);
static void vUnlock_AudioSources(eAudioSrc_Id_t eFirstSrcId,
                                 eAudioSrc_Id_t eSecondSrcId);
static void vCopy_AudioSrcConfiguration(sT_AudioSourceConfig_t *pstDestination,
                                        const sT_AudioSourceConfig_t *pstSource);
static int iUpdate_AudioSrcFile(const sT_AudioSourceConfig_t *pstAudioSrcConfig);
static int iLock_AudioSourceAccess(eAudioSrc_Id_t eSrcId);
static int iUnLock_AudioSourceAccess(eAudioSrc_Id_t eSrcId);

int iRead_AudioSrcConfigurationRecord(eAudioSrc_Id_t eSrcId,
                                      uint8_t *puiData,
                                      size_t uiLength)
{
    if(eSrcId >= eNUMBER_OF_AUDIO_SOURCES ||
       puiData == NULL ||
       uiLength != AUDIO_SOURCE_CONFIG_RECORD_SIZE)
    {
        return -EINVAL;
    }

    char caFilePath[MAX_FILE_PATH_LENGTH] = {0};
    const int iPathLength = snprintf(caFilePath,
                                     sizeof(caFilePath),
                                     AUDIO_SOURCE_CONFIG_FILE_FORMAT,
                                     (uint8_t)eSrcId);
    if(iPathLength < 0 || (size_t)iPathLength >= sizeof(caFilePath))
    {
        return -EINVAL;
    }

    return iRead_FromFileBin(caFilePath, puiData, uiLength);
}

int iGet_SelectedAudioSrcConfig( sT_AudioSourceConfig_t *pstConfigSnapshot )
{
    if(pstConfigSnapshot == NULL)
    {
        FHALT("Null Pointer Reference");
        return -EINVAL;
    }

    if(k_mutex_lock(&kMutex_AudioSrcSelection,
                    K_MSEC(MUTEX_ACCESS_WAIT_TIME_ms)) != 0)
    {
        FHALT("Mutex cannot be acquired");
        return -EBUSY;
    }

    const eAudioSrc_Id_t eSelectedSrcId = eFind_SelectedAudioSrc();
    if(eSelectedSrcId >= eNUMBER_OF_AUDIO_SOURCES)
    {
        k_mutex_unlock(&kMutex_AudioSrcSelection);
        return -ENODATA;
    }

    if(iLock_AudioSources(eSelectedSrcId, eSelectedSrcId) != 0)
    {
        FHALT("Audio Source[%d] cannot be locked", eSelectedSrcId);
        k_mutex_unlock(&kMutex_AudioSrcSelection);
        return -EBUSY;
    }

    vCopy_AudioSrcConfiguration(
        pstConfigSnapshot,
        &staAudioSrcConfigs[eSelectedSrcId]);

    vUnlock_AudioSources(eSelectedSrcId, eSelectedSrcId);
    k_mutex_unlock(&kMutex_AudioSrcSelection);
    return 0;
}

bool bUpdate_AudioSrcMuteStatus(eAudioSrc_Id_t eSrcId, bool bIsMute)
{
    if(eSrcId >= eNUMBER_OF_AUDIO_SOURCES)
    {
        FHALT("Invalid Audio Source: %d", eSrcId);
        return false;
    }

    if(!bAudioSrcMutexesInitialized)
    {
        return false;
    }

    int iResult = iLock_AudioSourceAccess(eSrcId);
    if(iResult != 0)
    {
        return false;
    }

    const bool bPreviousMuteState = atomic_load_explicit(&staAudioSrcConfigs[eSrcId].bIsMute,
                                                         memory_order_acquire);
    if(bPreviousMuteState == bIsMute)
    {
        return (iUnLock_AudioSourceAccess(eSrcId) == 0);
    }

    atomic_store_explicit(&staAudioSrcConfigs[eSrcId].bIsMute,
                          bIsMute,
                          memory_order_release);

    iResult = iUpdate_AudioSrcFile(&staAudioSrcConfigs[eSrcId]);

    if(iResult != 0)
    {
        atomic_store_explicit(&staAudioSrcConfigs[eSrcId].bIsMute,
                              bPreviousMuteState,
                              memory_order_release);
    }

    const int iUnlockResult = iUnLock_AudioSourceAccess(eSrcId);

    return ((iResult == 0) && (iUnlockResult == 0));
}

static int iLock_AudioSourceAccess(eAudioSrc_Id_t eSrcId)
{
    if(eSrcId >= eNUMBER_OF_AUDIO_SOURCES)
    {
        FHALT("Invalid Audio Source: %d", eSrcId);
        return -EINVAL;
    }
    
    int iresult = k_mutex_lock(&staAudioSrcMutexes[eSrcId], K_MSEC(MUTEX_ACCESS_WAIT_TIME_ms));
    return iresult;
}
static int iUnLock_AudioSourceAccess(eAudioSrc_Id_t eSrcId)
{
    if(eSrcId >= eNUMBER_OF_AUDIO_SOURCES)
    {
        FHALT("Invalid Audio Source: %d", eSrcId);
        return -EINVAL;
    }
    
    return k_mutex_unlock(&staAudioSrcMutexes[eSrcId]);
}

void vGetSnapshot_Of_AudioSrcName(eAudioSrc_Id_t eSrcId, char *pcaSrcName)
{
    if(eSrcId >= eNUMBER_OF_AUDIO_SOURCES)
    {
        FHALT("Invalid Audio Src Id: %d", eSrcId);
        return;
    }
    if(pcaSrcName == NULL)
    {
        FHALT("Null Pointer Reference for Char Array");
        return;
    }

    if(k_mutex_lock(&kMutex_AudioSrcSelection,
                    K_MSEC(MUTEX_ACCESS_WAIT_TIME_ms)) != 0)
    {
        FHALT("Mutex cannot be acquired");
        return;
    }

    switch(eSrcId)
    {
        case eAudio_Src_0:
            memcpy(pcaSrcName, staAudioSrcConfigs[eAudio_Src_0].caSrcName, sizeof(staAudioSrcConfigs[eAudio_Src_0].caSrcName));
            break;
        case eAudio_Src_1:
            memcpy(pcaSrcName, staAudioSrcConfigs[eAudio_Src_1].caSrcName, sizeof(staAudioSrcConfigs[eAudio_Src_1].caSrcName));
            break;
        case eAudio_Src_2:
            memcpy(pcaSrcName, staAudioSrcConfigs[eAudio_Src_2].caSrcName, sizeof(staAudioSrcConfigs[eAudio_Src_2].caSrcName));
            break;
        case eAudio_Src_3:
            memcpy(pcaSrcName, staAudioSrcConfigs[eAudio_Src_3].caSrcName, sizeof(staAudioSrcConfigs[eAudio_Src_3].caSrcName));
            break;
        default:
            FHALT("Audio Src[%d] is not implemented", eSrcId);
            break;;        
    }

    k_mutex_unlock(&kMutex_AudioSrcSelection);
}

int iUpdate_AudioScrSelected(eAudioSrc_Id_t eSrcId, bool bIsSelected)
{
    if(eSrcId >= eNUMBER_OF_AUDIO_SOURCES)
    {
        FHALT("Invalid Audio Source: %d", eSrcId);
        return -EINVAL;
    }
    if(!bAudioSrcMutexesInitialized)
    {
        return -EACCES;
    }

    int iresult = k_mutex_lock(&kMutex_AudioSrcSelection,
                               K_MSEC(MUTEX_ACCESS_WAIT_TIME_ms));
    if(iresult != 0)
    {
        return -EBUSY;
    }

    const eAudioSrc_Id_t ePreviousSrcId = eFind_SelectedAudioSrc();

    if(!bIsSelected)
    {
        iresult = iLock_AudioSources(eSrcId, eSrcId);
        if(iresult != 0)
        {
            (void)k_mutex_unlock(&kMutex_AudioSrcSelection);
            return iresult;
        }

        if(!atomic_load_explicit(&staAudioSrcConfigs[eSrcId].bIsSelected,
                                 memory_order_acquire))
        {
            vUnlock_AudioSources(eSrcId, eSrcId);
            (void)k_mutex_unlock(&kMutex_AudioSrcSelection);
            return 0;
        }

        atomic_store_explicit(&staAudioSrcConfigs[eSrcId].bIsSelected,
                              false,
                              memory_order_release);
        iresult = iUpdate_AudioSrcFile(&staAudioSrcConfigs[eSrcId]);
        if(iresult != 0)
        {
            atomic_store_explicit(&staAudioSrcConfigs[eSrcId].bIsSelected,
                                  true,
                                  memory_order_release);
        }

        vUnlock_AudioSources(eSrcId, eSrcId);
        (void)k_mutex_unlock(&kMutex_AudioSrcSelection);
        return iresult;
    }

    if(ePreviousSrcId == eSrcId)
    {
        (void)k_mutex_unlock(&kMutex_AudioSrcSelection);
        return 0;
    }

    iresult = iLock_AudioSources(ePreviousSrcId, eSrcId);
    if(iresult != 0)
    {
        (void)k_mutex_unlock(&kMutex_AudioSrcSelection);
        return iresult;
    }

    if(!atomic_load_explicit(&staAudioSrcConfigs[eSrcId].bIsActive,
                             memory_order_acquire))
    {
        vUnlock_AudioSources(ePreviousSrcId, eSrcId);
        (void)k_mutex_unlock(&kMutex_AudioSrcSelection);
        return -EACCES;
    }

    /*
     * Persist the old source as unselected first.  If power is lost between
     * the two writes, startup can observe zero selected sources, never two.
     */
    if(ePreviousSrcId < eNUMBER_OF_AUDIO_SOURCES)
    {
        atomic_store_explicit(
            &staAudioSrcConfigs[ePreviousSrcId].bIsSelected,
            false,
            memory_order_release);

        iresult = iUpdate_AudioSrcFile(
            &staAudioSrcConfigs[ePreviousSrcId]);
        if(iresult != 0)
        {
            atomic_store_explicit(
                &staAudioSrcConfigs[ePreviousSrcId].bIsSelected,
                true,
                memory_order_release);
            vUnlock_AudioSources(ePreviousSrcId, eSrcId);
            (void)k_mutex_unlock(&kMutex_AudioSrcSelection);
            return iresult;
        }
    }

    atomic_store_explicit(&staAudioSrcConfigs[eSrcId].bIsSelected,
                          true,
                          memory_order_release);
    iresult = iUpdate_AudioSrcFile(&staAudioSrcConfigs[eSrcId]);
    if(iresult != 0)
    {
        /* The old file is already unselected, so keep RAM consistent. */
        atomic_store_explicit(&staAudioSrcConfigs[eSrcId].bIsSelected,
                              false,
                              memory_order_release);
    }

    vUnlock_AudioSources(ePreviousSrcId, eSrcId);
    (void)k_mutex_unlock(&kMutex_AudioSrcSelection);
    return iresult;
}

static eAudioSrc_Id_t eFind_SelectedAudioSrc( void )
{
    for(uint8_t uiIndex = eAudio_Src_0;
        uiIndex < eNUMBER_OF_AUDIO_SOURCES;
        uiIndex++)
    {
        if(atomic_load_explicit(&staAudioSrcConfigs[uiIndex].bIsSelected,
                                memory_order_acquire))
        {
            return (eAudioSrc_Id_t)uiIndex;
        }
    }

    return eNUMBER_OF_AUDIO_SOURCES;
}

eAudioSrc_Id_t eGet_BluetoothSource( void )
{
    for(uint8_t i = eAudio_Src_0; i < eNUMBER_OF_AUDIO_SOURCES; i++)
    {
        if(staAudioSrcConfigs[i].eSrcType == eAudioSrc_Type_Bluetooth)
        {
            return (eAudioSrc_Id_t)i;
        }
    }

    return eNUMBER_OF_AUDIO_SOURCES;
}

eBLEState_t eGet_BleConnectionState( void )
{
    eAudioSrc_Id_t eId = eGet_BluetoothSource();
    if(eId == eNUMBER_OF_AUDIO_SOURCES)
    {
        return eNUMBER_OF_BLE_STATEs;
    }

    eBLEState_t eState = atomic_load_explicit(&staAudioSrcConfigs[eId].eBleState, memory_order_acquire);
    return eState;
}

bool bSet_BleConnectionState(eBLEState_t eState)
{
    if(eState <= eBleState_None || eState >= eNUMBER_OF_BLE_STATEs)
    {
        return false;
    }

    const eAudioSrc_Id_t eId = eGet_BluetoothSource();
    if(eId >= eNUMBER_OF_AUDIO_SOURCES)
    {
        return false;
    }

    atomic_store_explicit(&staAudioSrcConfigs[eId].eBleState,
                          eState,
                          memory_order_release);
    return true;
}

static bool bInit_AudioSrcMutexes( void )
{
    if(bAudioSrcMutexesInitialized)
    {
        return true;
    }

    for(uint8_t uiIndex = eAudio_Src_0;
        uiIndex < eNUMBER_OF_AUDIO_SOURCES;
        uiIndex++)
    {
        if(k_mutex_init(&staAudioSrcMutexes[uiIndex]) != 0)
        {
            return false;
        }
    }

    bAudioSrcMutexesInitialized = true;
    return true;
}

static int iLock_AudioSources(eAudioSrc_Id_t eFirstSrcId,
                              eAudioSrc_Id_t eSecondSrcId)
{
    bool abLocked[eNUMBER_OF_AUDIO_SOURCES] = {false};

    for(uint8_t uiIndex = eAudio_Src_0;
        uiIndex < eNUMBER_OF_AUDIO_SOURCES;
        uiIndex++)
    {
        if((uiIndex != (uint8_t)eFirstSrcId) &&
           (uiIndex != (uint8_t)eSecondSrcId))
        {
            continue;
        }

        const int iResult = k_mutex_lock(
            &staAudioSrcMutexes[uiIndex],
            K_MSEC(MUTEX_ACCESS_WAIT_TIME_ms));
        if(iResult != 0)
        {
            for(uint8_t uiUnlockIndex = eAudio_Src_0;
                uiUnlockIndex < uiIndex;
                uiUnlockIndex++)
            {
                if(abLocked[uiUnlockIndex])
                {
                    (void)k_mutex_unlock(
                        &staAudioSrcMutexes[uiUnlockIndex]);
                }
            }
            return -EBUSY;
        }

        abLocked[uiIndex] = true;
    }

    return 0;
}

static void vUnlock_AudioSources(eAudioSrc_Id_t eFirstSrcId,
                                 eAudioSrc_Id_t eSecondSrcId)
{
    for(uint8_t uiIndex = eNUMBER_OF_AUDIO_SOURCES;
        uiIndex > eAudio_Src_0;
        uiIndex--)
    {
        const uint8_t uiSrcId = uiIndex - 1U;
        if((uiSrcId == (uint8_t)eFirstSrcId) ||
           (uiSrcId == (uint8_t)eSecondSrcId))
        {
            (void)k_mutex_unlock(&staAudioSrcMutexes[uiSrcId]);
        }
    }
}

static void vCopy_AudioSrcConfiguration(
    sT_AudioSourceConfig_t *pstDestination,
    const sT_AudioSourceConfig_t *pstSource)
{
    pstDestination->eSrcId = pstSource->eSrcId;
    pstDestination->eSrcType = pstSource->eSrcType;
    memcpy(pstDestination->caSrcName,
           pstSource->caSrcName,
           sizeof(pstDestination->caSrcName));

    atomic_store_explicit(
        &pstDestination->bIsActive,
        atomic_load_explicit(&pstSource->bIsActive, memory_order_acquire),
        memory_order_release);
    atomic_store_explicit(
        &pstDestination->bIsMute,
        atomic_load_explicit(&pstSource->bIsMute, memory_order_acquire),
        memory_order_release);
    atomic_store_explicit(
        &pstDestination->bIsSelected,
        atomic_load_explicit(&pstSource->bIsSelected, memory_order_acquire),
        memory_order_release);
    atomic_store_explicit(
        &pstDestination->uiVolume,
        atomic_load_explicit(&pstSource->uiVolume, memory_order_acquire),
        memory_order_release);
}

static int iUpdate_AudioSrcFile(const sT_AudioSourceConfig_t *pstAudioSrcConfig)
{
    if(pstAudioSrcConfig == NULL)
    {
        FHALT("Null Pointer Reference");
        return -EINVAL;
    }

    sT_AudioSourceConfigRecord_t stTAudioSrcRecord = {0};
    if(!bEncode_AudioSrcConfiguration(pstAudioSrcConfig, &stTAudioSrcRecord))
    {
        return -EBADE;
    }

    char caFilePath[MAX_FILE_PATH_LENGTH] = {0};
    const int iPathLength = snprintf(caFilePath,
                                     sizeof(caFilePath),
                                     AUDIO_SOURCE_CONFIG_FILE_FORMAT,
                                     (uint8_t)pstAudioSrcConfig->eSrcId);

    if(iPathLength < 0 || (size_t)iPathLength >= sizeof(caFilePath))
    {
        return -EBADF;
    }

    int iresult = iWrite_ToFileBin(caFilePath, (uint8_t *)&stTAudioSrcRecord, 
                                   sizeof(stTAudioSrcRecord), sizeof(stTAudioSrcRecord));
    return iresult;    
}

static bool bEncode_AudioSrcConfiguration(const sT_AudioSourceConfig_t *pstAudioSrcConfig,
                                          sT_AudioSourceConfigRecord_t *pstRecord)
{
    if(pstAudioSrcConfig == NULL || pstRecord == NULL)
    {
        FHALT("Null Pointer Reference");
        return false;
    }

    const uint16_t uiNameLength = strnlen(pstAudioSrcConfig->caSrcName, AUDIO_SOURCE_CONFIG_NAME_SIZE);
    if(uiNameLength == 0U || uiNameLength >= AUDIO_SOURCE_CONFIG_NAME_SIZE)
    {
        FHALT("Invalid Source Name Length @Length: %d", uiNameLength);
        return false;
    }

    const uint16_t uiVolume = atomic_load_explicit(&pstAudioSrcConfig->uiVolume, memory_order_acquire);
    if(uiVolume > SELECTED_AUDIO_SRC_MAX_VOLUME)
    {
        FHALT("Invalid Source Volume : %d", uiVolume);
        return false;        
    }

    memset(pstRecord, 0, sizeof(*pstRecord));
    memcpy(pstRecord->au8Magic, AUDIO_SOURCE_CONFIG_MAGIC, sizeof(pstRecord->au8Magic));

    sys_put_le16(AUDIO_SOURCE_CONFIG_VERSION, pstRecord->au8VersionLE);
    sys_put_le16(AUDIO_SOURCE_CONFIG_HEADER_SIZE, pstRecord->au8HeaderSizeLE);
    sys_put_le32(AUDIO_SOURCE_CONFIG_RECORD_SIZE, pstRecord->au8TotalSizeLE);
    
    pstRecord->uiSourceType = (uint8_t)pstAudioSrcConfig->eSrcType;
    pstRecord->uiSourceId = (uint8_t)pstAudioSrcConfig->eSrcId;
    pstRecord->uiVolume = (uint8_t)uiVolume;
    pstRecord->uiSourceNameLength = uiNameLength;

    if(atomic_load_explicit(&pstAudioSrcConfig->bIsActive, memory_order_acquire))
    {
        pstRecord->uiFlags |= AUDIO_SOURCE_CONFIG_FLAG_ACTIVE;
    }
    if(atomic_load_explicit(&pstAudioSrcConfig->bIsMute, memory_order_acquire))
    {
        pstRecord->uiFlags |= AUDIO_SOURCE_CONFIG_FLAG_MUTED;
    }
    if(atomic_load_explicit(&pstAudioSrcConfig->bIsSelected, memory_order_acquire))
    {
        pstRecord->uiFlags |= AUDIO_SOURCE_CONFIG_FLAG_SELECTED;
    }
    
    memcpy(pstRecord->caSourceName, pstAudioSrcConfig->caSrcName, uiNameLength);

    const uint8_t *puiCRCData = ((const uint8_t *)pstRecord + AUDIO_SOURCE_CONFIG_CRC_OFFSET);

    const uint32_t uiCRC = crc32_ieee(puiCRCData, AUDIO_SOURCE_CONFIG_RECORD_SIZE - AUDIO_SOURCE_CONFIG_CRC_OFFSET);
    sys_put_le32(uiCRC, pstRecord->au8PayloadCRC32LE);

    return true;
}

int iUpdate_AudioSrcConfigurations(uint8_t *puiData, uint8_t uiLength, eAudioSrc_Id_t eSrcId)
{
    if(puiData == NULL || (uiLength == 0U || uiLength != AUDIO_SOURCE_CONFIG_RECORD_SIZE))
    {
        FHALT("Invalid Parameters");
        return -EINVAL;
    }
    if(eSrcId >= eNUMBER_OF_AUDIO_SOURCES)
    {
        FHALT("Invalid Audio Src Index (%d)", eSrcId);
        return -EINVAL;        
    }
    if(!bAudioSrcMutexesInitialized)
    {
        return -EACCES;
    }

    sT_AudioSourceConfigRecord_t stTAudioSrcCon_Record = {0};
    sT_AudioSourceConfig_t stTemp_AudioSrConfig = {0};
    memcpy((uint8_t *)&stTAudioSrcCon_Record, puiData, uiLength);

    if(!bDecode_AudioSourceConfiguration(&stTAudioSrcCon_Record, eSrcId, &stTemp_AudioSrConfig))
    {
        Debug_AudioSrcConf_Printf("Invalid Source Configurations @SrcId: %d\n\r", eSrcId);
        return -EINVAL;            
    }

    char caFilePath[MAX_FILE_PATH_LENGTH];
    memset(caFilePath, '\0', sizeof(caFilePath));

    const int ipathLength = snprintf(caFilePath, sizeof(caFilePath), 
                                        AUDIO_SOURCE_CONFIG_FILE_FORMAT, (uint8_t)eSrcId);
    if(ipathLength < 0 || (size_t)ipathLength >= sizeof(caFilePath))
    {
        Debug_AudioSrcConf_Printf("Invalid FilePath: %s @SrcId: %d\n\r", caFilePath, eSrcId);
        return -EINVAL;
    }

    int iresult = k_mutex_lock(&kMutex_AudioSrcSelection,
                               K_MSEC(MUTEX_ACCESS_WAIT_TIME_ms));
    if(iresult != 0)
    {
        return -EBUSY;
    }

    iresult = iLock_AudioSources(eSrcId, eSrcId);
    if(iresult != 0)
    {
        (void)k_mutex_unlock(&kMutex_AudioSrcSelection);
        return iresult;
    }

    const bool bIncomingSelected = atomic_load_explicit(&stTemp_AudioSrConfig.bIsSelected, memory_order_acquire);
    const eAudioSrc_Id_t eSelectedSrcId = eFind_SelectedAudioSrc();
    if(bIncomingSelected &&
       eSelectedSrcId < eNUMBER_OF_AUDIO_SOURCES &&
       eSelectedSrcId != eSrcId)
    {
        vUnlock_AudioSources(eSrcId, eSrcId);
        (void)k_mutex_unlock(&kMutex_AudioSrcSelection);
        return -EALREADY;
    }

    iresult = iWrite_ToFileBin(caFilePath,
                               (uint8_t *)&stTAudioSrcCon_Record,
                               uiLength,
                               uiLength);
    if(iresult != 0)
    {
        vUnlock_AudioSources(eSrcId, eSrcId);
        (void)k_mutex_unlock(&kMutex_AudioSrcSelection);
        FHALT("File Update Failed");
        return -EIO;
    }

    vCopy_AudioSrcConfiguration(&staAudioSrcConfigs[eSrcId],
                                &stTemp_AudioSrConfig);
    vUnlock_AudioSources(eSrcId, eSrcId);
    (void)k_mutex_unlock(&kMutex_AudioSrcSelection);
    return 0;
}

int iUpdate_AudioSrcVolume(eAudioSrc_Id_t eSrcId, uint8_t uiVolume)
{
    if(eSrcId >= eNUMBER_OF_AUDIO_SOURCES ||
       uiVolume > SELECTED_AUDIO_SRC_MAX_VOLUME)
    {
        FHALT("Invalid Src Id: %d", eSrcId);
        return -EINVAL;
    }
    if(!bAudioSrcMutexesInitialized)
    {
        return -EACCES;
    }

    int iResult = iLock_AudioSources(eSrcId, eSrcId);
    if(iResult != 0)
    {
        return iResult;
    }

    const uint16_t uiPreviousVolume = atomic_load_explicit(
        &staAudioSrcConfigs[eSrcId].uiVolume,
        memory_order_acquire);
    if(uiPreviousVolume == uiVolume)
    {
        vUnlock_AudioSources(eSrcId, eSrcId);
        return 0;
    }

    atomic_store_explicit(&staAudioSrcConfigs[eSrcId].uiVolume,
                          uiVolume,
                          memory_order_release);
    iResult = iUpdate_AudioSrcFile(&staAudioSrcConfigs[eSrcId]);
    if(iResult != 0)
    {
        atomic_store_explicit(&staAudioSrcConfigs[eSrcId].uiVolume,
                              uiPreviousVolume,
                              memory_order_release);
    }

    vUnlock_AudioSources(eSrcId, eSrcId);
    return iResult;
}

bool bLoad_AudioConfigurations( void )
{
    if(!bInit_AudioSrcMutexes())
    {
        return false;
    }

    sT_AudioSourceConfig_t staTemp_AudioSrConfig[eNUMBER_OF_AUDIO_SOURCES];
    memset(staTemp_AudioSrConfig, 0, sizeof(staTemp_AudioSrConfig));
    atomic_store_explicit(&bConfigurationsLoaded,
                          false,
                          memory_order_release);

    for(uint8_t uiSrcId = eAudio_Src_0; uiSrcId < eNUMBER_OF_AUDIO_SOURCES; uiSrcId++)
    {
        char caFilePath[MAX_FILE_PATH_LENGTH];
        memset(caFilePath, '\0', sizeof(caFilePath));

        const int ipathLength = snprintf(caFilePath, sizeof(caFilePath), 
                                         AUDIO_SOURCE_CONFIG_FILE_FORMAT, uiSrcId);
        if(ipathLength < 0 || (size_t)ipathLength >= sizeof(caFilePath))
        {
            Debug_AudioSrcConf_Printf("Invalid FilePath: %s @SrcId: %d\n\r", caFilePath, uiSrcId);
            return false;
        }

        sT_AudioSourceConfigRecord_t stTAudioSrcCon_Record = {0};

        const int iReadResult = iRead_FromFileBin(caFilePath, (uint8_t *)&stTAudioSrcCon_Record, sizeof(stTAudioSrcCon_Record));
        if(iReadResult != 0)
        {
            Debug_AudioSrcConf_Printf("Failed to read file @Path: %s [Failure: %d]\n\r", caFilePath, iReadResult);
            return false;
        }

        if(!bDecode_AudioSourceConfiguration(&stTAudioSrcCon_Record, uiSrcId, &staTemp_AudioSrConfig[uiSrcId]))
        {
            Debug_AudioSrcConf_Printf("Invalid Source Configurations @SrcId: %d\n\r", uiSrcId);
            return false;            
        }
        
        atomic_store_explicit(&staTemp_AudioSrConfig[uiSrcId].eBleState, eBleState_Disconnected, memory_order_release);
    }

    if(!bValidate_AllSourceConfigurations(staTemp_AudioSrConfig))
    {
        Debug_AudioSrcConf_Printf("Source Configuration is invalid\n\r");
        return false;        
    }

    if(k_mutex_lock(&kMutex_AudioSrcSelection,
                    K_MSEC(MUTEX_ACCESS_WAIT_TIME_ms)) != 0)
    {
        return false;
    }

    uint8_t uiLockedSources = 0U;
    for(uint8_t uiIndex = eAudio_Src_0;
        uiIndex < eNUMBER_OF_AUDIO_SOURCES;
        uiIndex++)
    {
        if(k_mutex_lock(&staAudioSrcMutexes[uiIndex],
                        K_MSEC(MUTEX_ACCESS_WAIT_TIME_ms)) != 0)
        {
            while(uiLockedSources > 0U)
            {
                uiLockedSources--;
                (void)k_mutex_unlock(
                    &staAudioSrcMutexes[uiLockedSources]);
            }
            (void)k_mutex_unlock(&kMutex_AudioSrcSelection);
            return false;
        }
        uiLockedSources++;
    }

    for(uint8_t uiIndex = eAudio_Src_0;
        uiIndex < eNUMBER_OF_AUDIO_SOURCES;
        uiIndex++)
    {
        vCopy_AudioSrcConfiguration(&staAudioSrcConfigs[uiIndex],
                                    &staTemp_AudioSrConfig[uiIndex]);
    }

    while(uiLockedSources > 0U)
    {
        uiLockedSources--;
        (void)k_mutex_unlock(&staAudioSrcMutexes[uiLockedSources]);
    }
    (void)k_mutex_unlock(&kMutex_AudioSrcSelection);

    atomic_store_explicit(&bConfigurationsLoaded,
                          true,
                          memory_order_release);
    return true;
}

static bool bDecode_AudioSourceConfiguration(const sT_AudioSourceConfigRecord_t *pstRecord,
                                             eAudioSrc_Id_t eExpectedSourceId,
                                             sT_AudioSourceConfig_t *pstConfiguration)
{
    if(pstRecord == NULL || pstConfiguration == NULL)
    {
        FHALT("Null Pointer Reference");
        return false;
    }
    if(memcmp(pstRecord->au8Magic, AUDIO_SOURCE_CONFIG_MAGIC, sizeof(pstRecord->au8Magic)) != 0)
    {
        FHALT("Magic String Mismatch(Expected: %s, Received: %s)", AUDIO_SOURCE_CONFIG_MAGIC, (char *)pstRecord->au8Magic);
        return false;        
    }

    const uint16_t uiVersion = sys_get_le16(pstRecord->au8VersionLE);
    const uint16_t uiHeaderSize = sys_get_le16(pstRecord->au8HeaderSizeLE);
    const uint32_t uiTotalSize = sys_get_le32(pstRecord->au8TotalSizeLE);
    const uint32_t uiStoredCRC32 = sys_get_le32(pstRecord->au8PayloadCRC32LE);

    if ((uiVersion != AUDIO_SOURCE_CONFIG_VERSION) ||
        (uiHeaderSize != AUDIO_SOURCE_CONFIG_HEADER_SIZE) ||
        (uiTotalSize != AUDIO_SOURCE_CONFIG_RECORD_SIZE))
    {
        FHALT("Either 'Version', 'HeaderSize' or 'TotalSize' Mismatch (Version: %u[Expected: %u]," 
              "HeaderSize: %u[Expected: %u], TotalSize: %u[Expected: %u])",
              (unsigned int)uiVersion, (unsigned int)AUDIO_SOURCE_CONFIG_VERSION, 
              (unsigned int)uiHeaderSize, (unsigned int)AUDIO_SOURCE_CONFIG_HEADER_SIZE,
              (unsigned int)uiTotalSize, (unsigned int)AUDIO_SOURCE_CONFIG_RECORD_SIZE);
        return false;
    }
    
    const uint8_t *puiCRCData = (const uint8_t *)pstRecord + AUDIO_SOURCE_CONFIG_CRC_OFFSET;//Data for the calculated CRC starts at 16bytes offset

    const uint32_t uiCalculatedCRC32 = crc32_ieee(puiCRCData, AUDIO_SOURCE_CONFIG_RECORD_SIZE - AUDIO_SOURCE_CONFIG_CRC_OFFSET);

    if(uiCalculatedCRC32 != uiStoredCRC32)
    {
        FHALT("CRC Invalid (Calculated: %d, Stored: %d)", uiCalculatedCRC32, uiStoredCRC32);
        return false;
    }

    if(pstRecord->uiSourceId >= (uint8_t)eNUMBER_OF_AUDIO_SOURCES)
    {
        FHALT("Invalid SrcId: %d", pstRecord->uiSourceId);
        return false;        
    }
    if(pstRecord->uiSourceId != (uint8_t)eExpectedSourceId)
    {
        FHALT("SrcId Mismatch( Received: %d, Expected: %d)", pstRecord->uiSourceType, eExpectedSourceId);
        return false;         
    }
    if(pstRecord->uiSourceType >= (uint8_t)eNUMBER_OF_AUDIO_SRC_TYPEs ||
       pstRecord->uiSourceType == eAudioSrc_Type_None)
    {
        FHALT("Invalid SrcType: %d", pstRecord->uiSourceType);
        return false;        
    }
    if(pstRecord->uiVolume > SELECTED_AUDIO_SRC_MAX_VOLUME)
    {
        FHALT("Invalid Src Volume: %d", pstRecord->uiVolume);
        return false;        
    }
    if((pstRecord->uiFlags & (~AUDIO_SOURCE_CONFIG_KNOWN_FLAGS)) != 0)
    {
        FHALT("Unkonown Source Flag Bits: 0x%02X", pstRecord->uiFlags);
        return false;
    }
    if ((pstRecord->au8Reserved[0] != 0U) ||
        (pstRecord->au8Reserved[1] != 0U) ||
        (pstRecord->au8Reserved[2] != 0U))
    {
        FHALT("Source configuration reserved bytes are not zero\n");
        return false;
    }
    if(!bValidate_SourceName(pstRecord))
    {
        FHALT("Invalid Source Name @SrcId: %d\n", (uint8_t)eExpectedSourceId);
        return false;        
    }
    
    const bool bIsActive = ((pstRecord->uiFlags & AUDIO_SOURCE_CONFIG_FLAG_ACTIVE) != 0u);
    const bool bIsMute = ((pstRecord->uiFlags & AUDIO_SOURCE_CONFIG_FLAG_MUTED) != 0u);
    const bool bIsSelected = ((pstRecord->uiFlags & AUDIO_SOURCE_CONFIG_FLAG_SELECTED) != 0u);

    if(!bIsActive && bIsSelected)
    {
        FHALT("Inactive Source cannot be selected @SrcId: %d\n", pstRecord->uiSourceId);
        return false;        
    }

    memset(pstConfiguration, 0, sizeof(*pstConfiguration));

    pstConfiguration->eSrcId = (eAudioSrc_Id_t)pstRecord->uiSourceId;
    pstConfiguration->eSrcType = (eAudioSrc_Type_t)pstRecord->uiSourceType;
    atomic_init(&pstConfiguration->bIsActive, bIsActive);
    atomic_init(&pstConfiguration->bIsMute, bIsMute);
    atomic_init(&pstConfiguration->bIsSelected, bIsSelected);
    atomic_init(&pstConfiguration->uiVolume, pstRecord->uiVolume);
    memcpy(pstConfiguration->caSrcName, pstRecord->caSourceName, pstRecord->uiSourceNameLength);
    pstConfiguration->caSrcName[pstRecord->uiSourceNameLength] = '\0';

    return true;
}

static bool bValidate_SourceName(const sT_AudioSourceConfigRecord_t *pstRecord)
{
    if(pstRecord == NULL)
    {
        FHALT("Null Pointer Reference");
        return false;
    }
    if(pstRecord->uiSourceNameLength == 0U ||
       pstRecord->uiSourceNameLength >= AUDIO_SOURCE_CONFIG_NAME_SIZE)
    {
        FHALT("Source Name Length must be 1..23(Current Length: %d)", pstRecord->uiSourceNameLength);
        return false;
    }

    if(memchr(pstRecord->caSourceName, '\0', pstRecord->uiSourceNameLength) != NULL)
    {
        FHALT("Invalid Source Name: %s", pstRecord->caSourceName);
        return false;
    }

    for(uint8_t i = pstRecord->uiSourceNameLength; i < AUDIO_SOURCE_CONFIG_NAME_SIZE; i++)
    {
        if(pstRecord->caSourceName[i] != '\0')
        {
            FHALT("Source Name does not contain null terminating character at Index: %d", i);
            return false;            
        }
    }

    return true;
}

static bool bValidate_AllSourceConfigurations(const sT_AudioSourceConfig_t *pstaConfigurations)
{
    if(pstaConfigurations == NULL)
    {
        FHALT("Null Pointer Reference");
        return false;
    }

    uint8_t uiSelectedSources = 0U;

    for(uint8_t uiIndex = eAudio_Src_0; uiIndex < eNUMBER_OF_AUDIO_SOURCES; uiIndex++)
    {
        const sT_AudioSourceConfig_t *pstConfig = &pstaConfigurations[uiIndex];
        if(pstConfig->eSrcId != (eAudioSrc_Id_t)uiIndex)
        {
            FHALT("Source Id Mismatch");
            return false;
        }

        if(atomic_load_explicit(&pstConfig->bIsSelected,
                                memory_order_acquire))
        {
            uiSelectedSources++;
            if(!atomic_load_explicit(&pstConfig->bIsActive,
                                     memory_order_acquire))
            {
                return false;
            }
        }
    }

    return (uiSelectedSources <= 1U);
}

bool bGet_AudioSrcConfigurations(eAudioSrc_Id_t eSrcId, sT_AudioSourceConfig_t *pstSnapShot)
{
    if(eSrcId >= eNUMBER_OF_AUDIO_SOURCES || pstSnapShot == NULL)
    {
        return false;
    }

    memcpy(pstSnapShot, &staAudioSrcConfigs[eSrcId], sizeof(staAudioSrcConfigs[eSrcId]));
    return true;
}

bool bIs_AudioSrcConfigLoaded( void )
{
    return atomic_load_explicit(&bConfigurationsLoaded,
                                memory_order_acquire);
}

