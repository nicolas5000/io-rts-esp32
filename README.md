> [!CAUTION]
> Breaking change! Since merge of PR #28 on July 3rd 2026 flash mapping has changed: devices partition is reduced and webserver partition is bigger. The consequence is that all your devices will be lost after upgrade. Please be sure to save your devices information (you can use web UI) before upgrading to last version, then restore your devices information!

# io-rts-esp32
IO-Homecontrol 2W (2-Way, with device feedback) protocol and legacy RTS protocol implementation on ESP32-S3 hardware based on ESP-IDF SDK version 6.0.0

### Acknowledgment

This project is based on the wonderful work of:

[Velocet/iown-homecontrol](https://github.com/Velocet/iown-homecontrol)  for the original project, and especially for all the IO-Homecontrol protocol documentation

[cridp/iown-homecontrol-esp32sx1276](https://github.com/cridp/iown-homecontrol-esp32sx1276) especially for the SX1276 registers management part

### **Disclaimer**  
> [!CAUTION]
> This tool designed for educational and testing purposes, provided "as is", without warranty of any kind. Creators and contributors are not responsible for any misuse or damage caused by this tool. Keep in mind that it is forbidden in most countries to try to interact with IO-Homecontrol devices that are not yours and you may be sued for doing it.

_I don't give any support concerning the IO-Homecontrol 1W part. I give limited support depending on my free time._

### Documentation
- All IO-Homecontrol details require you to carefully read the work of [Velocet/iown-homecontrol](https://github.com/Velocet/iown-homecontrol) to understand this protocol.
- The documentation provides useful information about this project, especially:
  - On IO-Homecontrol: the [list of supported devices](doc/supported_devices.md), the current knowledge about the protocol and commands (in addition to what is already documented in [Velocet/iown-homecontrol](https://github.com/Velocet/iown-homecontrol))
  - On this project development (structure of the project, how the source code is organized if you want to contribute or fork...)

### Current status and coming features

These features are currently available:
- IO-Homecontrol devices are supported to:
  - Add device (if already paired) / Discover and pair device (if never paired)
  - Change device name
  - Open / Close / Stop / Set to favorite position / Set to specific position (0-100%) for devices of type "blind" / "screen" (devices based on movement and position)
  - Change tilt position (for devices with tilt support)
  - On / Off for devices of type "light" or "switch"
  - Get status feedback (currently moving, current position...), that's the main advantage of the 2W implementation!
- Connectivity:
  - Wifi (integrated to ESP32 chip) support
  - Ethernet support (based on W5500 module)
  - DHCP support, with DNS provided by DHCP server
  - Static IPv4 support, including manual DNS server configuration
- (S)NTP support for time synchronization
- Front-end:
  - Command line features: see [dedicated page](doc/command_line.md) for more information
    - Control IO devices: discover and pair, add, open, close, stop, set to favorite position, set to specific position (0-100%), change name inside device, link a remote to a device, delete a device, delete a remote, invert OPEN/CLOSE positions
    - Reboot ESP32
    - Change Wifi configuration without reflashing firmware (configuration applied after reboot)
    - Change DHCP/IPv4 configuration without reflashing firmware (configuration applied after reboot)
    - Change MQTT configuration without reflashing firmware (configuration applied after reboot)
    - Change IO-Homecontrol configuration without reflashing firmware (configuration applied after reboot)
    - Note: command line features are password protected by default, change default password in project configuration before building firmware.
  - MQTT support: see [dedicated page](doc/mqtt.md) for more information
    - If IO logging and MQTT are both enabled, IO layer messages are sent to MQTT_TOPIC_PREFIX/log
    - Discovery message is published and compatible with Home Assistant, permitting to automatically add devices to it without extra configuration (only in active mode).
    - Currently all devices based on position (shutter, blind, awning, window opener, garage opener, gate opener), ON/OFF switch, ON/OFF light and locks are expected to work.
    - A "Favorite position" button for devices based on position (shutter, blind, awning, window opener, garage opener, gate opener) is available.
    - In addition to devices:
      - A button is added to reboot the ESP32 board.
      - A button is added to discover and pair a new IO device (to add device never paired to 2W box or reset to factory settings)
      - An input is added to add an IO device already paired (the device must already contain the system key of your installation). Enter the device ID (XXYYZZ) in the input and validate.
      - An input is added to delete an IO device already added / paired. Enter the device ID (XXYYZZ) in the input and validate.
      - An input is added to change name of an IO device already added / paired. Enter the device ID and new name separated by ; (XXYYZZ;New name) in the input and validate. The new name length must be less or equal to 16 characters.
      - An input is added to link an IO device to an IO remote (IO device must already be added). Enter the device ID and remote ID separated by a ; (XXYYZZ;AABBCC) in the input and validate.
      - An input is added to delete an IO remote already added. Enter the remote ID (XXYYZZ) in the input and validate.
      - An input is added to invert OPEN/CLOSE positions on an IO device already added / paired. Enter the device ID (XXYYZZ) in the input and validate.
      - A switch is added to enable/disable IO layer logging (applied after reboot)
      - A switch is added to enable/disable IO passive mode (applied after reboot)
      - A slider is added to configure IO Tx power (applied after reboot)
- Configuration storage to flash
- Devices storage to flash (thanks [@kfroeschl](https://github.com/kfroeschl))

These features should be available before end of 2026 depending on my available time:
- ESP32 security features and OTA sofware update (expected October-November 2026 :calendar:, optional, enable if you want): flash encryption, secure boot, firmware signature, update over Wifi/Ethernet with rollback in case of failure
- RTS protocol for legacy devices, based on CC1101 module (expected November-December 2026 :calendar:)

These features could be added, if useful:
- Support for new devices: I don't have these devices, my devices work well, so it will depend on you! See [supported devices](doc/supported_devices.md)
- Web: configuration, devices control and status

### Hardware requirements
In order to execute this project, you will need:
- ESP32-S3 board
- SX1276 radio module (it can be included on the ESP32 board) with SPI (SCLK, MOSI, MISO, CS) and RST, DIO0, DIO2 and DIO4 pins wired to ESP32. Note: set -1 if DIO2 or DIO4 is not wired on your board.
- Wires to connect the radio module to the board
- USB cable to connect the board to your computer

### Development environment

The development environment is based on:
- Visual Studio Code (also known as VSCode)
- ESP-IDF extension for VSCode and ESP-IDF SDK installed

### Supported devices

See [supported devices](doc/supported_devices.md) for more information about currently supported devices and how to support new devices.

### Starting guide

Here are a few steps to follow to start with this project:
1. If you are nor familiar with VSCode and ESP-IDF, I encourage you to read ESP-IDF starting guide and try the "Hello world" example on your ESP32-S3 board. You should be able to build the example, flash the binary to your ESP32-S3 board and monitor the execution from ESP-IDF monitor tool before going to next step.
2. Download this project / clone the repository, then open the project folder in VSCode.
3. Choose the ESP32-S3 target
4. Open "SDK Configuration Editor" to configure the project and go to "IO RTS Project Configuration" section. Configure network and choose the GPIO pins you want to use to connect the SX1276 radio module. The default configuration is compatible with the [Lilygo T3S3 SX1276 board](https://wiki.lilygo.cc/get_started/en/LoRa_GPS/T3S3/T3S3.html#Pin-Overview) even if I have never tested this board...
5. Use wires to connect all the pins between ESP32-S3 board and SX1276 module (power, ground, and all pins chosen in the configuration)
6. Build the source code, flash it to the board and monitor (there is single button that does everything if you are confident, otherwise, use the 3 buttons in this order).

By default the project is configured with verbose enabled and in passive mode: you can see what happens with detailed logs but you can't control your IO devices. In active mode you can use the command line to send commands to your devices. You should first use passive mode to retrieve useful information about your devices before switching to active mode.

#### Passive mode

As previously explained, in passive mode (don't forget to enable verbose) you will get very useful information about your IO-Homecontrol devices.
Once in this mode, you should:
1. Use each remote to control each device (you can press the "open" button even if the blind is already opened, it is enough to get what you want). You will see logs like "command 00 from XXXXXX to 00003F" &rarr; note the XXXXXX, it is the ID of your remote.
2. If you have a box (Tahoma, connectivity kit, ...), choose a device that is easy to reset and then extract your site key:
  - Remove the device from the app (like Tahoma app)
  - Reset the device to forget any previously paired remote or box. Depending on the device, it can be done through power supply and/or using the remote. See [there](https://www.youtube.com/watch?v=Z6ZYs2_-aa4) or [there](https://www.amc-production.fr/guides-conseils/remettre-a-zeros-moteur-rs100-solar-io-somfy) for example on RS100 SOLAR IO.
  - First, pair your remote(s) to control the device
  - Then, use the app (like Tahoma app) to pair the box to the device (like you probably did it in the past).
  - You should then see a log like "Extracted a key to control device XXXXXX: YYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYY" where XXXXXX is the device ID and YYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYY is your private site key (never share it, even with me!).
3. If you have a box (Tahoma, connectivity kit, ...), use it to control each device. You will see logs like "command 00 from XXXXXX to YYYYYY" &rarr; note the XXXXXX is the ID of your box and YYYYYY is the ID of your device.

After that you should know:
- All your remotes IDs
- If you have a box (Tahoma, connectivity kit, ...), all your devices IDs, your box ID and your private site key

Note: you can also use the passive mode to record commands from your box if your devices are not yet fully supported by the project, it will be very useful.

#### Active mode

Once you have everything from passive mode, you can change your configuration:
- Disable passive mode
- Keep verbose enabled if you want to see what happens in IO-Homecontrol layer
- Choose an 3 byte ID for your ESP32-S3: I recommand that you take any random value for testing but if you have a box you should at the end use the box ID (once the box is unplugged) because some devices automatically send their status to the box ID used during pairing process when they are controlled by any other remote.
- Choose a private site key: use a random value or use the value extracted in passive mode if you have a box.
- Then build, flash, monitor. You can use command line to take control of all your devices. Try 'help' to see the commands. You should start by adding already paired device and discover (and pair) any device that has nver been paired to a box.

Notes:
- If you want to pair a device with a new key and this device was already paired to a box in the past, you have to first reset (see [Passive mode section](README.md#passive-mode))
- You shall keep your private site key, your ESP32-S3 Node ID and all your remotes and devices IDs in a secure place to be able to reload them in the future. If you keep them in the project folder and clean this folder you will have to restart from the beginning!

### How to contribute

You can mainly contribute to this project by reporting any issue and by checking if your devices are supported and open a discussion to describe what is working or not. I will update the list of supported devices based on your feedbacks! See [supported devices](doc/supported_devices.md) for the list of known supported devices and how to provide useful information about (not yet) supported devices.

If you have development skills you can also propose source code modification to fix issues or add new supported devices.
