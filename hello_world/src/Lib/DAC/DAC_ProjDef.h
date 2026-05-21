#ifndef DAC_PROJDEF_H
#define DAC_PROJDEF_H

#include <zephyr/device.h>

#define DEBUG_DAC_WAVEGEN_SAWTOOTH

#define DAC_NODE                DT_NODELABEL(dac0)
#define DAC_DMA_CTLR_NODE       DT_DMAS_CTLR_BY_NAME(DAC_NODE, tx)

#define DAC_DMA_CHANNEL         DT_DMAS_CELL_BY_NAME(DAC_NODE, tx, mux)
#define DAC_DMA_SLOT_DAC0       DT_DMAS_CELL_BY_NAME(DAC_NODE, tx, source)

#define USE_DMA

#define VREF_VDD_ANA_mV         (3300U) /* VDDANA reference voltage in millivolts. Adjust as needed based on actual hardware configuration. */
#define VREF_EXTERNAL_mV        (3300U) /* External reference voltage in millivolts. Adjust as needed based on actual hardware configuration. */
#define VREF_INTERNAL_mV        (1200U) /* Internal reference voltage in millivolts. Adjust as needed based on actual hardware configuration. */

#endif /* DAC_PROJDEF_H */