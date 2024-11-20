# Boot swap embedded binary example with MCUboot

| Supported Targets | ESP32    | ESP32-S2 | ESP32-S3 | ESP32-C3 | ESP32-C6 | ESP32-H2 |
| ----------------- | -------- | -------- | -------- | -------- | -------- | -------- |

## Overview

This project exemplifies how the IDF bootloader + IDF application can be migrated to another **target bootloader/OS/app** using the `esp-self-reflasher` Embedded reflashing image mode.

The migration is achieved by updating (using IDF OTA system for instance) an IDF based system with this application (this project), which will perform the overwritting of the system.

The **target bootloader** and **target OS/app** considered in this example are the **MCUboot** bootloader and a **NuttX OS** `nsh` application.

Note that the bootloader and NuttX app binaries are concatenated into one single binary considering their MCUboot slot offset configuration (as can be seen in [example_bin_dir](/example_bin_dir)), this binary is the target **reflashing image** that will be embedded into the project application.

## How to use the example

### Configure the project

Open the project configuration menu (`idf.py menuconfig`).

In the `Example Configuration` menu:

* Set the directory where the **reflashing image** is placed;
* Set the name of **reflashing image** filename;
* Set the destination address where the **reflashing image** will be finally copied to. This example considers the bootloader offset in flash as it is overwritting the bootloader;
* Set the destination region size. This example considers the whole region comprehending the bootloader and application slot;

### Build and flash

Considering the goal of the example, the project may be built using:

```CMake
idf.py build
```

And sent through a firmware update method, for instance using the IDF OTA system.

However, to simply run locally, type the following command:

```CMake
idf.py -p PORT flash monitor
```

(To exit the serial monitor, type ``Ctrl-]``.)

See the [Getting Started Guide](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/index.html) for full steps to configure and use ESP-IDF to build projects.

### Example Output

After the mentioned hypothetical update or the local flash, the application will copy the embedded **reflashing image** (in this example, the MCUboot bootloader + NuttX application) to the destination and then reboot, as shown below:

```
I (29) boot: ESP-IDF v5.1.4 2nd stage bootloader
I (29) boot: compile time Nov 30 2024 18:53:53
I (29) boot: Multicore bootloader
I (34) boot: chip revision: v3.0
I (37) boot.esp32: SPI Speed      : 40MHz
I (42) boot.esp32: SPI Mode       : DIO
I (47) boot.esp32: SPI Flash Size : 4MB
I (51) boot: Enabling RNG early entropy source...
I (57) boot: Partition Table:
I (60) boot: ## Label            Usage          Type ST Offset   Length
I (67) boot:  0 nvs              WiFi data        01 02 00009000 00004000
I (75) boot:  1 otadata          OTA data         01 00 0000d000 00002000
I (82) boot:  2 phy_init         RF data          01 01 0000f000 00001000
I (90) boot:  3 factory          factory app      00 00 00010000 00200000
I (97) boot:  4 ota_0            OTA app          00 10 00210000 00180000
I (105) boot: End of partition table
I (109) boot: Defaulting to factory image
I (114) esp_image: segment 0: paddr=00010020 vaddr=3f400020 size=1189f0h (1149424) map
I (537) esp_image: segment 1: paddr=00128a18 vaddr=3ffb0000 size=02124h (  8484) load
I (541) esp_image: segment 2: paddr=0012ab44 vaddr=40080000 size=054d4h ( 21716) load
I (552) esp_image: segment 3: paddr=00130020 vaddr=400d0020 size=13878h ( 79992) map
I (581) esp_image: segment 4: paddr=001438a0 vaddr=400854d4 size=07240h ( 29248) load
I (600) boot: Loaded app from partition at offset 0x10000
I (600) boot: Disabling RNG early entropy source...
I (612) cpu_start: Multicore app
I (612) cpu_start: Pro cpu up.
I (612) cpu_start: Starting app cpu, entry point is 0x40081134
I (0) cpu_start: App cpu up.
I (630) cpu_start: Pro cpu start user code
I (630) cpu_start: cpu freq: 160000000 Hz
I (630) cpu_start: Application information:
I (635) cpu_start: Project name:     boot_swap_embedded
I (641) cpu_start: App version:      87e50ff-dirty
I (646) cpu_start: Compile time:     Nov 30 2024 18:53:41
I (652) cpu_start: ELF file SHA256:  1b81e844620a1f04...
I (658) cpu_start: ESP-IDF:          v5.1.4
I (663) cpu_start: Min chip rev:     v0.0
I (668) cpu_start: Max chip rev:     v3.99
I (673) cpu_start: Chip rev:         v3.0
I (677) heap_init: Initializing. RAM available for dynamic allocation:
I (685) heap_init: At 3FFAE6E0 len 00001920 (6 KiB): DRAM
I (691) heap_init: At 3FFB2998 len 0002D668 (181 KiB): DRAM
I (697) heap_init: At 3FFE0440 len 00003AE0 (14 KiB): D/IRAM
I (703) heap_init: At 3FFE4350 len 0001BCB0 (111 KiB): D/IRAM
I (710) heap_init: At 4008C714 len 000138EC (78 KiB): IRAM
I (717) spi_flash: detected chip: generic
I (720) spi_flash: flash io: dio
I (725) app_start: Starting scheduler on CPU0
I (729) app_start: Starting scheduler on CPU1
I (729) main_task: Started on CPU0
I (739) main_task: Calling app_main()
I (739) boot_swap_embedded_example: Boot swap embedded bin example start
I (749) self_reflasher: Starting copy 0x0010f000 bytes from address 0x00014294 to address 0x00001000
I (13059) self_reflasher: Data copied from address 0x00014294 to region: 0x00001000
ets Jul 29 2019 12:21:46

rst:0xc (SW_CPU_RESET),boot:0x12 (SPI_FAST_FLASH_BOOT)
configsip: 0, SPIWP:0xee
clk_drv:0x00,q_drv:0x00,d_drv:0x00,cs0_drv:0x00,hd_drv:0x00,wp_drv:0x00
mode:DIO, clock div:2
load:0x3fff7c98,len:6176
load:0x40078000,len:13312
load:0x40090000,len:7804
entry 0x4007b3b0
[esp32] [INF] *** Booting MCUboot build v1.10.0-131-gb206b99b ***
[esp32] [INF] [boot] chip revision: v3.0
[esp32] [INF] [boot.esp32] SPI Speed      : 40MHz
[esp32] [INF] [boot.esp32] SPI Mode       : DIO
[esp32] [INF] [boot.esp32] SPI Flash Size : 4MB
[esp32] [INF] [boot] Enabling RNG early entropy source...
[esp32] [INF] Primary image: magic=good, swap_type=0x1, copy_done=0x3, image_ok=0x1
[esp32] [INF] Scratch: magic=unset, swap_type=0x1, copy_done=0x3, image_ok=0x3
[esp32] [INF] Boot source: primary slot
[esp32] [INF] Image index: 0, Swap type: none
[esp32] [INF] Disabling RNG early entropy source...
[esp32] [INF] br_image_off = 0x10000
[esp32] [INF] ih_hdr_size = 0x20
[esp32] [INF] Loading image 0 - slot 0 from flash, area id: 1
[esp32] [INF] DRAM segment: start=0x189b8, size=0x32c, vaddr=0x3ffb1ee0
[esp32] [INF] IRAM segment: start=0x16bc8, size=0x1df0, vaddr=0x40080000
[esp32] [INF] start=0x400819ac
IROM segment aligned lma 0x00020000 vma 0x400d0000 len 0x014960 (84320)
DROM segment aligned lma 0x00010000 vma 0x3f400000 len 0x006b88 (27528)
__esp32_start: ESP32 chip revision is v3.0
NuttShell (NSH) NuttX-10.4.0
nsh>
```
