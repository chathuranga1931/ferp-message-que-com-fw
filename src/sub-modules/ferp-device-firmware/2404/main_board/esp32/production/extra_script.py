Import("env")

env.AddCustomTarget(
    name="export_binaries",
    dependencies=None,
    actions=[
        "if exist bin_files ( rd /s /q bin_files )",
        "mkdir bin_files",
        "cp .pio/build/esp32dev/bootloader.bin bin_files/",
        "cp .pio/build/esp32dev/firmware.bin bin_files/",
        "cp .pio/build/esp32dev/partitions.bin bin_files/",
        "cp .pio/build/esp32dev/spiffs.bin bin_files/"
    ],
    title="Export Binaries",
    description="Export build files into a folder"
)