#ifndef NXP_SPI_TYPES_H
#define NXP_SPI_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <zephyr/drivers/gpio.h>
#include "NXP_SPI_ProjDef.h"

typedef enum
{
    eSPI_0 = 0,
    eSPI_1,
    eNUMBER_OF_SPI_MODULEs
} eSPIModule_t;

typedef enum
{
    eTx_Success = 0,
    eTx_Error,
    eNUMBER_OF_SPI_TX_STATEs
} eSPI_TransferStatus_t;

typedef enum
{
    eSPI_Mode_Peripheral = 0,
    eSPI_Mode_Controller,
    eNUMBER_OF_SPI_MODEs
} eSPI_Mode_t;

typedef enum
{
    eLSB_First = 0,
    eMSB_First,
    eNUMBER_OF_SPIWORD_TXRX_TYPEs
} eSPI_ShiftDirection_t;

typedef enum
{
    ePCS_0,
    ePCS_1,
    ePCS_2,
    ePCS_3,
    eNUMBER_OF_PERIPHERAL_CS_LINEs
} eSPI_PCS_t;

typedef enum
{
    eCPOL_0_CPH_0,
    eCPOL_0_CPH_1,
    eCPOL_1_CPH_0,
    eCPOL_1_CPH_1,
    eNUMBER_OF_CLK_POL_PHASE_COMBINATIONs
} eSPI_CPOL_CPHA_Type_t;

typedef enum
{
    e1bit_Transfer,//1bit -> 1SCK (has MSISO and MOSI)
    e2bit_Transfer,//2bit -> 1SCK (has two separate Data Lines, Data0 & Data1)
    e4bit_Transfer,//4bit -> 1SCK (has four separate Data Lines, Data0,Data1,Data2 & Data3)
    eNUMBER_OF_SPI_DATALANEs
} eSPI_DataLane_Width_t;

typedef enum
{
    eEn_FullDuplex_Transfer_Normal,//SDI : Input, SDO : Output (FullDuplex)
    eEn_HalfDuplex_Transfer_With_SDI,//SDI : for Input and Output (Halfduplex transfer only)
    eEn_HalfDuplex_Transfer_With_SDO,//SDO : for Input and Output (Halfduplex transfer only)
    eEn_FullDuplex_Transfer_Swapped,// SDO : Input, SDI : Output (FullDuplex)
    eNUMBER_OF_PINCONFIGs
} eSPI_PinCfg_For_Transfer_t;

typedef enum
{
    eNotify_Interrupt = 0,
    eNotify_DMA,
    eNUMBER_OF_SPI_NOTIFICATION_TYPEs
} eSPI_NotificationType_t;

typedef enum
{
    eData_Out_TriState = 0,
    eData_Out_RetainLastValue,
    eNUMBER_OF_DATA_OUTSTATEs
} eSPI_DataOut_PinState_t;

typedef enum
{
    eTransfer_Success = 0,
    eTransfer_Failed,
    eTransfer_Timeout,
    eNUMBER_OF_TRANSFER_STATUSs
} eSPI_TransferResult_t;

/**
 * @enum This is used only when the SPI module is in Peripheral Mode.
 */
typedef enum
{
    eCS_Active_Low = 0,
    eCS_Active_High,
    eNUMBER_OF_CS_CONFIGs
} eSPI_CS_Polarity_t;

typedef enum
{
    eHW_Match_Disabled = 0,
    eHW_Match_FirstWord_Equals_Match0_Or_Match1,//MATCH0 = 0xA5 and Byte Stream: A5 12 34 56 : Matches and then SW can deal with the rest of the words
    eHW_Match_AnyWord_Equals_Match0_Or_Match1,//Match any incoming data word with MATCH0 or MATCH1
    eHW_Match_With_Match0_1_Sequentially,//MATCH0 = 0xAA & MATCH1 = 0x55, Byte Stream: AA 55 12 34 (Match Ok) and Byte Stream: 55 AA 12 34 (Match Fail)
    eHW_Match_With_Match0_1_AnyWord,//MATCH0 = 0xAA & MATCH1 = 0x55, Byte Stream: 12 34 AA 55 78 (Match Ok)
    eHW_Match_FirstWord_With_Match0_MaskedWith_Match1,//MATCH0 = 0xA0, MATCH1 = 0xF0, Byte Stream: AA 55 12 34 ->Operation: ((MATCH0 & MATCH1) == (0xAA & MATCH1))? -> Match OK
    eHW_Match_AnyWord_With_Match0_MaskedWith_Match1,//MATCH0 = 0xA0, MATCH1 = 0xF0, Byte Stream: 55 12 AA 34 ->Operation: ((MATCH0 & MATCH1) == (0xAA & MATCH1))? -> Match OK
    eNUMBER_OF_HW_MATCH_CONFIGURATIONs
} eHW_Match_Type_t;

typedef enum
{
    eTransfer_Tx_Only = 0,
    eTransfer_Rx_Only,
    eTransfer_Transceive,
    eNUMBER_OF_TRANSFER_TYPEs
} eTransfer_Type_t;

typedef enum
{
    eTransfer_Use_MessageBus,
    eTransfer_Use_Callback,
    eNUMBER_OF_SLAVE_DATATRANSFER_Types
} eSlaveData_PathConfig_t;

typedef enum
{
    eBuffer_Free = 0,
    eBuffer_Filling,
    eBuffer_Ready,
    eBuffer_Overflow,//3
    eBuffer_Error,
    eBuffer_None,
    eNUMBER_OF_Rx_BUFFER_STATEs
} eRxBuffer_State_t;

typedef enum
{
    eSPI_Overflow_DropNewest = 0,
    eSPI_Overflow_DropOldest,
    eSPI_Overflow_StopAndReport,
    eSPI_Overflow_OverWriteOldest,
    eNUMBER_OF_OVERFLOW_POLICIEs
} eSPI_OverflowPolicy_t;

typedef enum
{
    eSPI_RxTarget_AppBuffer,
    eSPI_RxTarget_DrainBuffer,
    eNUMBER_OF_RXTARGET_BUFFERTYPEs
} eSPI_RxTarget_t;

typedef enum
{
    eSPI_PeripheralEvent_RxReady = 0,
    eSPI_PeripheralEvent_RxOverflow,
    eSPI_PeripheralEvent_RxError,
    eSPI_PeripheralEvent_TxCompleted,//3
    eSPI_PeripheralEvent_TxError,
    eNUMBER_OF_PERIPHERAL_EVENT_TYPEs
} eSPI_PeripheralEvent_Type_t;

typedef enum
{
    eSPI_Rdy_Active_Low = 0,
    eSPI_Rdy_Active_High,
    eNUMBER_OF_SPI_HW_RDY_STATEs
} eSPI_HWRDY_PinState_t;

typedef enum
{
    eSlave_RdyState_Idle = 0,
    eSlave_RdyState_WaitingForReady,
    eSlave_RdyState_Ready,
    eSlave_RdyState_Timeout,
    eNUMBER_OF_SLAVE_STATEs
} eSPI_ExternalSlave_State_t;

typedef enum
{
    eSPI_Buffer_None,
    eSPI_Buffer_Static,
    eSPI_Buffer_Dynamic_And_Free_ByCaller,
    eNUMBER_OF_BUFFER_MANAGEMENT_TYPEs
} eSPIBuffer_ReleaseType_t;

typedef struct
{
    uint8_t *puiRxData;
    uint16_t uiLen;    
} sT_Callback_Data_t;

typedef struct sT_SPIRxBuff_t
{
    uint8_t uiBuffId;
    eRxBuffer_State_t eState;
    uint8_t *puiBuffer;
    size_t uisize;
    struct sT_SPIRxBuff_t *pstNextRxBuff;
} sT_SPIRxBuff_t;

typedef struct
{
    uint8_t uiBuffId;
    eRxBuffer_State_t eState;
    const uint8_t *const puiBuffer;
    size_t uisize;
    bool bTxCompleted;
} sT_RxBuffData_t;

typedef struct
{
    volatile uint32_t uiOverflowCount;
    uint32_t uiDroppedFrameCount;
    eSPI_OverflowPolicy_t eOverflowPolicy;
    _Atomic eSPI_PeripheralEvent_Type_t eEventType;
} sT_SPI_RxOverflowCtrl_t;

typedef struct
{
    bool bHWReady_Used;//Set this if slave use HW Ready Signal
    _Atomic eSPI_ExternalSlave_State_t eSlaveState;//API asynchronously set this state internally. Once configured, the User application
                                                   //must not touch this. User can get the current status of the slave using 'eGetSPI_MasterModeExt_SlaveState'.
    int64_t iReadyWaitStartTime;
    eSPI_HWRDY_PinState_t eHWRdy_PinState;//Defines the active state for the HW ready pin
    const struct gpio_dt_spec *pstGPIOStruct;//Assign this properly, so the API can update the slave status automatically
} sT_HWReadyPin_Ctrl;

typedef void (*SPI_Callback_t)(eSPI_Slave_Id_t eSlaveId, eSPI_TransferResult_t eResult);

/**
 * @brief Callback Fn for the 'eTransfer_Use_Callback' Notification type to the application layer
 * @param eEventType : Current event type. Check this at the application to identify whether receive is in Error or not
 * @param eResult : Transfer result
 * @param stTBuffData : Buffer data. It contains Buffer Id, Current Buffer state (data valid only if it is 'Ready', Pointer to RxBuffer and Size)
 * @param bIsBufferAssignmentSuccess : Defines whether dynamic next buffer assignment at the API is successful or not
 */
typedef void (*SPI_PeripheralCallback_t)(eSPI_PeripheralEvent_Type_t eEventType, 
                                         eSPI_TransferResult_t eResult, 
                                         sT_RxBuffData_t stTBuffData,
                                         bool bIsBufferAssignmentSuccess);

typedef struct
{
    eSPI_Slave_Id_t eSlaveId;
    bool bIs_CS_HWControlled;//If CS is controlled by SW, then it has to be done manually
    bool bEn_SCKLoopBack_ForSampling;//Use this feature when slave needs more setup time at higher SPI frequencies. In HW level, the SCK freq is loopedback and delayed to sample data
    eSPI_ShiftDirection_t eEndianFormat;
    eSPI_PCS_t eHW_PCS_Ctrl;
    eSPI_CPOL_CPHA_Type_t eCPOLCPH_Ctrl;
    eSPI_DataLane_Width_t eSPI_BusWidth;
    eSPI_CS_Polarity_t eCSPolarityType;
    uint32_t uiSPI_Freq_Hz;
    uint32_t uiDelay_Between_BlockTx_ns;
    uint32_t uiDelay_LastSCK_To_CS_Deassert_ns;
    uint32_t uiDelay_CS_Assert_To_SCK_ns;
    sT_HWReadyPin_Ctrl stTHWReadyCtrl;//Use this struct if the slave is providing HW Ready capability
    SPI_Callback_t pvSPI_CallBack; 
} sT_SPISlave_Config_t;

typedef struct sT_SPISlave_Control_t
{
    sT_SPISlave_Config_t stTConfigs;
    struct sT_SPISlave_Control_t *pstNextSlave;
} sT_SPISlave_Control_t;

typedef struct
{
    eSPIModule_t eModuleId;
    eSPI_Slave_Id_t eSlaveId;
    eTransfer_Type_t eType;

    uint8_t *puiTxData;
    size_t uiTxDataLen;    
    eSPIBuffer_ReleaseType_t eTxBufReleaseType;//API does not release any memory. You can use these values for validation at the user application
                                                //and differntiating between static memory allocation vs dynamic memory allocation and free accordingly
    uint8_t *volatile puiRxData;
    size_t uiRxDataLen;
    size_t uiRxMaskLen;
    _Atomic bool bIsTransferBusy;
    eSPIBuffer_ReleaseType_t eRxBufReleaseType;//Same as with 'eTxBufReleaseType'

    bool bShould_CS_Asserted_For_EntireTransfer;//This tell the HW that CS should be asserted until all the specified bytes are transmitted.
                                                //If set to 'false', then CS will be toggled for each frame transfer
} sT_SPIMasterTransfer_t;

typedef struct
{
    eSPIModule_t eModuleId;
    const uint8_t *puiTxData;
    size_t uiLen;
} sT_SPIPreipheralResponse_t;

typedef struct
{
    eHW_Match_Type_t eHW_Recv_SyncType;
    bool bFIFO_StoreOnly_MatchedData;//If 'true', Rx FIFO will store only matched data based on match configurations.
                                     //If 'false', RX FIFO will store all data
                                     //In both cases, the Match Flag is set based on the match configurations
    uint32_t uiMatch0_Value;
    uint32_t uiMatch1_Value;
} sT_HWMatch_Config_t;

typedef struct
{
    size_t uiBuffSize;//Defines number of bytes inside the array
    size_t uiBuffCount;//Defines number of rx buffers required
    SPI_PeripheralCallback_t pvSPI_PeripheralCallBack;
} sT_Callback_Ctrl;

typedef struct
{

} sT_MessageBus_Ctrl;

typedef struct
{
    eSlaveData_PathConfig_t eDataPathType;
    eSPI_OverflowPolicy_t eOverflowPolicy;
    union
    {
        sT_Callback_Ctrl stTCallbackConfig;
        sT_MessageBus_Ctrl stTMessageBusConfig;
    }slave_dataPath;
    
} sT_SPISlave_RxControl_t;

typedef struct
{
    bool bRequest_TxNotifications;//If 'false', the API will notify only Rx and Transceive events and data
    eSPI_PCS_t eCSPin;
    eSPI_DataLane_Width_t eSPI_BusWidth;
    eSPI_CS_Polarity_t eSlaveMode_CS_Ctrl;
    eSPI_ShiftDirection_t eEndianFormat;
    eSPI_CPOL_CPHA_Type_t eCPOLCPH_Ctrl;    
    eSPI_HWRDY_PinState_t eHWRdy_PinState;
    sT_HWMatch_Config_t stTHWMatchConfig;
    sT_SPISlave_RxControl_t stTRxControl;    
    uint16_t uiFrameSize;
} sT_Peripheral_Config_t;

typedef struct
{
    eSPI_Mode_t eMode;
    union
    {
        sT_Peripheral_Config_t stTConfig_Peripheral;
        sT_SPISlave_Control_t *pstSPISlaveHead_Ctrl;
    }spi_mode;

} sT_SPI_Mode_Config_t;

typedef struct
{
    eSPIModule_t eModule;
    bool bIsOk;
    eSPI_NotificationType_t eNotificationType;
    eSPI_DataOut_PinState_t eDataOutPinState;
    eSPI_PinCfg_For_Transfer_t ePinConfig;
    sT_SPI_Mode_Config_t stTSPIModeCtrl;
} sT_SPIConfig_t;

#endif
