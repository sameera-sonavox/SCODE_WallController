#ifndef NXP_DAC_DMACONFIG_H
#define NXP_DAC_DMACONFIG_H

#include <stdint.h>
#include <stdbool.h>
#include <zephyr/drivers/dma.h>

#include "NXP_DAC_Types.h"

#define DAC_DMA_WORD_BYTES          sizeof(uint32_t)

extern bool bSetup_DAC_DMA_Circular(const uint32_t *puiDMABuffer, uint16_t uiLen, uintptr_t ptrDAC);
extern void vDisable_DAC_DMA_Circular( void );
extern sT_DAC_DMA_Flags stGet_DAC_DMA_Status( void );

#endif
