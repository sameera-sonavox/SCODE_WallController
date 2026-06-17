#ifndef NXP_ADC_PROJDEF_H
#define NXP_ADC_PROJDEF_H


#define ADC_IRQ_PRIORITY                                2


#define ADC_DMA_THREAD_PRIORITY                         1U
#define ADC_STATISTIC_THREAD_PRIORITY                   0//If you use DMA for acquiring ADC samples, then the statistic thread must run higher priority than the DMA thread.
                                                         //Otherwise it will be overflown

#define ADC_MSGQ_LENGTH                                 256U
#define ADC_STATISTIC_THREAD_STACK_SIZE                 2048U
#define ADC_STATS_DEAFULT_MAX_RELEASE_TIME_ms           10U
#define ADC_STATS_DEAFULT_MAX_RELEASE_STEP_SIZE         5U
#define ADC_STATS_DEAFULT_MIN_RELEASE_TIME_ms           10U
#define ADC_STATS_DEAFULT_MIN_RELEASE_STEP_SIZE         5U

#define ADC0_NODE                                       DT_NODELABEL(lpadc0)
#define ADC0_DMA_CTLR_NODE                              DT_DMAS_CTLR_BY_NAME(ADC0_NODE, fifoa)
#define ADC0_DMA_CHANNEL                                DT_DMAS_CELL_BY_NAME(ADC0_NODE, fifoa, mux)
#define ADC0_DMA_SLOT                                   DT_DMAS_CELL_BY_NAME(ADC0_NODE, fifoa, source)

#define ADC1_NODE                                       DT_NODELABEL(lpadc1)
#define ADC1_DMA_CTLR_NODE                              DT_DMAS_CTLR_BY_NAME(ADC1_NODE, fifoa)
#define ADC1_DMA_CHANNEL                                DT_DMAS_CELL_BY_NAME(ADC1_NODE, fifoa, mux)
#define ADC1_DMA_SLOT                                   DT_DMAS_CELL_BY_NAME(ADC1_NODE, fifoa, source)

#define ADC0_DMA_NO_OF_ADC_RESULTS_PER_TRANSFER         3//It is better to set this value equal to the Watermark level define
#define ADC1_DMA_NO_OF_ADC_RESULTS_PER_TRANSFER         3//Then ADC will assert DMA request, when the RESFIFO > watermark level, then the DMA will read
                                                         // 3 * sizeof(uint32_t) from its source (here ADC RESFIFO)

//ADC0 Channel Average Sample Count Limit
#define ADC0_ADC_CH0_AVG_MAX_SAMPLE_COUNT       3
#define ADC0_ADC_CH1_AVG_MAX_SAMPLE_COUNT       3
#define ADC0_ADC_CH2_AVG_MAX_SAMPLE_COUNT       3
#define ADC0_ADC_CH3_AVG_MAX_SAMPLE_COUNT       3
#define ADC0_ADC_CH4_AVG_MAX_SAMPLE_COUNT       3
#define ADC0_ADC_CH5_AVG_MAX_SAMPLE_COUNT       3
#define ADC0_ADC_CH6_AVG_MAX_SAMPLE_COUNT       3
#define ADC0_ADC_CH7_AVG_MAX_SAMPLE_COUNT       3
#define ADC0_ADC_CH8_AVG_MAX_SAMPLE_COUNT       3
#define ADC0_ADC_CH9_AVG_MAX_SAMPLE_COUNT       3
#define ADC0_ADC_CH10_AVG_MAX_SAMPLE_COUNT      3
#define ADC0_ADC_CH11_AVG_MAX_SAMPLE_COUNT      3
#define ADC0_ADC_CH12_AVG_MAX_SAMPLE_COUNT      3
#define ADC0_ADC_CH13_AVG_MAX_SAMPLE_COUNT      3
#define ADC0_ADC_CH14_AVG_MAX_SAMPLE_COUNT      3
#define ADC0_ADC_CH15_AVG_MAX_SAMPLE_COUNT      3
#define ADC0_ADC_CH16_AVG_MAX_SAMPLE_COUNT      3
#define ADC0_ADC_CH17_AVG_MAX_SAMPLE_COUNT      3
#define ADC0_ADC_CH18_AVG_MAX_SAMPLE_COUNT      3
#define ADC0_ADC_CH19_AVG_MAX_SAMPLE_COUNT      3
#define ADC0_ADC_CH20_AVG_MAX_SAMPLE_COUNT      3
#define ADC0_ADC_CH21_AVG_MAX_SAMPLE_COUNT      3
#define ADC0_ADC_CH22_AVG_MAX_SAMPLE_COUNT      3
#define ADC0_ADC_CH23_AVG_MAX_SAMPLE_COUNT      3
#define ADC0_ADC_CH24_AVG_MAX_SAMPLE_COUNT      3
#define ADC0_ADC_CH25_AVG_MAX_SAMPLE_COUNT      3
#define ADC0_ADC_CH26_AVG_MAX_SAMPLE_COUNT      3
#define ADC0_ADC_CH27_AVG_MAX_SAMPLE_COUNT      3
#define ADC0_ADC_CH28_AVG_MAX_SAMPLE_COUNT      3
#define ADC0_ADC_CH29_AVG_MAX_SAMPLE_COUNT      3
#define ADC0_ADC_CH30_AVG_MAX_SAMPLE_COUNT      3
#define ADC0_ADC_CH31_AVG_MAX_SAMPLE_COUNT      3

//ADC1 Channel Average Sample Count Limit
#define ADC1_ADC_CH0_AVG_MAX_SAMPLE_COUNT       3
#define ADC1_ADC_CH1_AVG_MAX_SAMPLE_COUNT       3
#define ADC1_ADC_CH2_AVG_MAX_SAMPLE_COUNT       3
#define ADC1_ADC_CH3_AVG_MAX_SAMPLE_COUNT       3
#define ADC1_ADC_CH4_AVG_MAX_SAMPLE_COUNT       3
#define ADC1_ADC_CH5_AVG_MAX_SAMPLE_COUNT       3
#define ADC1_ADC_CH6_AVG_MAX_SAMPLE_COUNT       3
#define ADC1_ADC_CH7_AVG_MAX_SAMPLE_COUNT       3
#define ADC1_ADC_CH8_AVG_MAX_SAMPLE_COUNT       3
#define ADC1_ADC_CH9_AVG_MAX_SAMPLE_COUNT       3
#define ADC1_ADC_CH10_AVG_MAX_SAMPLE_COUNT      3
#define ADC1_ADC_CH11_AVG_MAX_SAMPLE_COUNT      3
#define ADC1_ADC_CH12_AVG_MAX_SAMPLE_COUNT      3
#define ADC1_ADC_CH13_AVG_MAX_SAMPLE_COUNT      3
#define ADC1_ADC_CH14_AVG_MAX_SAMPLE_COUNT      3
#define ADC1_ADC_CH15_AVG_MAX_SAMPLE_COUNT      3
#define ADC1_ADC_CH16_AVG_MAX_SAMPLE_COUNT      3
#define ADC1_ADC_CH17_AVG_MAX_SAMPLE_COUNT      3
#define ADC1_ADC_CH18_AVG_MAX_SAMPLE_COUNT      3
#define ADC1_ADC_CH19_AVG_MAX_SAMPLE_COUNT      3
#define ADC1_ADC_CH20_AVG_MAX_SAMPLE_COUNT      3
#define ADC1_ADC_CH21_AVG_MAX_SAMPLE_COUNT      3
#define ADC1_ADC_CH22_AVG_MAX_SAMPLE_COUNT      3
#define ADC1_ADC_CH23_AVG_MAX_SAMPLE_COUNT      3
#define ADC1_ADC_CH24_AVG_MAX_SAMPLE_COUNT      3
#define ADC1_ADC_CH25_AVG_MAX_SAMPLE_COUNT      3
#define ADC1_ADC_CH26_AVG_MAX_SAMPLE_COUNT      3
#define ADC1_ADC_CH27_AVG_MAX_SAMPLE_COUNT      3
#define ADC1_ADC_CH28_AVG_MAX_SAMPLE_COUNT      3
#define ADC1_ADC_CH29_AVG_MAX_SAMPLE_COUNT      3
#define ADC1_ADC_CH30_AVG_MAX_SAMPLE_COUNT      3
#define ADC1_ADC_CH31_AVG_MAX_SAMPLE_COUNT      3

#endif
