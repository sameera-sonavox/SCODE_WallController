#ifndef LVGL_TEXTANIMATE_H
#define LVGL_TEXTANIMATE_H

#include <stdint.h>
#include <stdbool.h>

typedef void (*TextAnimator_Callback_t)(const char *pcaUpdateText);

extern void vStart_TextAnimator(const char *const pcaStringAnimate[],
                                const char *pcaBaseString,
                                uint8_t uiCount,
                                TextAnimator_Callback_t animatorCallback,
                                uint32_t uiPeriod_ms);
extern void vStop_TextAnimator( void );

#endif
