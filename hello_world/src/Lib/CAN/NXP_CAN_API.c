
#include <zephyr/logging/log.h>
#include <zephyr/drivers/can.h>

#include <string.h>
#include "NXP_CAN_API.h"
#include "../GenericMacro.h"

#ifdef DEBUG_CAN_DEV_INIT
    #define CAN_INIT_Print          printk
#else
    #define CAN_INIT_Print(...)
#endif
#ifdef DEBUG_CAN_TX
    #define CAN_TX_Print            printk
#else
    #define CAN_TX_Print(...)
#endif  

typedef struct{
    int iFilterId;
    uint32_t uiCanId;
    eT_CAN_RxFilter_ID_t eRxFilterId;
    eT_CAN_RxFilterIDType_t eFilterIdType;
} sT_CAN_RxFilterData_t;

typedef struct{
    const struct device *pstCAN_Device;
    bool bIsInitialized;
    bool bIsCANBusActive;
    enum can_state eCANState;
    struct can_bus_err_cnt stErrCount;
    uint8_t uiNumInstalledRxFilters;
    sT_CAN_RxFilterData_t staRxFilterData[eNUMBER_OF_CAN_RX_CONFIGs];
} sT_CAN_DeviceData_t;

sT_CAN_DeviceData_t stTCANDevData_t;

#define IS_CAN_DEV_Initialized()        (stTCANDevData_t.bIsInitialized == true)
#define IS_CAN_BUS_Active()             (stTCANDevData_t.bIsCANBusActive == true)

bool bIsCAN_DeviceReady( void );
bool bIsCAN_ConfigValidOk( sT_CAN_RXFilterConfig_t *psRxFilterConfigs, uint32_t uiNumRxFilters );
bool bConfig_RxFilters( sT_CAN_RXFilterConfig_t *psRxFilterConfigs, uint32_t uiNumRxFilters, uint8_t *puiCurrentIndex );
bool bIsCAN_RxFilterIdsValid( sT_CAN_RXFilterConfig_t *psRxFilterConfigs, uint32_t uiNumRxFilters );
bool bFlexCAN_AddRxFilter( sT_CAN_RXFilterConfig_t stTRxFilterConfig, uint8_t index, uint8_t *puiCurrentIndex );
void vRollBack_CANConfigs( uint8_t uiIndex );
bool bIsRxFilterAvailable(  eT_CAN_RxFilter_ID_t eRxFilterId  );
const struct device * pstGetCANBussAccessibility( void );

//Callbacks
static void vCAN_State_Change_Callback_Fn(const struct device *dev, enum can_state state, struct can_bus_err_cnt err_cnt,
                         void *user_data);

//Call Back Functions
#pragma region 
static void vCAN_State_Change_Callback_Fn(const struct device *dev,
                         enum can_state state,
                         struct can_bus_err_cnt err_cnt,
                         void *user_data)
{
    stTCANDevData_t.stErrCount = err_cnt;
    stTCANDevData_t.eCANState = state;
    switch(state)
    {
        case CAN_STATE_ERROR_ACTIVE:
            break;
        case CAN_STATE_ERROR_WARNING:
            break;
        case CAN_STATE_ERROR_PASSIVE:
            break;
        case CAN_STATE_BUS_OFF:
            stTCANDevData_t.bIsCANBusActive = false;
            break;
        case CAN_STATE_STOPPED:
            stTCANDevData_t.bIsCANBusActive = false;
            break;
    }
}
#pragma endregion

bool bFlexCAN_SendMsg( sT_CAN_TXMsg_t *pstTxMsg)
{
    const struct device *pstCANDev = pstGetCANBussAccessibility();
    if(pstCANDev == NULL)
        return false;

    
    return true;
}

const struct device * pstGetCANBussAccessibility( void )
{
    if(!IS_CAN_DEV_Initialized())
    {
        FHALT("CAN Interface is not initialized");
        return NULL;
    }
    if(!device_is_ready(stTCANDevData_t.pstCAN_Device))
    {
        FHALT("CAN Device is not ready");
        return NULL;        
    }
    if(!IS_CAN_BUS_Active())
    {
        CAN_TX_Print("CAN Bus is Fault @State: %d\n\r", stTCANDevData_t.eCANState);
        return NULL;
    }

    const struct device *pstCANDev = stTCANDevData_t.pstCAN_Device;
    int ret = can_get_state(pstCANDev, &stTCANDevData_t.eCANState, &stTCANDevData_t.stErrCount);
    if(ret != 0)
    {
        FHALT("Device state not accessible");
        return NULL;
    }

    switch(stTCANDevData_t.eCANState)
    {
        case CAN_STATE_STOPPED:
        case CAN_STATE_BUS_OFF:
            return NULL;
        default:
            break;
    }

    return pstCANDev;
}

int iFlexCAN_Init( sT_CAN_RXFilterConfig_t *psRxFilterConfigs, uint32_t uiNumRxFilters )
{
    int ret = 0;
    uint8_t uiIndex = 0;

    if(!bIsCAN_DeviceReady())
        return -1;
    if(!bIsCAN_ConfigValidOk(psRxFilterConfigs, uiNumRxFilters))
        return -1;

    ret = can_set_mode(stTCANDevData_t.pstCAN_Device, CAN_MODE_FD);
    if(ret < 0)
    {
        FHALT("Failed to set CAN FD Mode.");
        return -1;
    }

    can_set_state_change_callback(stTCANDevData_t.pstCAN_Device, vCAN_State_Change_Callback_Fn, NULL);

    if(!bConfig_RxFilters(psRxFilterConfigs, uiNumRxFilters, &uiIndex))
    {
        vRollBack_CANConfigs(uiIndex);
        return -1;    
    }
    
    ret = can_start(stTCANDevData_t.pstCAN_Device);
    if(ret < 0)
    {
        FHALT("Failed to start CAN Device.");
        vRollBack_CANConfigs(uiIndex);
        return -1;
    }
    stTCANDevData_t.bIsInitialized = true;
    CAN_INIT_Print("CAN Device Initialized\n\r");
    return 0;
}

void vRollBack_CANConfigs( uint8_t uiIndex )
{
    for(uint8_t i = 0; i < uiIndex; i++)
    {
        can_remove_rx_filter(stTCANDevData_t.pstCAN_Device, stTCANDevData_t.staRxFilterData[i].iFilterId);
    }

    memset(stTCANDevData_t.staRxFilterData, 0, sizeof(stTCANDevData_t.staRxFilterData));
    stTCANDevData_t.uiNumInstalledRxFilters = 0;

    can_set_state_change_callback(stTCANDevData_t.pstCAN_Device, NULL, NULL);
    stTCANDevData_t.bIsInitialized = false;
}

bool bConfig_RxFilters( sT_CAN_RXFilterConfig_t *psRxFilterConfigs, uint32_t uiNumRxFilters, uint8_t *puiCurrentIndex )
{
    if(!bIsCAN_RxFilterIdsValid(psRxFilterConfigs, uiNumRxFilters))
        return false;

    for(uint8_t i = 0; i < uiNumRxFilters; i++)
    {
        if(!bIsRxFilterAvailable(psRxFilterConfigs[i].eID))
            return false;
        if(!bFlexCAN_AddRxFilter(psRxFilterConfigs[i], i, puiCurrentIndex))
            return false;
    }

    return true;
}

bool bIsRxFilterAvailable(  eT_CAN_RxFilter_ID_t eRxFilterId  )
{
    for(uint8_t i = 0; i < stTCANDevData_t.uiNumInstalledRxFilters; i++)
    {
        if(stTCANDevData_t.staRxFilterData[i].eRxFilterId == eRxFilterId)
        {
            //Rx Filter ID already in use
            FHALT("Rx Filter ID already in use");
            return false;
        }
    }
    return true;
}

bool bFlexCAN_AddRxFilter( sT_CAN_RXFilterConfig_t stTRxFilterConfig, uint8_t index, uint8_t *puiCurrentIndex )
{
    int ret = 0;
    int iFilterId = iGetCAN_RxFilterID(stTRxFilterConfig.eID);
    if(iFilterId < 0)
    {
        //Invalid Rx Filter ID
        FHALT("Invalid Rx Filter ID");
        return false;
    }

    struct can_filter rxFilter = {
        .id = iFilterId,
        .mask = (stTRxFilterConfig.eFilterIdType == eCAN_RxFilterType_Standard) ? CAN_STD_ID_MASK : CAN_EXT_ID_MASK,
        .flags = (stTRxFilterConfig.eFilterIdType == eCAN_RxFilterType_Standard) ? 0 : CAN_FILTER_IDE
    };

    switch(stTRxFilterConfig.eRxType)
    {
        case eCAN_RxType_Callback:
            if(stTRxFilterConfig.rxCallback == NULL)
            {
                //Invalid Rx Callback
                FHALT("Invalid Rx Callback");
                return false;
            }
            ret = can_add_rx_filter(stTCANDevData_t.pstCAN_Device,
                                    stTRxFilterConfig.rxCallback,
                                    stTRxFilterConfig.pUserData, 
                                    &rxFilter);
            break;
        case eCAN_RxType_MsgQueue:
            if(stTRxFilterConfig.pstMsgQueue == NULL)
            {
                //Invalid Rx Message Queue
                FHALT("Invalid Rx Message Queue");
                return false;
            }
            ret = can_add_rx_filter_msgq(stTCANDevData_t.pstCAN_Device, 
                                    stTRxFilterConfig.pstMsgQueue, &rxFilter);
            break;
        default:
            //Invalid Rx Type
            FHALT("Invalid Rx Type");
            return false;
    }

    if(ret  < 0)
    {
        //Failed to add Rx Filter
        FHALT("Failed to add Rx Filter");
        return false;
    }

    stTCANDevData_t.staRxFilterData[index].iFilterId = ret;
    stTCANDevData_t.staRxFilterData[index].uiCanId = rxFilter.id;
    stTCANDevData_t.staRxFilterData[index].eRxFilterId = stTRxFilterConfig.eID;
    stTCANDevData_t.staRxFilterData[index].eFilterIdType = stTRxFilterConfig.eFilterIdType;
    (*puiCurrentIndex)++;
    stTCANDevData_t.uiNumInstalledRxFilters = *puiCurrentIndex;
    return true;

}

bool bIsCAN_RxFilterIdsValid( sT_CAN_RXFilterConfig_t *psRxFilterConfigs, uint32_t uiNumRxFilters )
{
    uint8_t uiCount_Std = 0, uiCount_Ext = 0;

    uint8_t uiMaxFilters_Std = can_get_max_filters(stTCANDevData_t.pstCAN_Device, false);
    uint8_t uiMaxFilters_Ext = can_get_max_filters(stTCANDevData_t.pstCAN_Device, true);

    for(uint8_t i = 0; i < uiNumRxFilters; i++)
    {               
        if(psRxFilterConfigs[i].eFilterIdType == eCAN_RxFilterType_Standard)
        {
            uiCount_Std++;
            if(uiCount_Std > uiMaxFilters_Std)
            {
                //Exceeded Max Number of Standard Filters
                FHALT("Exceeded Max Number of Standard Filters");
                return false;
            }
        }
        else if(psRxFilterConfigs[i].eFilterIdType == eCAN_RxFilterType_Extended)
        {
            uiCount_Ext++;
            if(uiCount_Ext > uiMaxFilters_Ext)
            {
                //Exceeded Max Number of Extended Filters
                FHALT("Exceeded Max Number of Extended Filters");
                return false;
            }
        }
    }

    return true;    
}


bool bIsCAN_ConfigValidOk( sT_CAN_RXFilterConfig_t *psRxFilterConfigs, uint32_t uiNumRxFilters )
{
    if(psRxFilterConfigs == NULL)
    {
        //Invalid Rx Filter Config Pointer
        FHALT("Invalid Rx Filter Config Pointer");
        return false;
    }
    if(uiNumRxFilters == 0 || uiNumRxFilters != eNUMBER_OF_CAN_RX_CONFIGs)
    {
        //Invalid Number of Rx Filters
        FHALT("Invalid Number of Rx Filters");
        return false;
    }

    for(int i = 0; i< uiNumRxFilters; i++)
    {
        if(psRxFilterConfigs[i].eID >= eNUMBER_OF_CAN_RX_CONFIGs || psRxFilterConfigs[i].eID < 0)
        {
            //Invalid eT_CAN_RxType_t
            FHALT("Invalid FiltId: %d", psRxFilterConfigs[i].eID);
            return false;
        }
        if(psRxFilterConfigs[i].eFilterIdType >= eNUMBER_OF_FILT_ID_TYPEs || psRxFilterConfigs[i].eFilterIdType < 0)
        {
            //Invalid Rx Filter ID Type
            FHALT("Invalid Rx Filter ID Type for FiltId: %d", psRxFilterConfigs[i].eID);
            return false;
        }
        if(psRxFilterConfigs[i].eRxType >= eNUMBER_OF_CAN_RX_TYPES || psRxFilterConfigs[i].eRxType < 0)
        {
            //Invalid eT_CAN_RxType_t
            FHALT("Invalid Receive Type Defined for FiltId: %d", psRxFilterConfigs[i].eID);
            return false;
        }
    }

    return true;
}

bool bIsCAN_DeviceReady( void )
{
    stTCANDevData_t.pstCAN_Device = pstGetCAN_Device();
    if(stTCANDevData_t.pstCAN_Device == NULL)
    {
        //Failed to get CAN Device
        FHALT("Failed to get CAN Device");
        return false;
    }

    if(!device_is_ready(stTCANDevData_t.pstCAN_Device))
    {
        //CAN Device not ready
        FHALT("CAN Device not ready");
        return false;
    }
    return true;
}
