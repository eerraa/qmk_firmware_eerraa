VIA_ENABLE = yes

# atmega32u4 with 28,672 usable bytes. The default keymap sits at 81% and VIA
# lands 2 bytes over the ceiling without this, so the size is bought back with
# link-time optimisation rather than by dropping a feature or an effect.
LTO_ENABLE = yes
