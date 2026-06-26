#ifndef NXP_SPI_TYPES_H
#define NXP_SPI_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <stdlib.h>
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

typedef void (*SPI_Callback_t)(eSPI_Slave_Id_t eSlaveId, void *pvUserdata);

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
    const uint8_t *puiTxData;
    size_t uiTxDataLen;
    uint8_t *puiRxData;
    size_t uiRxDataLen;
} sT_SPITransfer_t;

typedef struct
{
    eHW_Match_Type_t eHW_Recv_SyncType;
    uint32_t uiMatch0_Value;
    uint32_t uiMatch1_Value;
} sT_HWMatch_Config_t;

typedef struct
{
    eSPI_PCS_t eCSPin;
    eSPI_DataLane_Width_t eSPI_BusWidth;
    eSPI_CS_Polarity_t eSlaveMode_CS_Ctrl;
    eSPI_ShiftDirection_t eEndianFormat;
    eSPI_CPOL_CPHA_Type_t eCPOLCPH_Ctrl;
    uint16_t uiFrameSize;
    sT_HWMatch_Config_t stTHWMatchConfig;
    SPI_Callback_t pvSPI_CallBack;
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