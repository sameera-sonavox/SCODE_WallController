# SPDX-License-Identifier: Apache-2.0

zephyr_library()
zephyr_library_sources_ifdef(
    CONFIG_WALLCONTROLLER_MCUBOOT_EXTFLASH_PROBE
    src/MCUBoot_ExtFlashProbe.c
)
