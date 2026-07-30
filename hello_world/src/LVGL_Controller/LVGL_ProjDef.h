#ifndef LVGL_PROJDEF_H
#define LVGL_PROJDEF_H

#define NUMBER_OF_UI_CONTROLS_PER_AUDIO_SOURCE          1U

#define MAX_TIME_BETWEEN_SCREEN_EVENTs_ms               2000U
#define MAX_ENCODER_MESSAGEs                            12U

#define MIN_POS_DIFFERENCE_FOR_SOURCE_SELECT            1U
#define NAV_COUNT_PER_DETENT                            1U
#define NAV_MAX_STEPS_PER_UPDATE                        3U

//Encoder SW Pin Definition
#define ENC_SW_PIN_NODE                                 DT_ALIAS(enc_sw)
#define LVGL_BACKLIGHT_EN_PIN_NODE                      DT_ALIAS(lvgl_bk_en)
#define EQDC0_PHASE_PIN_NODE                            DT_NODELABEL(eqdc0_phase_pins)


#endif