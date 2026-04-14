.# Import("env")
# import os
# # import shutil

# env_name = env["PIOENV"]
# build_dir = f".pio/build/{env_name}"
# target_dir = os.path.join("bin_files", env_name)

# # Ensure slashes are Windows-compatible and wrap in quotes
# build_dir_win = build_dir.replace("/", "\\")
# target_dir_win = target_dir.replace("/", "\\")

# env.AddCustomTarget(
#     name="export_binaries",
#     dependencies=None,
#     actions=[
#         f"if exist {target_dir_win} ( rd /s /q {target_dir_win} )",
#         f"mkdir {target_dir_win}",
#         f"copy {build_dir_win}\\bootloader.bin {target_dir_win}\\",
#         f"copy {build_dir_win}\\firmware.bin {target_dir_win}\\",
#         f"copy {build_dir_win}\\partitions.bin {target_dir_win}\\",
#         f"copy {build_dir_win}\\spiffs.bin {target_dir_win}\\"
#     ],
#     title=f"Export Binaries for {env_name}",
#     description=f"Export .bin files for {env_name} into bin_files/{env_name}"
# )

# # def export_binaries(source, target, env):
# #     print(f"Exporting binaries for {env_name}...")

# #     # Clean and recreate output folder
# #     if os.path.exists(target_dir):
# #         shutil.rmtree(target_dir)
# #     os.makedirs(target_dir)

# #     # List of binary files to export
# #     bin_files = ["bootloader.bin", "firmware.bin", "partitions.bin", "spiffs.bin"]

# #     for file_name in bin_files:
# #         src = os.path.join(build_dir, file_name)
# #         dst = os.path.join(target_dir, file_name)
# #         if os.path.exists(src):
# #             shutil.copy(src, dst)
# #             print(f"  Copied: {file_name}")
# #         else:
# #             print(f"  [Warning] {file_name} not found, skipping.")

# # env.AddPostAction("buildprog", export_binaries)



Import("env")
import os
import shutil
import subprocess

env_name = env["PIOENV"]
build_dir = os.path.join(".pio", "build", env_name)
target_dir = os.path.join("bin_files", env_name)

def export_binaries(source, target, env):
    print(f"Exporting binaries for {env_name}...")

    if os.path.exists(target_dir):
        shutil.rmtree(target_dir)
    os.makedirs(target_dir)

    bin_files = ["bootloader.bin", "firmware.bin", "partitions.bin", "spiffs.bin"]

    for file_name in bin_files:
        src = os.path.join(build_dir, file_name)
        dst = os.path.join(target_dir, file_name)
        if os.path.exists(src):
            shutil.copy(src, dst)
            print(f"  Copied: {file_name}")
        else:
            print(f"  [Warning] {file_name} not found, skipping.")

def merge_binaries(source, target, env):
    """Merge all ESP32 bin files into one single merged.bin using esptool.py"""
    print(f"Merging binaries for {env_name}...")

    # Paths to individual bin files (from build dir or exported target dir)
    bootloader  = os.path.join(build_dir, "bootloader.bin")
    partitions  = os.path.join(build_dir, "partitions.bin")
    firmware    = os.path.join(build_dir, "firmware.bin")
    spiffs      = os.path.join(build_dir, "spiffs.bin")
    merged_out  = os.path.join(target_dir, f"merged_{env_name}.bin")

    # Locate esptool.py from PlatformIO's tool package (cross-platform)
    import sys
    pio_home = env.subst("$PLATFORMIO_HOME_DIR")
    if not pio_home:
        if sys.platform == "win32":
            pio_home = os.path.join(os.environ.get("USERPROFILE", "C:\\Users\\Default"), ".platformio")
        else:
            pio_home = os.path.expanduser("~/.platformio")

    esptool_path = os.path.join(pio_home, "packages", "tool-esptoolpy", "esptool.py")
    if not os.path.exists(esptool_path):
        print(f"  [Error] esptool.py not found at: {esptool_path}")
        return

    # Build the esptool merge_bin command
    cmd = [
        env.subst("$PYTHONEXE"), esptool_path,
        "--chip", "esp32",
        "merge_bin",
        "--flash_mode", "dio",
        "--flash_freq", "40m",
        "--flash_size", "4MB",
        "-o", merged_out,
        "0x1000",  bootloader,
        "0x8000",  partitions,
        "0x10000", firmware,
    ]

    # Only include spiffs if it was built
    if os.path.exists(spiffs):
        cmd += ["0x370000", spiffs]
    else:
        print("  [Info] spiffs.bin not found, skipping SPIFFS partition in merge.")

    # All individual bins must exist
    for f in [bootloader, partitions, firmware]:
        if not os.path.exists(f):
            print(f"  [Error] Required file not found: {f}. Aborting merge.")
            return

    os.makedirs(target_dir, exist_ok=True)
    print(f"  Running: {' '.join(cmd)}")
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode == 0:
        print(f"  Merged binary created: {merged_out}")
    else:
        print(f"  [Error] esptool merge_bin failed:\n{result.stderr}")

# Post-build: auto-merge after every normal build
env.AddPostAction("$BUILD_DIR/${PROGNAME}.elf", [export_binaries, merge_binaries])

# Define a custom task "Build & Export"
env.AddCustomTarget(
    name="build_and_export",  # This is the name shown in PlatformIO task menu
    dependencies=None,
    actions=[
        env.VerboseAction("platformio run -e " + env_name, "Building project..."),
        env.VerboseAction("platformio run -t buildfs -e " + env_name, "Building spiffs image..."),
        export_binaries,
        merge_binaries,
    ],
    title="Build & Export",
    description=f"Build the firmware and export .bin files for {env_name}"
)
