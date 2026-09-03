#ifndef NXP_DAC_PROJDEF_H
#define NXP_DAC_PROJDEF_H

#include <zephyr/device.h>

// #define DEBUG_DAC_WAVEGEN_SAWTOOTH
// #define DEBUG_DAC_WAVEGEN_SINE
#define DEBUG_DAC_WAVEGEN_NOISE

#define DAC_MAX_WAVEFORM_SAMPLE_COUNT   4096U
#define DAC_NODE                        DT_NODELABEL(dac0)
#define DAC_DMA_CTLR_NODE               DT_DMAS_CTLR_BY_NAME(DAC_NODE, tx)

#define DAC_DMA_CHANNEL                 DT_DMAS_CELL_BY_NAME(DAC_NODE, tx, mux)
#define DAC_DMA_SLOT_DAC0               DT_DMAS_CELL_BY_NAME(DAC_NODE, tx, source)

#define DMA_TCD_RING_BUFF_COUNT         4//Must match with the k-config 'CONFIG_DMA_TCD_QUEUE_SIZE'
#define DMA_NOISEGEN_SAMPLE_COUNT       1024

#define USE_DMA

#define VREF_VDD_ANA_mV                 (3300U) /* VDDANA reference voltage in millivolts. Adjust as needed based on actual hardware configuration. */
#define VREF_EXTERNAL_mV                (3300U) /* External reference voltage in millivolts. Adjust as needed based on actual hardware configuration. */
#define VREF_INTERNAL_mV                (1200U) /* Internal reference voltage in millivolts. Adjust as needed based on actual hardware configuration. */

typedef enum
{
    eTCDBuff_0,
    eTCDBuff_1,
    eTCDBuff_2,
    eTCDBuff_3,
    eNUMBER_OF_BUFFERs//Must match to 'DMA_TCD_RING_BUFF_COUNT'
} eT_TCDBuff_t;

#endif /* NXP_DAC_PROJDEF_H */
