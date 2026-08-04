#ifndef LVGL_PROJDEF_H
#define LVGL_PROJDEF_H

#define NUMBER_OF_UI_CONTROLS_PER_AUDIO_SOURCE          2U

#define MAX_TIME_BETWEEN_SCREEN_EVENTs_ms               2000U
#define VOLUME_EDIT_TIMEOUT_ms                          5000U
#define MAX_ENCODER_MESSAGEs                            12U

#define MIN_POS_DIFFERENCE_FOR_SOURCE_SELECT            1U
#define NAV_COUNTS_PER_DETENT                           4
#define NAV_MAX_STEPS_PER_UPDATE                        3U
#define MAX_AUDIO_SRC_VOLUME                            100U

//Encoder SW Pin Definition
#define ENC_SW_PIN_NODE                                 DT_ALIAS(enc_sw)
#define LVGL_BACKLIGHT_EN_PIN_NODE                      DT_ALIAS(lvgl_bk_en)
#define EQDC0_PHASE_PIN_NODE                            DT_NODELABEL(eqdc0_phase_pins)

//Volume update tuning parameters
#define VOLUME_ACCEL_RESET_TIME_ms                      300U

#define VOLUME_MEDIUM_SPEED_DPS                         5U
#define VOLUME_FAST_SPEED_DPS                           10U
#define VOLUME_VERY_FAST_SPEED_DPS                      20U

#define VOLUME_MEDIUM_MULTIPLIER                        2
#define VOLUME_FAST_MULTIPLIER                          5
#define VOLUME_VERY_FAST_MULTIPLIER                     10

#define MAX_VOLUME_CHANGE_PER_UPDATE                    15


#endif
