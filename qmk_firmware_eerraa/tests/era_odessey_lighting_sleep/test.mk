RGBLIGHT_ENABLE = yes
RGBLIGHT_DRIVER = custom
SRC += keyboards/era/newone/common/odessey_common.c
SRC += keyboards/era/common/features/era_lighting_sleep.c
SRC += keyboards/era/common/features/era_rgb_sleep.c
OPT_DEFS += -DRGBLIGHT_LED_COUNT=31 -DRGBLIGHT_SLEEP
OPT_DEFS += -DERA_RGB_SLEEP_MASTER_ENABLE -DERA_LIGHTING_SLEEP_ENABLE -DERA_LIGHTING_SLEEP_OWNED
