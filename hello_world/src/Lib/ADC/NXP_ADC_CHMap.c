#include "NXP_ADC_CHMap.h"
#include "NXP_ADC_ProjDef.h"
#include "../GenericMacro.h"

#define ADC_CHANNEL_MAP_ENTRY(adc_channel, is_available, lpadc_channel, assignment_type, max_sample_count) \
    { \
        .stInfo = { \
            .bIsAvailable = (is_available), \
            .bIsChUsed = false, \
            .uiLPADCChannelNumber = (lpadc_channel), \
            .eAssignmentType = (assignment_type), \
        }, \
        .stOwner = { \
            .eTrigSlot = eNUMBER_OF_ADC_TRIG_SLOTs, \
            .eCommandId = eADC_CMD_None, \
            .eChannel = (adc_channel), \
        }, \
        .stValue = { \
            .uiADCVal = 0U, \
        }, \
        .stStats = { \
            .stTMinVal = {UINT16_MAX, 0U, ADC_STATS_DEAFULT_MIN_RELEASE_TIME_ms}, \
            .stTMaxVal = {0U, 0U, ADC_STATS_DEAFULT_MAX_RELEASE_TIME_ms}, \
            .stTAvgVal = {0U, 0U, 0U, (max_sample_count)}, \
            .stTRMSVal = {0U, 0U, 0U, (max_sample_count)}, \
        }, \
    }

sT_ADC_ChannelMap_t staADC_ChannelMap[eNUMBER_OF_ADC_MODULEs][eNUMBER_OF_ADC_CHANNELs] = {
    [eADC_ADC0] = {
        [eADC_Ch_0] = ADC_CHANNEL_MAP_ENTRY(
            eADC_Ch_0,
            true,
            0U,
            eADC_CH_Conn_Pin,
            ADC0_ADC_CH0_AVG_MAX_SAMPLE_COUNT),
        [eADC_Ch_1] = ADC_CHANNEL_MAP_ENTRY(
            eADC_Ch_1,
            true,
            1U,
            eADC_CH_Conn_Pin,
            ADC0_ADC_CH1_AVG_MAX_SAMPLE_COUNT),
        [eADC_Ch_2] = ADC_CHANNEL_MAP_ENTRY(
            eADC_Ch_2,
            true,
            2U,
            eADC_CH_Conn_Pin,
            ADC0_ADC_CH2_AVG_MAX_SAMPLE_COUNT),
        [eADC_Ch_3] = ADC_CHANNEL_MAP_ENTRY(
            eADC_Ch_3,
            true,
            3U,
            eADC_CH_Conn_OpAmp0_Int,
            ADC0_ADC_CH3_AVG_MAX_SAMPLE_COUNT),
        [eADC_Ch_4] = ADC_CHANNEL_MAP_ENTRY(
            eADC_Ch_4,
            true,
            4U,
            eADC_CH_Conn_Pin,
            ADC0_ADC_CH4_AVG_MAX_SAMPLE_COUNT),
        [eADC_Ch_5] = ADC_CHANNEL_MAP_ENTRY(
            eADC_Ch_5,
            true,
            5U,
            eADC_CH_Conn_Pin,
            ADC0_ADC_CH5_AVG_MAX_SAMPLE_COUNT),
        [eADC_Ch_6] = ADC_CHANNEL_MAP_ENTRY(
            eADC_Ch_6,
            true,
            6U,
            eADC_CH_Conn_Pin,
            ADC0_ADC_CH6_AVG_MAX_SAMPLE_COUNT),
        [eADC_Ch_7] = ADC_CHANNEL_MAP_ENTRY(
            eADC_Ch_7,
            true,
            7U,
            eADC_CH_Conn_Pin_VREFI,
            ADC0_ADC_CH7_AVG_MAX_SAMPLE_COUNT),
        [eADC_Ch_8] = ADC_CHANNEL_MAP_ENTRY(
            eADC_Ch_8,
            true,
            8U,
            eADC_CH_Conn_Pin,
            ADC0_ADC_CH8_AVG_MAX_SAMPLE_COUNT),
        [eADC_Ch_9] = ADC_CHANNEL_MAP_ENTRY(
            eADC_Ch_9,
            true,
            9U,
            eADC_CH_Conn_Pin,
            ADC0_ADC_CH9_AVG_MAX_SAMPLE_COUNT),
        [eADC_Ch_10] = ADC_CHANNEL_MAP_ENTRY(
            eADC_Ch_10,
            true,
            10U,
            eADC_CH_Conn_Pin,
            ADC0_ADC_CH10_AVG_MAX_SAMPLE_COUNT),
        [eADC_Ch_11] = ADC_CHANNEL_MAP_ENTRY(
            eADC_Ch_11,
            true,
            11U,
            eADC_CH_Conn_Pin,
            ADC0_ADC_CH11_AVG_MAX_SAMPLE_COUNT),
        [eADC_Ch_12] = ADC_CHANNEL_MAP_ENTRY(
            eADC_Ch_12,
            true,
            12U,
            eADC_CH_Conn_Pin,
            ADC0_ADC_CH12_AVG_MAX_SAMPLE_COUNT),
        [eADC_Ch_13] = ADC_CHANNEL_MAP_ENTRY(
            eADC_Ch_13,
            true,
            13U,
            eADC_CH_Conn_Pin,
            ADC0_ADC_CH13_AVG_MAX_SAMPLE_COUNT),
        [eADC_Ch_14] = ADC_CHANNEL_MAP_ENTRY(
            eADC_Ch_14,
            true,
            14U,
            eADC_CH_Conn_Pin,
            ADC0_ADC_CH14_AVG_MAX_SAMPLE_COUNT),
        [eADC_Ch_15] = ADC_CHANNEL_MAP_ENTRY(
            eADC_Ch_15,
            true,
            15U,
            eADC_CH_Conn_Pin,
            ADC0_ADC_CH15_AVG_MAX_SAMPLE_COUNT),
        [eADC_Ch_16] = ADC_CHANNEL_MAP_ENTRY(
            eADC_Ch_16,
            true,
            16U,
            eADC_CH_Conn_Pin,
            ADC0_ADC_CH16_AVG_MAX_SAMPLE_COUNT),
        [eADC_Ch_17] = ADC_CHANNEL_MAP_ENTRY(
            eADC_Ch_17,
            true,
            17U,
            eADC_CH_Conn_Pin,
            ADC0_ADC_CH17_AVG_MAX_SAMPLE_COUNT),
        [eADC_Ch_18] = ADC_CHANNEL_MAP_ENTRY(
            eADC_Ch_18,
            true,
            18U,
            eADC_CH_Conn_Pin,
            ADC0_ADC_CH18_AVG_MAX_SAMPLE_COUNT),
        [eADC_Ch_19] = ADC_CHANNEL_MAP_ENTRY(
            eADC_Ch_19,
            true,
            19U,
            eADC_CH_Conn_Pin,
            ADC0_ADC_CH19_AVG_MAX_SAMPLE_COUNT),
        // Channels 20-22 are powered from a different domain. Do not scan them with other channels.
        [eADC_Ch_20] = ADC_CHANNEL_MAP_ENTRY(
            eADC_Ch_20,
            true,
            20U,
            eADC_CH_Conn_Pin_VDD_P3,
            ADC0_ADC_CH20_AVG_MAX_SAMPLE_COUNT),
        [eADC_Ch_21] = ADC_CHANNEL_MAP_ENTRY(
            eADC_Ch_21,
            true,
            21U,
            eADC_CH_Conn_Pin_VDD_P3,
            ADC0_ADC_CH21_AVG_MAX_SAMPLE_COUNT),
        [eADC_Ch_22] = ADC_CHANNEL_MAP_ENTRY(
            eADC_Ch_22,
            true,
            22U,
            eADC_CH_Conn_Pin_VDD_P3,
            ADC0_ADC_CH22_AVG_MAX_SAMPLE_COUNT),
        [eADC_Ch_23] = ADC_CHANNEL_MAP_ENTRY(
            eADC_Ch_23,
            true,
            23U,
            eADC_CH_Conn_Pin,
            ADC0_ADC_CH23_AVG_MAX_SAMPLE_COUNT),
        [eADC_Ch_24] = ADC_CHANNEL_MAP_ENTRY(
            eADC_Ch_24,
            true,
            24U,
            eADC_CH_Conn_VSSA,
            ADC0_ADC_CH24_AVG_MAX_SAMPLE_COUNT),
        [eADC_Ch_25] = ADC_CHANNEL_MAP_ENTRY(
            eADC_Ch_25,
            false,
            25U,
            eADC_CH_Conn_Reserved,
            ADC0_ADC_CH25_AVG_MAX_SAMPLE_COUNT),
        [eADC_Ch_26] = ADC_CHANNEL_MAP_ENTRY(
            eADC_Ch_26,
            true,
            26U,
            eADC_CH_Conn_Temp,
            ADC0_ADC_CH26_AVG_MAX_SAMPLE_COUNT),
        [eADC_Ch_27] = ADC_CHANNEL_MAP_ENTRY(
            eADC_Ch_27,
            true,
            27U,
            eADC_CH_Conn_PMCBG,
            ADC0_ADC_CH27_AVG_MAX_SAMPLE_COUNT),
        [eADC_Ch_28] = ADC_CHANNEL_MAP_ENTRY(
            eADC_Ch_28,
            true,
            28U,
            eADC_CH_Conn_OpAmp0_BS,
            ADC0_ADC_CH28_AVG_MAX_SAMPLE_COUNT),
        [eADC_Ch_29] = ADC_CHANNEL_MAP_ENTRY(
            eADC_Ch_29,
            true,
            29U,
            eADC_CH_Conn_VDD_4,
            ADC0_ADC_CH29_AVG_MAX_SAMPLE_COUNT),
        [eADC_Ch_30] = ADC_CHANNEL_MAP_ENTRY(
            eADC_Ch_30,
            true,
            30U,
            eADC_CH_Conn_ATX0,
            ADC0_ADC_CH30_AVG_MAX_SAMPLE_COUNT),
        [eADC_Ch_31] = ADC_CHANNEL_MAP_ENTRY(
            eADC_Ch_31,
            true,
            31U,
            eADC_CH_Conn_ATX1,
            ADC0_ADC_CH31_AVG_MAX_SAMPLE_COUNT),
    },
    [eADC_ADC1] = {
        [eADC_Ch_0] = ADC_CHANNEL_MAP_ENTRY(
            eADC_Ch_0,
            true,
            0U,
            eADC_CH_Conn_Pin,
            ADC1_ADC_CH0_AVG_MAX_SAMPLE_COUNT),
        [eADC_Ch_1] = ADC_CHANNEL_MAP_ENTRY(
            eADC_Ch_1,
            true,
            1U,
            eADC_CH_Conn_Pin,
            ADC1_ADC_CH1_AVG_MAX_SAMPLE_COUNT),
        [eADC_Ch_2] = ADC_CHANNEL_MAP_ENTRY(
            eADC_Ch_2,
            true,
            2U,
            eADC_CH_Conn_Pin,
            ADC1_ADC_CH2_AVG_MAX_SAMPLE_COUNT),
        [eADC_Ch_3] = ADC_CHANNEL_MAP_ENTRY(
            eADC_Ch_3,
            true,
            3U,
            eADC_CH_Conn_Pin,
            ADC1_ADC_CH3_AVG_MAX_SAMPLE_COUNT),
        [eADC_Ch_4] = ADC_CHANNEL_MAP_ENTRY(
            eADC_Ch_4,
            true,
            4U,
            eADC_CH_Conn_Pin,
            ADC1_ADC_CH4_AVG_MAX_SAMPLE_COUNT),
        [eADC_Ch_5] = ADC_CHANNEL_MAP_ENTRY(
            eADC_Ch_5,
            true,
            5U,
            eADC_CH_Conn_Pin,
            ADC1_ADC_CH5_AVG_MAX_SAMPLE_COUNT),
        [eADC_Ch_6] = ADC_CHANNEL_MAP_ENTRY(
            eADC_Ch_6,
            true,
            6U,
            eADC_CH_Conn_Pin,
            ADC1_ADC_CH6_AVG_MAX_SAMPLE_COUNT),
        [eADC_Ch_7] = ADC_CHANNEL_MAP_ENTRY(
            eADC_Ch_7,
            true,
            7U,
            eADC_CH_Conn_Pin,
            ADC1_ADC_CH7_AVG_MAX_SAMPLE_COUNT),
        [eADC_Ch_8] = ADC_CHANNEL_MAP_ENTRY(
            eADC_Ch_8,
            true,
            8U,
            eADC_CH_Conn_Pin,
            ADC1_ADC_CH8_AVG_MAX_SAMPLE_COUNT),
        [eADC_Ch_9] = ADC_CHANNEL_MAP_ENTRY(
            eADC_Ch_9,
            true,
            9U,
            eADC_CH_Conn_Pin,
            ADC1_ADC_CH9_AVG_MAX_SAMPLE_COUNT),
        [eADC_Ch_10] = ADC_CHANNEL_MAP_ENTRY(
            eADC_Ch_10,
            true,
            10U,
            eADC_CH_Conn_Pin,
            ADC1_ADC_CH10_AVG_MAX_SAMPLE_COUNT),
        [eADC_Ch_11] = ADC_CHANNEL_MAP_ENTRY(
            eADC_Ch_11,
            true,
            11U,
            eADC_CH_Conn_Pin,
            ADC1_ADC_CH11_AVG_MAX_SAMPLE_COUNT),
        [eADC_Ch_12] = ADC_CHANNEL_MAP_ENTRY(
            eADC_Ch_12,
            true,
            12U,
            eADC_CH_Conn_Pin,
            ADC1_ADC_CH12_AVG_MAX_SAMPLE_COUNT),
        [eADC_Ch_13] = ADC_CHANNEL_MAP_ENTRY(
            eADC_Ch_13,
            true,
            13U,
            eADC_CH_Conn_Pin,
            ADC1_ADC_CH13_AVG_MAX_SAMPLE_COUNT),
        [eADC_Ch_14] = ADC_CHANNEL_MAP_ENTRY(
            eADC_Ch_14,
            false,
            14U,
            eADC_CH_Conn_Reserved,
            ADC1_ADC_CH14_AVG_MAX_SAMPLE_COUNT),
        [eADC_Ch_15] = ADC_CHANNEL_MAP_ENTRY(
            eADC_Ch_15,
            false,
            15U,
            eADC_CH_Conn_Reserved,
            ADC1_ADC_CH15_AVG_MAX_SAMPLE_COUNT),
        [eADC_Ch_16] = ADC_CHANNEL_MAP_ENTRY(
            eADC_Ch_16,
            false,
            16U,
            eADC_CH_Conn_Reserved,
            ADC1_ADC_CH16_AVG_MAX_SAMPLE_COUNT),
        [eADC_Ch_17] = ADC_CHANNEL_MAP_ENTRY(
            eADC_Ch_17,
            false,
            17U,
            eADC_CH_Conn_Reserved,
            ADC1_ADC_CH17_AVG_MAX_SAMPLE_COUNT),
        [eADC_Ch_18] = ADC_CHANNEL_MAP_ENTRY(
            eADC_Ch_18,
            false,
            18U,
            eADC_CH_Conn_Reserved,
            ADC1_ADC_CH18_AVG_MAX_SAMPLE_COUNT),
        [eADC_Ch_19] = ADC_CHANNEL_MAP_ENTRY(
            eADC_Ch_19,
            false,
            19U,
            eADC_CH_Conn_Reserved,
            ADC1_ADC_CH19_AVG_MAX_SAMPLE_COUNT),
        // Channels 20-22 are powered from a different domain. Do not scan them with other channels.
        [eADC_Ch_20] = ADC_CHANNEL_MAP_ENTRY(
            eADC_Ch_20,
            true,
            20U,
            eADC_CH_Conn_Pin_VDD_P3,
            ADC1_ADC_CH20_AVG_MAX_SAMPLE_COUNT),
        [eADC_Ch_21] = ADC_CHANNEL_MAP_ENTRY(
            eADC_Ch_21,
            true,
            21U,
            eADC_CH_Conn_Pin_VDD_P3,
            ADC1_ADC_CH21_AVG_MAX_SAMPLE_COUNT),
        [eADC_Ch_22] = ADC_CHANNEL_MAP_ENTRY(
            eADC_Ch_22,
            true,
            22U,
            eADC_CH_Conn_Pin_VDD_P3,
            ADC1_ADC_CH22_AVG_MAX_SAMPLE_COUNT),
        [eADC_Ch_23] = ADC_CHANNEL_MAP_ENTRY(
            eADC_Ch_23,
            false,
            23U,
            eADC_CH_Conn_Reserved,
            ADC1_ADC_CH23_AVG_MAX_SAMPLE_COUNT),
        [eADC_Ch_24] = ADC_CHANNEL_MAP_ENTRY(
            eADC_Ch_24,
            true,
            24U,
            eADC_CH_Conn_VSSA,
            ADC1_ADC_CH24_AVG_MAX_SAMPLE_COUNT),
        [eADC_Ch_25] = ADC_CHANNEL_MAP_ENTRY(
            eADC_Ch_25,
            false,
            25U,
            eADC_CH_Conn_Reserved,
            ADC1_ADC_CH25_AVG_MAX_SAMPLE_COUNT),
        [eADC_Ch_26] = ADC_CHANNEL_MAP_ENTRY(
            eADC_Ch_26,
            true,
            26U,
            eADC_CH_Conn_Temp,
            ADC1_ADC_CH26_AVG_MAX_SAMPLE_COUNT),
        [eADC_Ch_27] = ADC_CHANNEL_MAP_ENTRY(
            eADC_Ch_27,
            true,
            27U,
            eADC_CH_Conn_PMCBG,
            ADC1_ADC_CH27_AVG_MAX_SAMPLE_COUNT),
        // ADC1 channel 28 connection still requires confirmation.
        [eADC_Ch_28] = ADC_CHANNEL_MAP_ENTRY(
            eADC_Ch_28,
            false,
            28U,
            eADC_CH_Conn_ADC1_A20_A22_P3,
            ADC1_ADC_CH28_AVG_MAX_SAMPLE_COUNT),
        [eADC_Ch_29] = ADC_CHANNEL_MAP_ENTRY(
            eADC_Ch_29,
            true,
            29U,
            eADC_CH_Conn_VDD_4,
            ADC1_ADC_CH29_AVG_MAX_SAMPLE_COUNT),
        [eADC_Ch_30] = ADC_CHANNEL_MAP_ENTRY(
            eADC_Ch_30,
            true,
            30U,
            eADC_CH_Conn_ATX0,
            ADC1_ADC_CH30_AVG_MAX_SAMPLE_COUNT),
        [eADC_Ch_31] = ADC_CHANNEL_MAP_ENTRY(
            eADC_Ch_31,
            true,
            31U,
            eADC_CH_Conn_ATX2,
            ADC1_ADC_CH31_AVG_MAX_SAMPLE_COUNT),
    },
};

#undef ADC_CHANNEL_MAP_ENTRY

const inputmux_connection_t eaADCInputMuxConnection[eNUMBER_OF_ADC_MODULEs][eNUMBER_OF_ADC_TRIG_SOURCEs] = {
    [eADC_ADC0] = {
        [eADC_TrigSrc_ARM_TXEV] = kINPUTMUX_ArmTxevToAdc0Trigger,
        [eADC_TrigSrc_AOI0_OUT0] = kINPUTMUX_Aoi0Out0ToAdc0Trigger,
        [eADC_TrigSrc_AOI0_OUT1] = kINPUTMUX_Aoi0Out1ToAdc0Trigger,
        [eADC_TrigSrc_AOI0_OUT2] = kINPUTMUX_Aoi0Out2ToAdc0Trigger,
        [eADC_TrigSrc_AOI0_OUT3] = kINPUTMUX_Aoi0Out3ToAdc0Trigger,
        [eADC_TrigSrc_CMP0_OUT] = kINPUTMUX_Cmp0OutToAdc0Trigger,
        [eADC_TrigSrc_CMP1_OUT] = kINPUTMUX_Cmp1OutToAdc0Trigger,
        [eADC_TrigSrc_CTimer0_MAT0] = kINPUTMUX_Ctimer0M0ToAdc0Trigger,
        [eADC_TrigSrc_CTimer0_MAT1] = kINPUTMUX_Ctimer0M1ToAdc0Trigger,
        [eADC_TrigSrc_CTimer1_MAT0] = kINPUTMUX_Ctimer1M0ToAdc0Trigger,
        [eADC_TrigSrc_CTimer1_MAT1] = kINPUTMUX_Ctimer1M1ToAdc0Trigger,
        [eADC_TrigSrc_CTimer2_MAT0] = kINPUTMUX_Ctimer2M0ToAdc0Trigger,
        [eADC_TrigSrc_CTimer2_MAT1] = kINPUTMUX_Ctimer2M1ToAdc0Trigger,
        [eADC_TrigSrc_LPTMR0] = kINPUTMUX_Lptmr0ToAdc0Trigger,
        [eADC_TrigSrc_QDC0_POS_MATCH0] = kINPUTMUX_Qdc0PosMatch0ToAdc0Trigger,
        [eADC_TrigSrc_PWM0_SM0_OUT_TRIG0] = kINPUTMUX_Pwm0Sm0OutTrig0ToAdc0Trigger,
        [eADC_TrigSrc_PWM0_SM0_OUT_TRIG1] = kINPUTMUX_Pwm0Sm0OutTrig1ToAdc0Trigger,
        [eADC_TrigSrc_PWM0_SM1_OUT_TRIG0] = kINPUTMUX_Pwm0Sm1OutTrig0ToAdc0Trigger,
        [eADC_TrigSrc_PWM0_SM1_OUT_TRIG1] = kINPUTMUX_Pwm0Sm1OutTrig1ToAdc0Trigger,
        [eADC_TrigSrc_PWM0_SM2_OUT_TRIG0] = kINPUTMUX_Pwm0Sm2OutTrig0ToAdc0Trigger,
        [eADC_TrigSrc_PWM0_SM2_OUT_TRIG1] = kINPUTMUX_Pwm0Sm2OutTrig1ToAdc0Trigger,
        [eADC_TrigSrc_GPIO0_PIN_EVENT_TRIG0] = kINPUTMUX_Gpio0PinEventTrig0ToAdc0Trigger,
        [eADC_TrigSrc_GPIO1_PIN_EVENT_TRIG0] = kINPUTMUX_Gpio1PinEventTrig0ToAdc0Trigger,
        [eADC_TrigSrc_GPIO2_PIN_EVENT_TRIG0] = kINPUTMUX_Gpio2PinEventTrig0ToAdc0Trigger,
        [eADC_TrigSrc_GPIO3_PIN_EVENT_TRIG0] = kINPUTMUX_Gpio3PinEventTrig0ToAdc0Trigger,
        [eADC_TrigSrc_GPIO4_PIN_EVENT_TRIG0] = kINPUTMUX_Gpio4PinEventTrig0ToAdc0Trigger,
        [eADC_TrigSrc_WUU] = kINPUTMUX_WuuToAdc0Trigger,
        [eADC_TrigSrc_AOI1_OUT0] = kINPUTMUX_Aoi1Out0ToAdc0Trigger,
        [eADC_TrigSrc_AOI1_OUT1] = kINPUTMUX_Aoi1Out1ToAdc0Trigger,
        [eADC_TrigSrc_AOI1_OUT2] = kINPUTMUX_Aoi1Out2ToAdc0Trigger,
        [eADC_TrigSrc_AOI1_OUT3] = kINPUTMUX_Aoi1Out3ToAdc0Trigger,
        [eADC_TrigSrc_OtherADC_TCOMP0] = kINPUTMUX_Adc1Tcomp0ToAdc0Trigger,
        [eADC_TrigSrc_OtherADC_TCOMP1] = kINPUTMUX_Adc1Tcomp1ToAdc0Trigger,
        [eADC_TrigSrc_OtherADC_TCOMP2] = kINPUTMUX_Adc1Tcomp2ToAdc0Trigger,
        [eADC_TrigSrc_OtherADC_TCOMP3] = kINPUTMUX_Adc1Tcomp3ToAdc0Trigger,
        [eADC_TrigSrc_CTimer3_MAT0] = kINPUTMUX_Ctimer3M0ToAdc0Trigger,
        [eADC_TrigSrc_CTimer3_MAT1] = kINPUTMUX_Ctimer3M1ToAdc0Trigger,
        [eADC_TrigSrc_CTimer4_MAT0] = kINPUTMUX_Ctimer4M0ToAdc0Trigger,
        [eADC_TrigSrc_CTimer4_MAT1] = kINPUTMUX_Ctimer4M1ToAdc0Trigger,
        [eADC_TrigSrc_FlexIO_CH0] = kINPUTMUX_FlexioCh0ToAdc0Trigger,
        [eADC_TrigSrc_FlexIO_CH1] = kINPUTMUX_FlexioCh1ToAdc0Trigger,
        [eADC_TrigSrc_FlexIO_CH2] = kINPUTMUX_FlexioCh2ToAdc0Trigger,
        [eADC_TrigSrc_FlexIO_CH3] = kINPUTMUX_FlexioCh3ToAdc0Trigger,
        [eADC_TrigSrc_QDC1_POS_MATCH0] = kINPUTMUX_Qdc1PosMatch0ToAdc0Trigger,
        [eADC_TrigSrc_PWM1_SM0_MUX_TRIG0] = kINPUTMUX_Pwm1Sm0OutTrig0ToAdc0Trigger,
        [eADC_TrigSrc_PWM1_SM0_MUX_TRIG1] = kINPUTMUX_Pwm1Sm0OutTrig1ToAdc0Trigger,
        [eADC_TrigSrc_PWM1_SM1_MUX_TRIG0] = kINPUTMUX_Pwm1Sm1OutTrig0ToAdc0Trigger,
        [eADC_TrigSrc_PWM1_SM1_MUX_TRIG1] = kINPUTMUX_Pwm1Sm1OutTrig1ToAdc0Trigger,
        [eADC_TrigSrc_PWM1_SM2_MUX_TRIG0] = kINPUTMUX_Pwm1Sm2OutTrig0ToAdc0Trigger,
        [eADC_TrigSrc_PWM1_SM2_MUX_TRIG1] = kINPUTMUX_Pwm1Sm2OutTrig1ToAdc0Trigger,
    },
    [eADC_ADC1] = {
        [eADC_TrigSrc_ARM_TXEV] = kINPUTMUX_ArmTxevToAdc1Trigger,
        [eADC_TrigSrc_AOI0_OUT0] = kINPUTMUX_Aoi0Out0ToAdc1Trigger,
        [eADC_TrigSrc_AOI0_OUT1] = kINPUTMUX_Aoi0Out1ToAdc1Trigger,
        [eADC_TrigSrc_AOI0_OUT2] = kINPUTMUX_Aoi0Out2ToAdc1Trigger,
        [eADC_TrigSrc_AOI0_OUT3] = kINPUTMUX_Aoi0Out3ToAdc1Trigger,
        [eADC_TrigSrc_CMP0_OUT] = kINPUTMUX_Cmp0OutToAdc1Trigger,
        [eADC_TrigSrc_CMP1_OUT] = kINPUTMUX_Cmp1OutToAdc1Trigger,
        [eADC_TrigSrc_CTimer0_MAT0] = kINPUTMUX_Ctimer0M0ToAdc1Trigger,
        [eADC_TrigSrc_CTimer0_MAT1] = kINPUTMUX_Ctimer0M1ToAdc1Trigger,
        [eADC_TrigSrc_CTimer1_MAT0] = kINPUTMUX_Ctimer1M0ToAdc1Trigger,
        [eADC_TrigSrc_CTimer1_MAT1] = kINPUTMUX_Ctimer1M1ToAdc1Trigger,
        [eADC_TrigSrc_CTimer2_MAT0] = kINPUTMUX_Ctimer2M0ToAdc1Trigger,
        [eADC_TrigSrc_CTimer2_MAT1] = kINPUTMUX_Ctimer2M1ToAdc1Trigger,
        [eADC_TrigSrc_LPTMR0] = kINPUTMUX_Lptmr0ToAdc1Trigger,
        [eADC_TrigSrc_QDC0_POS_MATCH0] = kINPUTMUX_Qdc0PosMatch0ToAdc1Trigger,
        [eADC_TrigSrc_PWM0_SM0_OUT_TRIG0] = kINPUTMUX_Pwm0Sm0OutTrig0ToAdc1Trigger,
        [eADC_TrigSrc_PWM0_SM0_OUT_TRIG1] = kINPUTMUX_Pwm0Sm0OutTrig1ToAdc1Trigger,
        [eADC_TrigSrc_PWM0_SM1_OUT_TRIG0] = kINPUTMUX_Pwm0Sm1OutTrig0ToAdc1Trigger,
        [eADC_TrigSrc_PWM0_SM1_OUT_TRIG1] = kINPUTMUX_Pwm0Sm1OutTrig1ToAdc1Trigger,
        [eADC_TrigSrc_PWM0_SM2_OUT_TRIG0] = kINPUTMUX_Pwm0Sm2OutTrig0ToAdc1Trigger,
        [eADC_TrigSrc_PWM0_SM2_OUT_TRIG1] = kINPUTMUX_Pwm0Sm2OutTrig1ToAdc1Trigger,
        [eADC_TrigSrc_GPIO0_PIN_EVENT_TRIG0] = kINPUTMUX_Gpio0PinEventTrig0ToAdc1Trigger,
        [eADC_TrigSrc_GPIO1_PIN_EVENT_TRIG0] = kINPUTMUX_Gpio1PinEventTrig0ToAdc1Trigger,
        [eADC_TrigSrc_GPIO2_PIN_EVENT_TRIG0] = kINPUTMUX_Gpio2PinEventTrig0ToAdc1Trigger,
        [eADC_TrigSrc_GPIO3_PIN_EVENT_TRIG0] = kINPUTMUX_Gpio3PinEventTrig0ToAdc1Trigger,
        [eADC_TrigSrc_GPIO4_PIN_EVENT_TRIG0] = kINPUTMUX_Gpio4PinEventTrig0ToAdc1Trigger,
        [eADC_TrigSrc_WUU] = kINPUTMUX_WuuToAdc1Trigger,
        [eADC_TrigSrc_AOI1_OUT0] = kINPUTMUX_Aoi1Out0ToAdc1Trigger,
        [eADC_TrigSrc_AOI1_OUT1] = kINPUTMUX_Aoi1Out1ToAdc1Trigger,
        [eADC_TrigSrc_AOI1_OUT2] = kINPUTMUX_Aoi1Out2ToAdc1Trigger,
        [eADC_TrigSrc_AOI1_OUT3] = kINPUTMUX_Aoi1Out3ToAdc1Trigger,
        [eADC_TrigSrc_OtherADC_TCOMP0] = kINPUTMUX_Adc0Tcomp0ToAdc1Trigger,
        [eADC_TrigSrc_OtherADC_TCOMP1] = kINPUTMUX_Adc0Tcomp1ToAdc1Trigger,
        [eADC_TrigSrc_OtherADC_TCOMP2] = kINPUTMUX_Adc0Tcomp2ToAdc1Trigger,
        [eADC_TrigSrc_OtherADC_TCOMP3] = kINPUTMUX_Adc0Tcomp3ToAdc1Trigger,
        [eADC_TrigSrc_CTimer3_MAT0] = kINPUTMUX_Ctimer3M0ToAdc1Trigger,
        [eADC_TrigSrc_CTimer3_MAT1] = kINPUTMUX_Ctimer3M1ToAdc1Trigger,
        [eADC_TrigSrc_CTimer4_MAT0] = kINPUTMUX_Ctimer4M0ToAdc1Trigger,
        [eADC_TrigSrc_CTimer4_MAT1] = kINPUTMUX_Ctimer4M1ToAdc1Trigger,
        [eADC_TrigSrc_FlexIO_CH0] = kINPUTMUX_FlexioCh0ToAdc1Trigger,
        [eADC_TrigSrc_FlexIO_CH1] = kINPUTMUX_FlexioCh1ToAdc1Trigger,
        [eADC_TrigSrc_FlexIO_CH2] = kINPUTMUX_FlexioCh2ToAdc1Trigger,
        [eADC_TrigSrc_FlexIO_CH3] = kINPUTMUX_FlexioCh3ToAdc1Trigger,
        [eADC_TrigSrc_QDC1_POS_MATCH0] = kINPUTMUX_Qdc1PosMatch0ToAdc1Trigger,
        [eADC_TrigSrc_PWM1_SM0_MUX_TRIG0] = kINPUTMUX_Pwm1Sm0OutTrig0ToAdc1Trigger,
        [eADC_TrigSrc_PWM1_SM0_MUX_TRIG1] = kINPUTMUX_Pwm1Sm0OutTrig1ToAdc1Trigger,
        [eADC_TrigSrc_PWM1_SM1_MUX_TRIG0] = kINPUTMUX_Pwm1Sm1OutTrig0ToAdc1Trigger,
        [eADC_TrigSrc_PWM1_SM1_MUX_TRIG1] = kINPUTMUX_Pwm1Sm1OutTrig1ToAdc1Trigger,
        [eADC_TrigSrc_PWM1_SM2_MUX_TRIG0] = kINPUTMUX_Pwm1Sm2OutTrig0ToAdc1Trigger,
        [eADC_TrigSrc_PWM1_SM2_MUX_TRIG1] = kINPUTMUX_Pwm1Sm2OutTrig1ToAdc1Trigger,
    },
};

bool bValidate_CMD_ChChainingWithLoop(eADC_Module_t eModule, eADC_Channel_t eChannel, uint8_t uiLoopCount)
{
    sT_ADC_ChannelMap_t *pstADCModChs = staADC_ChannelMap[eModule];
    uint8_t uiLastChannel = (uint8_t)eChannel + uiLoopCount;

    for(uint8_t i = eChannel; i <= uiLastChannel; i++)
    {
        if(!pstADCModChs[i].stInfo.bIsAvailable)
            return false;
    }
    return true;
}

bool bUpdateADCChannelCommandMap(eADC_Module_t eModule,
                                 eADC_Channel_t eChannel,
                                 uint8_t uiLoopCount,
                                 bool bIsLoopWithChIncrementEnabled,
                                 eADC_TrigSlot_t eTrigSlot,
                                 eADC_Command_t eCommandId,
                                 uint32_t uiMaxRelTime_ms,
                                 uint32_t uiMinRelTime_ms,
                                 uint16_t uiSWAvgSampleCount)
{

    if((eModule >= eNUMBER_OF_ADC_MODULEs) ||
       (eChannel >= eNUMBER_OF_ADC_CHANNELs) ||
       (eTrigSlot >= eNUMBER_OF_ADC_TRIG_SLOTs) ||
       (eCommandId == eADC_CMD_None) ||
       (eCommandId >= eNUMBER_OF_ADC_COMMANDs))
    {
        return false;
    }

    uint8_t uiLastChannel = (uint8_t)eChannel;
    if(bIsLoopWithChIncrementEnabled)
    {
        uiLastChannel += uiLoopCount;
    }

    if(uiLastChannel >= eNUMBER_OF_ADC_CHANNELs)
    {
        return false;
    }

    uint16_t uiNormalizedSampleCount = (uiSWAvgSampleCount == 0U) ? 1U : uiSWAvgSampleCount;
    sT_ADC_ChannelMap_t *pstADCModChs = staADC_ChannelMap[eModule];
    for(uint8_t i = (uint8_t)eChannel; i <= uiLastChannel; i++)
    {
        if(!pstADCModChs[i].stInfo.bIsAvailable)
        {
            return false;
        }

        if(bIsADC_ChannelUsed(eModule, i))
        {
            FHALT("Ch[%d] already being used in ADC Module[%d]. Please verify", i, eModule);
            return false;            
        }
    }

    for(uint8_t i = (uint8_t)eChannel; i <= uiLastChannel; i++)
    {
        if(!bIsADC_ChannelUsed(eModule, i))
        {
            vMark_ADC_CH_InUse(eModule, i);
        }

        sT_ADC_ChMinMax_t *pstStats_Max = &pstADCModChs[i].stStats.stTMaxVal;
        sT_ADC_ChMinMax_t *pstStats_Min = &pstADCModChs[i].stStats.stTMinVal;
        sT_ADC_ChAvgRMS_t *pstStats_Avg = &pstADCModChs[i].stStats.stTAvgVal;
        sT_ADC_ChAvgRMS_t *pstStats_RMS = &pstADCModChs[i].stStats.stTRMSVal;

        pstADCModChs[i].stOwner.eTrigSlot = eTrigSlot;
        pstADCModChs[i].stOwner.eCommandId = eCommandId;

        pstStats_Max->uiReleaseDelay_ms = (uiMaxRelTime_ms == 0)? ADC_STATS_DEAFULT_MAX_RELEASE_TIME_ms:
                                           uiMaxRelTime_ms;
        pstStats_Min->uiReleaseDelay_ms = (uiMinRelTime_ms == 0)? ADC_STATS_DEAFULT_MIN_RELEASE_TIME_ms:
                                           uiMinRelTime_ms;
        atomic_store_explicit(&pstStats_Avg->uiMaxSampleCount, uiNormalizedSampleCount, memory_order_release);
        atomic_store_explicit(&pstStats_RMS->uiMaxSampleCount, uiNormalizedSampleCount, memory_order_release);
    }

    return true;
}

const sT_ADC_ChannelMap_t *pstGetADCChannelMapROnly(eADC_Module_t eModule, eADC_Channel_t eChannel)
{
    if ((eModule < eNUMBER_OF_ADC_MODULEs) && (eChannel < eNUMBER_OF_ADC_CHANNELs))
    {
        return &staADC_ChannelMap[eModule][eChannel];
    }
    return NULL;
}

sT_ADC_ChannelMap_t *pstGetADCChannelData(eADC_Module_t eModule, eADC_Channel_t eChannel)
{    
    if ((eModule < eNUMBER_OF_ADC_MODULEs) && (eChannel < eNUMBER_OF_ADC_CHANNELs))
    {
        return &staADC_ChannelMap[eModule][eChannel];
    }
    return NULL;
}

bool bIsADC_ChannelUsed(eADC_Module_t eModule, eADC_Channel_t eChannel)
{
    if ((eModule < eNUMBER_OF_ADC_MODULEs) && (eChannel < eNUMBER_OF_ADC_CHANNELs))
    {
        return staADC_ChannelMap[eModule][eChannel].stInfo.bIsChUsed;
    }
    return false;    
}

void vMark_ADC_CH_InUse(eADC_Module_t eModule, eADC_Channel_t eChannel)
{
    sT_ADC_ChannelMap_t *pstChannel = pstGetADCChannelData(eModule, eChannel);
    if(pstChannel == NULL)
        return;
    pstChannel->stInfo.bIsChUsed = true;
}

void vRemove_ADC_CH_FromUse(eADC_Module_t eModule, eADC_Channel_t eChannel)
{
    sT_ADC_ChannelMap_t *pstChannel = pstGetADCChannelData(eModule, eChannel);
    if(pstChannel == NULL)
        return;
    pstChannel->stInfo.bIsChUsed = false;    
}

void vRelease_ADCChannelConfig(eADC_Module_t eModule)
{
    if(eModule >= eNUMBER_OF_ADC_MODULEs)
        return;

    for(uint8_t i = eADC_Ch_0; i < eNUMBER_OF_ADC_CHANNELs; i++)
    {
        sT_ADC_ChannelMap_t *pstChannel = &staADC_ChannelMap[eModule][i];

        pstChannel->stInfo.bIsChUsed = false;
        pstChannel->stOwner.eTrigSlot = eNUMBER_OF_ADC_TRIG_SLOTs;
        pstChannel->stOwner.eCommandId = eADC_CMD_None;

        atomic_store_explicit(&pstChannel->stValue.uiADCVal, 0U, memory_order_release);
        atomic_store_explicit(&pstChannel->stStats.stTAvgVal.uiADCVal, 0U, memory_order_release);
        pstChannel->stStats.stTAvgVal.uiADCVal_Sum = 0U;
        pstChannel->stStats.stTAvgVal.uiSampleCount = 0U;

        atomic_store_explicit(&pstChannel->stStats.stTRMSVal.uiADCVal, 0U, memory_order_release);
        pstChannel->stStats.stTRMSVal.uiADCVal_Sum = 0U;
        pstChannel->stStats.stTRMSVal.uiSampleCount = 0U;

        atomic_store_explicit(&pstChannel->stStats.stTMinVal.uiADCVal, UINT16_MAX, memory_order_release);
        pstChannel->stStats.stTMinVal.uiLastTriggerTime_ms = 0U;
        atomic_store_explicit(&pstChannel->stStats.stTMinVal.uiReleaseDelay_ms,
                              ADC_STATS_DEAFULT_MIN_RELEASE_TIME_ms,
                              memory_order_release);

        atomic_store_explicit(&pstChannel->stStats.stTMaxVal.uiADCVal, 0U, memory_order_release);
        pstChannel->stStats.stTMaxVal.uiLastTriggerTime_ms = 0U;
        atomic_store_explicit(&pstChannel->stStats.stTMaxVal.uiReleaseDelay_ms,
                              ADC_STATS_DEAFULT_MAX_RELEASE_TIME_ms,
                              memory_order_release);
    }
}

inputmux_connection_t eGetInputMuxConnection(eADC_Module_t eModule, eADC_TrigSource_t eSrc)
{
    if((eModule < eNUMBER_OF_ADC_MODULEs) && (eSrc < eNUMBER_OF_ADC_TRIG_SOURCEs))
    {
        return eaADCInputMuxConnection[eModule][eSrc];
    }

    return 0U;
}

sT_ADC_ChannelMap_t *pstGetChInfo_ByCmdId_TrigSlot(eADC_Module_t eModule, eADC_TrigSlot_t eTrigSlot, eADC_Command_t eCommandId, uint8_t uiLoopCount)
{
    sT_ADC_ChannelMap_t *pstChannels = staADC_ChannelMap[eModule];
    uint8_t uiLoopIndex = 0;

    for(uint8_t i = 0; i < eNUMBER_OF_ADC_CHANNELs; i++)
    {
        if(pstChannels[i].stOwner.eTrigSlot == eTrigSlot && pstChannels[i].stOwner.eCommandId == eCommandId)
        {
            if(uiLoopIndex == uiLoopCount)
            {
                return &pstChannels[i];
            }

            uiLoopIndex++;
            if(uiLoopIndex > uiLoopCount)
                return NULL;
        }
    }

    return NULL;
}

bool bUpdate_ReleaseTime_OnChStats(sT_ADC_ChRelTimeUpdate_t *pstRelTimeUpdate)
{
    uint32_t uiReleaseDelay_ms = 0;
    if(pstRelTimeUpdate == NULL)
    {
        FHALT("Null Pointer reference");
        return false;
    }
    if(pstRelTimeUpdate->eModule < 0 || pstRelTimeUpdate->eModule >= eNUMBER_OF_ADC_MODULEs)
    {
        FHALT("Invalid ADC Module: %d", pstRelTimeUpdate->eModule);
        return false;        
    }
    if(pstRelTimeUpdate->eChannel < 0 || pstRelTimeUpdate->eChannel >= eNUMBER_OF_ADC_CHANNELs)
    {
        FHALT("Invalid ADC Channel: %d on Module[%d]", pstRelTimeUpdate->eChannel, pstRelTimeUpdate->eModule);
        return false;        
    }
    if(!bIsADC_ChannelUsed(pstRelTimeUpdate->eModule, pstRelTimeUpdate->eChannel))
    {
        return false;
    }

    sT_ADC_ChannelMap_t *pstChData = pstGetADCChannelData(pstRelTimeUpdate->eModule, pstRelTimeUpdate->eChannel);

    switch(pstRelTimeUpdate->eStatType)
    {
        case eADC_Stat_Min:
            uiReleaseDelay_ms = (pstRelTimeUpdate->uiRelTime_ms == 0)? ADC_STATS_DEAFULT_MIN_RELEASE_TIME_ms:
                                                              pstRelTimeUpdate->uiRelTime_ms;
            atomic_store_explicit(&pstChData->stStats.stTMinVal.uiReleaseDelay_ms, uiReleaseDelay_ms, memory_order_release);
            break;
        case eADC_Stat_Max:
            uiReleaseDelay_ms = (pstRelTimeUpdate->uiRelTime_ms == 0)? ADC_STATS_DEAFULT_MAX_RELEASE_TIME_ms:
                                                              pstRelTimeUpdate->uiRelTime_ms;
            atomic_store_explicit(&pstChData->stStats.stTMaxVal.uiReleaseDelay_ms, uiReleaseDelay_ms, memory_order_release);
            break;
        default:
            FHALT("Invalid Stat Type[%d] for Ch[%d] of ADC Module[%d]", 
                  pstRelTimeUpdate->eStatType,
                  pstRelTimeUpdate->eChannel,
                  pstRelTimeUpdate->eModule);
            return false;
    }
    return true;
}
