# SDCARD — SD Card Emulation Directory
#
# This directory is the root of the SD card emulation for the ferp-com simulator.
# pal_mac_sd.cpp maps all pal_sd_* file/directory operations into this folder,
# mirroring the real SD card filesystem on embedded hardware.
#
# Structure:
#   SDCARD/
#     Logs/       — transaction and event log files (written by the firmware)
#     Config/     — any configuration files stored on SD
#     (anything the firmware creates at runtime)
#
# This file keeps the directory tracked in git.
# Actual SD card content is gitignored (see .gitignore).
