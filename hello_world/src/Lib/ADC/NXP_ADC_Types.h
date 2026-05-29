#ifndef NXP_ADC_TYPES_H
#define NXP_ADC_TYPES_H

typedef enum
{
    eADC_Ch_0 = 0,
    eADC_Ch_1,
    eADC_Ch_2,
    eADC_Ch_3,
    eADC_Ch_4,
    eADC_Ch_5,
    eADC_Ch_6,
    eADC_Ch_7,
    eADC_Ch_8,
    eADC_Ch_9,
    eADC_Ch_10,
    eADC_Ch_11,
    eADC_Ch_12,
    eADC_Ch_13,
    eADC_Ch_14,
    eADC_Ch_15,
    eADC_Ch_16,
    eADC_Ch_17,
    eADC_Ch_18,
    eADC_Ch_19,
    eADC_Ch_20,
    eADC_Ch_21,
    eADC_Ch_22,
    eADC_Ch_23,
    eADC_Ch_24,
    eADC_Ch_25,
    eADC_Ch_26,
    eADC_Ch_27,
    eADC_Ch_28,
    eADC_Ch_29,
    eADC_Ch_30,
    eADC_Ch_31,
    eNUMBER_OF_ADC_CHANNELs
} eADC_Channel_t;

typedef enum
{
    eADC_Mod_0 = 0,
    eADC_Mod_1,
    eNUMBER_OF_ADC_MODULEs
} eADC_Module_t;

typedef enum
{
    eADC_CH_Conn_Pin = 0,
    eADC_CH_Conn_Reserved,
    eADC_CH_Conn_OpAmp0_Int,
    eADC_CH_Conn_VSSA,
    eADC_CH_Conn_Temp,
    eADC_CH_Conn_PMCBG,
    eADC_CH_Conn_OpAmp0_BS,
    eADC_CH_Conn_VDD_4,
    eADC_CH_Conn_ATX0,
    eADC_CH_Conn_ATX1,
    eNUMBER_OF_ADC_CONN_ASSIGNMENTs
} eADC_AssignmentType_t;

typedef enum
{
    eADC_VrefSrc_VDD_ANA = 0,    
    eADC_VrefSrc_Ext,      /* DACREF_2 reference source. */
    eADC_VrefSrc_Int,     /* DACREF_1 reference source. */
    eNUMBER_OF_ADC_REF_VOLT_SRCs
} eDAC_RefVoltSrc_t;

#endif
