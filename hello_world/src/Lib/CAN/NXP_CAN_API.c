
#include <zephyr/logging/log.h>
#include <zephyr/drivers/can.h>
#include <zephyr/kernel.h>

#include <string.h>
#include "NXP_CAN_API.h"
#include "../GenericMacro.h"

#ifdef DEBUG_CAN_DEV_INIT
    #define CAN_INIT_Print              printk
#else
    #define CAN_INIT_Print(...)
#endif
#ifdef DEBUG_CAN_TX
    #define CAN_TX_Print                printk
#else
    #define CAN_TX_Print(...)
#endif
#ifdef DEBUG_CAN_MANUAL_RECOVER
    #define CAN_FLT_MRECV_Print         printk
#else
    #define CAN_FLT_MRECV_Print(...)
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
    eT_CANBus_Recovery_State_t eBusRecoveryState;
    uint8_t uiBusRecoveryRetryCount;
    eT_CANBus_Recovery_Type_t eBusRecoveryType;
    eT_CAN_BUSState eCANState;
    struct can_bus_err_cnt stErrCount;
    uint8_t uiNumInstalledRxFilters;
    sT_CAN_RxFilterData_t staRxFilterData[eNUMBER_OF_CAN_RX_CONFIGs];
    can_criticalbus_state_callback_t pvBusErrorCallBack_t;
    can_busfault_latch_callback_t pvBusErrRecoveryFailCallback_t;
} sT_CAN_DeviceData_t;

typedef struct {
    uint32_t uiID;
    uint8_t uiLen;
    uint8_t uiaData[CAN_MAX_DLEN];
} sT_CAN_TxQueueItem_t;

sT_CAN_DeviceData_t stTCANDevData_t;
static struct k_spinlock stTCANDevDataLock;

#define IS_CAN_DEV_Initialized()        (bIsCANStackInitialized() == true)
#define IS_CAN_BUS_Active()             (bFlexCAN_IsBusActive() == true)

//Message Q Definitions
K_MSGQ_DEFINE(msgq_CANTxMsg, sizeof(sT_CAN_TxQueueItem_t), CAN_TX_QUEUE_DEPTH, 4);
static struct k_work stCAN_TxWork;

bool bIsCAN_DeviceReady( void );
bool bIsCAN_ConfigValidOk( sT_CANConfig_t *pstCANConfig, uint32_t uiNumRxFilters );
bool bConfig_RxFilters( sT_CAN_RXFilterConfig_t *psRxFilterConfigs, uint32_t uiNumRxFilters, uint8_t *puiCurrentIndex );
bool bIsCAN_RxFilterIdsValid( sT_CAN_RXFilterConfig_t *psRxFilterConfigs, uint32_t uiNumRxFilters );
bool bFlexCAN_AddRxFilter( sT_CAN_RXFilterConfig_t stTRxFilterConfig, uint8_t index, uint8_t *puiCurrentIndex );
void vRollBack_CANConfigs( uint8_t uiIndex );
bool bIsRxFilterAvailable(  eT_CAN_RxFilter_ID_t eRxFilterId  );
const struct device * pstGetCANBussAccessibility( void );
void vManage_SubscriberNotifications( void );
static void vCAN_FaultRecoveryHandler( struct k_work *work );
static inline void vTrigger_BusFaultCallback( void );
void vHandle_CANBusOff_Recovery( void );
void vHandle_CANBusStopped_Recovery( void );
static bool bFlexCAN_IsBusActive( void );
static void vSetCAN_BusStatus(bool bIsActive, eT_CAN_BUSState eCANState);
static void vSetCAN_RecoveryStatus(eT_CANBus_Recovery_State_t eRecoveryState, uint8_t uiRetryCount);
static uint8_t uiAddCAN_RecoveryRetryCount(uint8_t uiRetryCountToAdd);
static uint8_t uiGetCAN_RecoveryRetryCount( void );
static bool bIsCAN_RecoveryLatched( void );
static void vGetCAN_BusFaultSnapshot(can_criticalbus_state_callback_t *ppvCallback,
                                     eT_CAN_BUSState *peCANState,
                                     struct can_bus_err_cnt *pstErrCount);

static bool bCANTx_Msg_ValidationOk( sT_CAN_TXMsg_t *pstTxMsg, eT_CAN_TxResult *pstTxRes );
static void vCAN_TxMsg_Handler( struct k_work *work );

//Callbacks
static void vCAN_State_Change_Callback_Fn(const struct device *dev, enum can_state state, struct can_bus_err_cnt err_cnt,
                         void *user_data);

//Worker Functionality
struct k_work_delayable stTCAN_FaultRecovery_Work;

bool bIsCANStackInitialized( void )
{
    k_spinlock_key_t key = k_spin_lock(&stTCANDevDataLock);
    bool bIsInitialized = stTCANDevData_t.bIsInitialized;
    k_spin_unlock(&stTCANDevDataLock, key);

    return bIsInitialized;
}

static bool bFlexCAN_IsBusActive( void )
{
    k_spinlock_key_t key = k_spin_lock(&stTCANDevDataLock);
    bool bIsCANBusActive = stTCANDevData_t.bIsCANBusActive;
    k_spin_unlock(&stTCANDevDataLock, key);

    return bIsCANBusActive;
}

static void vSetCAN_BusStatus(bool bIsActive, eT_CAN_BUSState eCANState)
{
    k_spinlock_key_t key = k_spin_lock(&stTCANDevDataLock);
    stTCANDevData_t.bIsCANBusActive = bIsActive;
    stTCANDevData_t.eCANState = eCANState;
    k_spin_unlock(&stTCANDevDataLock, key);
}

static void vSetCAN_RecoveryStatus(eT_CANBus_Recovery_State_t eRecoveryState, uint8_t uiRetryCount)
{
    k_spinlock_key_t key = k_spin_lock(&stTCANDevDataLock);
    stTCANDevData_t.eBusRecoveryState = eRecoveryState;
    stTCANDevData_t.uiBusRecoveryRetryCount = uiRetryCount;
    k_spin_unlock(&stTCANDevDataLock, key);
}

static uint8_t uiAddCAN_RecoveryRetryCount(uint8_t uiRetryCountToAdd)
{
    k_spinlock_key_t key = k_spin_lock(&stTCANDevDataLock);
    stTCANDevData_t.uiBusRecoveryRetryCount += uiRetryCountToAdd;
    uint8_t uiRetryCount = stTCANDevData_t.uiBusRecoveryRetryCount;
    k_spin_unlock(&stTCANDevDataLock, key);

    return uiRetryCount;
}

static uint8_t uiGetCAN_RecoveryRetryCount( void )
{
    k_spinlock_key_t key = k_spin_lock(&stTCANDevDataLock);
    uint8_t uiRetryCount = stTCANDevData_t.uiBusRecoveryRetryCount;
    k_spin_unlock(&stTCANDevDataLock, key);

    return uiRetryCount;
}

static bool bIsCAN_RecoveryLatched( void )
{
    k_spinlock_key_t key = k_spin_lock(&stTCANDevDataLock);
    bool bIsLatched = (stTCANDevData_t.eBusRecoveryState == eCAN_Recovery_LatchedFault);
    k_spin_unlock(&stTCANDevDataLock, key);

    return bIsLatched;
}

static void vGetCAN_BusFaultSnapshot(can_criticalbus_state_callback_t *ppvCallback,
                                     eT_CAN_BUSState *peCANState,
                                     struct can_bus_err_cnt *pstErrCount)
{
    k_spinlock_key_t key = k_spin_lock(&stTCANDevDataLock);
    *ppvCallback = stTCANDevData_t.pvBusErrorCallBack_t;
    *peCANState = stTCANDevData_t.eCANState;
    *pstErrCount = stTCANDevData_t.stErrCount;
    k_spin_unlock(&stTCANDevDataLock, key);
}

//Call Back Functions
#pragma region 
static void vCAN_State_Change_Callback_Fn(const struct device *dev,
                         enum can_state state,
                         struct can_bus_err_cnt err_cnt,
                         void *user_data)
{
    bool bNotifySubscribers = false;

    k_spinlock_key_t key = k_spin_lock(&stTCANDevDataLock);
    stTCANDevData_t.stErrCount = err_cnt;
    switch(state)
    {
        case CAN_STATE_ERROR_ACTIVE:
        case CAN_STATE_ERROR_WARNING:
            stTCANDevData_t.bIsCANBusActive = true;
            stTCANDevData_t.eCANState = eCAN_BUS_Active;
            break;
        case CAN_STATE_ERROR_PASSIVE:
            stTCANDevData_t.bIsCANBusActive = true;
            stTCANDevData_t.eCANState = eCAN_BUS_OnlyForCritical_Traffic;
            bNotifySubscribers = true;
            break;
        case CAN_STATE_BUS_OFF:
        case CAN_STATE_STOPPED:
            stTCANDevData_t.bIsCANBusActive = false;
            stTCANDevData_t.eCANState = eCAN_BUS_Fault;
            bNotifySubscribers = true;
            break;
    }
    k_spin_unlock(&stTCANDevDataLock, key);

    if(bNotifySubscribers)
        vManage_SubscriberNotifications();
}

void vManage_SubscriberNotifications( void )
{
    bool bScheduleRecovery = false;

    k_spinlock_key_t key = k_spin_lock(&stTCANDevDataLock);
    if(stTCANDevData_t.eBusRecoveryType == eCAN_BusRecType_AutoRecover)
    {
        k_spin_unlock(&stTCANDevDataLock, key);
        vTrigger_BusFaultCallback();
        return;
    }

    if(stTCANDevData_t.eBusRecoveryState == eCAN_Recovery_InProgress ||
       stTCANDevData_t.eBusRecoveryState == eCAN_Recovery_LatchedFault)
    {
        k_spin_unlock(&stTCANDevDataLock, key);
        return;
    }

    stTCANDevData_t.eBusRecoveryState = eCAN_Recovery_InProgress;
    stTCANDevData_t.uiBusRecoveryRetryCount = 0;
    bScheduleRecovery = true;
    k_spin_unlock(&stTCANDevDataLock, key);
    CAN_FLT_MRECV_Print("CAN Fault Man.Recv Triggered\n\r");
    vTrigger_BusFaultCallback();
    if(bScheduleRecovery)
        k_work_schedule(&stTCAN_FaultRecovery_Work, K_MSEC(CAN_BUS_RECOVERY_WORKFn_SCHEDULE_TIME_ms));
}

static inline void vTrigger_BusFaultCallback( void )
{
    can_criticalbus_state_callback_t pvBusErrorCallback;
    eT_CAN_BUSState eCANState;
    struct can_bus_err_cnt stErrCount;

    vGetCAN_BusFaultSnapshot(&pvBusErrorCallback, &eCANState, &stErrCount);

    if(pvBusErrorCallback != NULL)
        pvBusErrorCallback(eCANState, stErrCount);
    else
    {
        FHALT("Cannot raise callback event at Bus Error due to Null Pointer");
    }
}
#pragma endregion

//Worker Functions
#pragma region 
static void vCAN_FaultRecoveryHandler( struct k_work *work )
{
    enum can_state estate;
    struct can_bus_err_cnt stTerrcnt;

    if(bIsCAN_RecoveryLatched())
        return;

    int ret = can_get_state(stTCANDevData_t.pstCAN_Device, &estate, &stTerrcnt);
    if(ret != 0)
    {
        FHALT("%s -> State couldn't be retreived.", __func__);
        uint8_t uiRetryCount = uiAddCAN_RecoveryRetryCount(2);
        k_work_schedule(&stTCAN_FaultRecovery_Work, 
                        K_MSEC(CAN_BUS_RECOVERY_WORKFn_SCHEDULE_TIME_ms + (uiRetryCount * CAN_BUS_RECOVERY_BACKOFF_TIME_ms)));
        return;
    }

    switch(estate)
    {
        case CAN_STATE_ERROR_ACTIVE:
        case CAN_STATE_ERROR_WARNING:
        case CAN_STATE_ERROR_PASSIVE:
            vSetCAN_RecoveryStatus(eCAN_Recovery_Idle, 0);
            if(estate == CAN_STATE_ERROR_ACTIVE || estate == CAN_STATE_ERROR_WARNING)
                vSetCAN_BusStatus(true, eCAN_BUS_Active);
            else
                vSetCAN_BusStatus(true, eCAN_BUS_OnlyForCritical_Traffic);
            return;
        case CAN_STATE_BUS_OFF:
        case CAN_STATE_STOPPED:
            vSetCAN_BusStatus(false, eCAN_BUS_Fault);
            break;
        default:
            FHALT("Invalid CAN Bus State");
            return;
    }

    if(estate == CAN_STATE_BUS_OFF)
        vHandle_CANBusOff_Recovery();
    else
        vHandle_CANBusStopped_Recovery();

}

void vHandle_CANBusOff_Recovery( void )
{
    enum can_state estate;
    struct can_bus_err_cnt stTerrcnt;
    uint8_t uiRetryCount;

    if(uiGetCAN_RecoveryRetryCount() >= CAN_BUS_MAX_RECOVERY_TURNs)
    {
        vSetCAN_RecoveryStatus(eCAN_Recovery_LatchedFault, uiGetCAN_RecoveryRetryCount());
        vSetCAN_BusStatus(false, eCAN_BUS_Fault);
        stTCANDevData_t.pvBusErrRecoveryFailCallback_t(eCAN_Recovery_LatchedFault);
        return;
    }

    int ret = can_recover(stTCANDevData_t.pstCAN_Device, K_MSEC(CAN_BUS_RECOVERY_TIME_ms));

    switch(ret)
    {
        case 0:
            ret = can_get_state(stTCANDevData_t.pstCAN_Device, &estate, &stTerrcnt);
            if(ret == 0 && estate != CAN_STATE_BUS_OFF && estate != CAN_STATE_STOPPED)
            {
                vSetCAN_RecoveryStatus(eCAN_Recovery_Idle, 0);
                vSetCAN_BusStatus(true, eCAN_BUS_Active);
                break;
            }
            
            uiRetryCount = uiAddCAN_RecoveryRetryCount(1);
            k_work_schedule(&stTCAN_FaultRecovery_Work, 
                    K_MSEC(CAN_BUS_RECOVERY_WORKFn_SCHEDULE_TIME_ms + (uiRetryCount * CAN_BUS_RECOVERY_BACKOFF_TIME_ms)));
            break;
        case -EAGAIN:
            uiRetryCount = uiAddCAN_RecoveryRetryCount(1);
            k_work_schedule(&stTCAN_FaultRecovery_Work, 
                    K_MSEC(CAN_BUS_RECOVERY_WORKFn_SCHEDULE_TIME_ms + (uiRetryCount * CAN_BUS_RECOVERY_BACKOFF_TIME_ms)));
            break;
        case -ENETDOWN:
            vHandle_CANBusStopped_Recovery();
            break;
        case -ENOTSUP:
        case -ENOSYS:
            vSetCAN_RecoveryStatus(eCAN_Recovery_LatchedFault, uiGetCAN_RecoveryRetryCount());
            vSetCAN_BusStatus(false, eCAN_BUS_Fault);
            stTCANDevData_t.pvBusErrRecoveryFailCallback_t(eCAN_Recovery_LatchedFault);            
            break;
        case -EIO:
        default:            
            uiRetryCount = uiAddCAN_RecoveryRetryCount(2);
            k_work_schedule(&stTCAN_FaultRecovery_Work, 
                            K_MSEC(CAN_BUS_RECOVERY_WORKFn_SCHEDULE_TIME_ms + (uiRetryCount * CAN_BUS_RECOVERY_BACKOFF_TIME_ms)));
            break;
    }
}

void vHandle_CANBusStopped_Recovery( void )
{
    int ret = can_start(stTCANDevData_t.pstCAN_Device);
    if(ret == 0 || ret == -EALREADY)
    {
        vSetCAN_RecoveryStatus(eCAN_Recovery_Idle, 0);
        vSetCAN_BusStatus(true, eCAN_BUS_Active);
    }
    else
    {
        vSetCAN_RecoveryStatus(eCAN_Recovery_LatchedFault, uiGetCAN_RecoveryRetryCount());
        vSetCAN_BusStatus(false, eCAN_BUS_Fault);
        stTCANDevData_t.pvBusErrRecoveryFailCallback_t(eCAN_Recovery_LatchedFault);
    }
}
#pragma endregion

// CAN Message Transmit
#pragma region 
eT_CAN_TxResult eFlexCAN_SendMsg( sT_CAN_TXMsg_t *pstTxMsg )
{
    eT_CAN_TxResult eTxResult;
    sT_CAN_TxQueueItem_t stTMsg;

    const struct device *pstCANDev = pstGetCANBussAccessibility();
    if(pstCANDev == NULL)
        return eCAN_TxResult_BusUnavailable;

    if(!bCANTx_Msg_ValidationOk(pstTxMsg, &eTxResult))
        return eTxResult;

    stTMsg.uiID = pstTxMsg->uiID;
    stTMsg.uiLen = pstTxMsg->uiLen;
    if(pstTxMsg->uiLen > 0)
        memcpy(stTMsg.uiaData, pstTxMsg->puiData, pstTxMsg->uiLen);
    
    int ret = k_msgq_put(&msgq_CANTxMsg, &stTMsg, K_NO_WAIT);
    if(ret != 0)
        return eCAN_TxResult_QueueFull;
    
    k_work_submit(&stCAN_TxWork);
    return eCAN_TxResult_Ok;
}

static void vCAN_TxMsg_Handler( struct k_work *work )
{
    sT_CAN_TxQueueItem_t stTMsg;

    while(k_msgq_get(&msgq_CANTxMsg, &stTMsg, K_NO_WAIT) == 0)
    {
        const struct device *pstCANDev = pstGetCANBussAccessibility();
        if(pstCANDev == NULL)
        {
            FHALT("CAN Tx Message Queue Error : Bus Unavailable");
            break;
        }

        struct can_frame stFrame = {0};
        if(stTMsg.uiID <= CAN_STD_ID_MASK)
        {
            stFrame.id = stTMsg.uiID;
        }
        else if(stTMsg.uiID <= CAN_EXT_ID_MASK)
        {
            stFrame.id = stTMsg.uiID;
            stFrame.flags |= CAN_FRAME_IDE;
        }
        else{
            FHALT("Msg ID Error for Id: %d", stTMsg.uiID);
            continue;
        }

        stFrame.dlc = can_bytes_to_dlc(stTMsg.uiLen);
        if(stTMsg.uiLen > 8)
            stFrame.flags |= CAN_FRAME_FDF;
        memcpy(stFrame.data, stTMsg.uiaData, stTMsg.uiLen);

        int ret = can_send(pstCANDev, &stFrame, K_MSEC(CAN_TX_TIMEOUT_MS), NULL, NULL);
        if(ret != 0)
        {
            FHALT("CAN Tx Fail @Err.Code : %d", ret);
        }
        CAN_TX_Print("Msg Sent to Id: %d\n\r", stTMsg.uiID);
    }
}

static bool bCANTx_Msg_ValidationOk( sT_CAN_TXMsg_t *pstTxMsg, eT_CAN_TxResult *pstTxRes )
{
    *pstTxRes = eCAN_TxResult_InvalidMsg;

    if(pstTxMsg == NULL)
    {
        FHALT("Null pointer for Tx Message");
        return false;
    }
    if(pstTxMsg->uiLen > CAN_MAX_DLEN)
    {
        FHALT("Payload length is larger than Max(%d) bytes", CAN_MAX_DLEN);
        return false;
    }
    if(pstTxMsg->uiLen > 0 && pstTxMsg->puiData == NULL)
    {
        FHALT("Payload pointer is NULL");
        return false;        
    }

    *pstTxRes = eCAN_TxResult_Ok;
    return true;
}
#pragma endregion

const struct device * pstGetCANBussAccessibility( void )
{
    eT_CAN_BUSState eCANState;
    eT_CANBus_Recovery_State_t eBusRecoveryState;

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
        k_spinlock_key_t key = k_spin_lock(&stTCANDevDataLock);
        eCANState = stTCANDevData_t.eCANState;
        k_spin_unlock(&stTCANDevDataLock, key);
        CAN_TX_Print("CAN Bus is Fault @State: %d\n\r", eCANState);
        return NULL;
    }
    k_spinlock_key_t key = k_spin_lock(&stTCANDevDataLock);
    eCANState = stTCANDevData_t.eCANState;
    eBusRecoveryState = stTCANDevData_t.eBusRecoveryState;
    k_spin_unlock(&stTCANDevDataLock, key);

    if(eBusRecoveryState == eCAN_Recovery_LatchedFault)
    {
        CAN_TX_Print("CAN Bus is Fault @State: %d & RecoveryState: %d\n\r", eCANState, eBusRecoveryState);
        return NULL;
    }

    const struct device *pstCANDev = stTCANDevData_t.pstCAN_Device;
    enum can_state ecanState;
    struct can_bus_err_cnt stErrCount;
    int ret = can_get_state(pstCANDev, &ecanState, &stErrCount);
    if(ret != 0)
    {
        FHALT("Device state not accessible");
        return NULL;
    }

    key = k_spin_lock(&stTCANDevDataLock);
    stTCANDevData_t.stErrCount = stErrCount;
    k_spin_unlock(&stTCANDevDataLock, key);

    switch(ecanState)
    {
        case CAN_STATE_ERROR_ACTIVE:
        case CAN_STATE_ERROR_WARNING:
            vSetCAN_BusStatus(true, eCAN_BUS_Active);
            break;
        case CAN_STATE_ERROR_PASSIVE:
            vSetCAN_BusStatus(true, eCAN_BUS_OnlyForCritical_Traffic);
            break;
        case CAN_STATE_BUS_OFF:
        case CAN_STATE_STOPPED:
            vSetCAN_BusStatus(false, eCAN_BUS_Fault);
            return NULL;
    }

    return pstCANDev;
}

int iFlexCAN_Init( sT_CANConfig_t *pstCANConfig, uint32_t uiNumRxFilters )
{
    int ret = 0;
    uint8_t uiIndex = 0;
    if(IS_CAN_DEV_Initialized())
    {
        FHALT("CAN Stack is already initialized. Run 'vReset_CANStack' first if you want to reinitialize");
        return -1;
    }

    if(pstCANConfig == NULL)
    {
        FHALT("Invalid Pointer for CAN Configurations");
        return -1;
    }
    if(!bIsCAN_DeviceReady())
    {
        FHALT("CAN Device not ready.");
        return -1;
    }
    if(!bIsCAN_ConfigValidOk(pstCANConfig, uiNumRxFilters))
    {
        return -1;
    }

    k_spinlock_key_t key = k_spin_lock(&stTCANDevDataLock);
    stTCANDevData_t.eCANState = eCAN_BUS_Uninitialized;
    stTCANDevData_t.eBusRecoveryType = pstCANConfig->eBusRecoveryType;
    stTCANDevData_t.pvBusErrorCallBack_t = pstCANConfig->busErrCallback;
    k_spin_unlock(&stTCANDevDataLock, key);

    if(pstCANConfig->eBusRecoveryType == eCAN_BusRecType_AutoRecover)
    {
        key = k_spin_lock(&stTCANDevDataLock);
        stTCANDevData_t.pvBusErrRecoveryFailCallback_t = NULL;
        k_spin_unlock(&stTCANDevDataLock, key);
        ret = can_set_mode(stTCANDevData_t.pstCAN_Device, CAN_MODE_FD);   
    }
    else
    {
        key = k_spin_lock(&stTCANDevDataLock);
        stTCANDevData_t.pvBusErrRecoveryFailCallback_t = pstCANConfig->busFaultLatchCallback;
        k_spin_unlock(&stTCANDevDataLock, key);
        ret = can_set_mode(stTCANDevData_t.pstCAN_Device, CAN_MODE_FD | CAN_MODE_MANUAL_RECOVERY);
    }
    if(ret < 0)
    {
        FHALT("Failed to set CAN FD Mode.");
        return -1;
    }

    if(!bConfig_RxFilters(pstCANConfig->staRxFiltConfigs, uiNumRxFilters, &uiIndex))
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

    vSetCAN_BusStatus(true, eCAN_BUS_Active);
    vSetCAN_RecoveryStatus(eCAN_Recovery_Idle, 0);

    if(stTCANDevData_t.eBusRecoveryType == eCAN_BUSRECTYPE_ManualRecover)
        k_work_init_delayable(&stTCAN_FaultRecovery_Work, vCAN_FaultRecoveryHandler);
    can_set_state_change_callback(stTCANDevData_t.pstCAN_Device, vCAN_State_Change_Callback_Fn, NULL);    

    key = k_spin_lock(&stTCANDevDataLock);
    stTCANDevData_t.bIsInitialized = true;
    k_spin_unlock(&stTCANDevDataLock, key);

    k_work_init(&stCAN_TxWork, vCAN_TxMsg_Handler);
    CAN_INIT_Print("CAN Device Initialized\n\r");
    return 0;
}

void vRollBack_CANConfigs( uint8_t uiIndex )
{
    struct k_work_sync sync;
    bool bCancelRecoveryWork;

    for(uint8_t i = 0; i < uiIndex; i++)
    {
        can_remove_rx_filter(stTCANDevData_t.pstCAN_Device, stTCANDevData_t.staRxFilterData[i].iFilterId);
    }

    k_spinlock_key_t key = k_spin_lock(&stTCANDevDataLock);
    memset(stTCANDevData_t.staRxFilterData, 0, sizeof(stTCANDevData_t.staRxFilterData));
    stTCANDevData_t.uiNumInstalledRxFilters = 0;
    bCancelRecoveryWork = (stTCANDevData_t.eBusRecoveryType == eCAN_BUSRECTYPE_ManualRecover && stTCANDevData_t.bIsInitialized);
    k_spin_unlock(&stTCANDevDataLock, key);

    if(bCancelRecoveryWork)
    {
        k_work_cancel_delayable_sync(&stTCAN_FaultRecovery_Work, &sync);
    }

    can_set_state_change_callback(stTCANDevData_t.pstCAN_Device, NULL, NULL);
    key = k_spin_lock(&stTCANDevDataLock);
    stTCANDevData_t.bIsCANBusActive = false;
    stTCANDevData_t.eCANState = eCAN_BUS_Uninitialized;
    stTCANDevData_t.bIsInitialized = false;
    stTCANDevData_t.eBusRecoveryState = eCAN_Recovery_Idle;
    k_spin_unlock(&stTCANDevDataLock, key);
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
    k_spinlock_key_t key = k_spin_lock(&stTCANDevDataLock);
    for(uint8_t i = 0; i < stTCANDevData_t.uiNumInstalledRxFilters; i++)
    {
        if(stTCANDevData_t.staRxFilterData[i].eRxFilterId == eRxFilterId)
        {
            //Rx Filter ID already in use
            k_spin_unlock(&stTCANDevDataLock, key);
            FHALT("Rx Filter ID already in use");
            return false;
        }
    }
    k_spin_unlock(&stTCANDevDataLock, key);
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

    (*puiCurrentIndex)++;
    k_spinlock_key_t key = k_spin_lock(&stTCANDevDataLock);
    stTCANDevData_t.staRxFilterData[index].iFilterId = ret;
    stTCANDevData_t.staRxFilterData[index].uiCanId = rxFilter.id;
    stTCANDevData_t.staRxFilterData[index].eRxFilterId = stTRxFilterConfig.eID;
    stTCANDevData_t.staRxFilterData[index].eFilterIdType = stTRxFilterConfig.eFilterIdType;
    stTCANDevData_t.uiNumInstalledRxFilters = *puiCurrentIndex;
    k_spin_unlock(&stTCANDevDataLock, key);
    return true;

}

bool bIsCAN_RxFilterIdsValid( sT_CAN_RXFilterConfig_t *psRxFilterConfigs, uint32_t uiNumRxFilters )
{
    uint8_t uiCount_Std = 0, uiCount_Ext = 0;

    int uiMaxFilters_Std = can_get_max_filters(stTCANDevData_t.pstCAN_Device, false);
    int uiMaxFilters_Ext = can_get_max_filters(stTCANDevData_t.pstCAN_Device, true);

    if(uiMaxFilters_Std < 0 || uiMaxFilters_Ext < 0)
    {
        FHALT("Invalid Max Values for Standard and Extended IDs");
        return false;
    }

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

bool bIsCAN_ConfigValidOk(  sT_CANConfig_t *pstCANConfig, uint32_t uiNumRxFilters )
{
    if(pstCANConfig == NULL)
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
        if(pstCANConfig->staRxFiltConfigs[i].eID >= eNUMBER_OF_CAN_RX_CONFIGs || pstCANConfig->staRxFiltConfigs[i].eID < 0)
        {
            //Invalid eT_CAN_RxType_t
            FHALT("Invalid FiltId: %d", pstCANConfig->staRxFiltConfigs[i].eID);
            return false;
        }
        if(pstCANConfig->staRxFiltConfigs[i].eFilterIdType >= eNUMBER_OF_FILT_ID_TYPEs || pstCANConfig->staRxFiltConfigs[i].eFilterIdType < 0)
        {
            //Invalid Rx Filter ID Type
            FHALT("Invalid Rx Filter ID Type for FiltId: %d", pstCANConfig->staRxFiltConfigs[i].eID);
            return false;
        }
        if(pstCANConfig->staRxFiltConfigs[i].eRxType >= eNUMBER_OF_CAN_RX_TYPES || pstCANConfig->staRxFiltConfigs[i].eRxType < 0)
        {
            //Invalid eT_CAN_RxType_t
            FHALT("Invalid Receive Type Defined for FiltId: %d", pstCANConfig->staRxFiltConfigs[i].eID);
            return false;
        }
    }

    if(pstCANConfig->eBusRecoveryType < 0 || pstCANConfig->eBusRecoveryType >= eNUMBER_OF_CANBUS_RECOVERY_TYPEs)
    {
        FHALT("Invalid Bus Recovery Type");
        return false;        
    }
    if(pstCANConfig->busErrCallback == NULL)
    {
        FHALT("Null Pointer for Cricical Bus Notification Callback Fn");
        return false;        
    }
    if(pstCANConfig->eBusRecoveryType == eCAN_BUSRECTYPE_ManualRecover && 
       pstCANConfig->busFaultLatchCallback == NULL)
    {
        FHALT("Callback Fn cannot be NULL for Manual Error Recovery");
        return false; 
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
