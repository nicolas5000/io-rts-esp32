# Project configuration
The configuration permits to define severals parameters to customize the behavior of the ESP32.
The configuration can be done using several ways:
- Before building the firmware: the default configuration stored in firmware binary
- After building the firmware, while ESP32 is running: configuration is store in NVS partition in Flash and always overrides default configuration.
> [!TIP]
> If you change the default configuration and see no change after loading the firmware, it's probably because you have custom value stored in NVS partition that overrides the default value. You can decide to change directly the value in NVS or delete parameters in NVS to use default values. You can also use ESP-IDF tools to erase the NVS partition in Flash (address 0x370000 and size 0x4000, see [partition.csv](../partitions.csv))

## Ways to configure the project

### Before building the firmware
As previously said, you can choose default values before building the firmware:
- In VSCode: use the ESP-IDF addon by clicking on "SDK Configuration Editor (menuconfig)". Then go to "IO RTS Project Configuration" section.
- Using a terminal: execute command 'idf.py menuconfig' (see ESP-IDF documentation)
> [!NOTE]
> Some parameters can be defined only defore building the firmware, especially parameters related to hardware configuration.
Most of the parameters have a description to help you to choose the value.

#### Main parameters
This section permits to configure:
- Default value for password used to protect command line access
- Wifi or Ethernet connectivity
- DHCP or static IPv4
- ESP32 hostname on your network
- NTP server to use to synchronize time on ESP32

#### Static IP configuration parameters
This section permits to configure:
- IPv4 address for the ESP32
- Network mask
- Gateway address
- Main DNS server address
- Backup DNS server address

#### Wifi configuration parameters
This section permits to configure:
- Wifi SSID
- Wifi password / key
- WPA3 SAE mode (if using WPA3)
- Password identifier (if using SAE H2E)
- WiFi authentication mode threshold: set it directly to the best value supported by your Wifi access point!

#### Ethernet configuration parameters
If you choose Ethernet connectivity in the main parameters, this section permits to configure parameters related to W5500 module:
- SPI host to use (depending on your ESP32 chip, you can have HOST2 and HOST3 or only HOST2)
- SPI clock speed (MHz)
- GPIO to use for:
  - SPI CS
  - Interrupt
  - PHY Reset

#### IO-HomeControl configuration parameters
This section permits to configure:
- Enable logging in IO-HomeControl layer (all logs from the IO-HomeControl layer will be sent to command line interface, and MQTT log topic if MQTT is configured)
- Passive mode only (ESP32 is not allowed to emit)
- Ignore auto-update flag: when enabled, the estimated time from device responses is used to schedule status polls instead of trusting the device to send a proactive update. Useful when the device sends its auto-update to a different controller address.
- IO System key, choose an unique value for your site
- IO Node ID, choose unique value for your ESP32 to identify itself when sending commands to IO devices
- TX Power (dBm), choose a value high enough to control your devices. A bigger value means more people around you could listen what you do...
- SPI HOST to use fo the radio module
- GPIO connected to radio module and used for:
  - RST
  - DIO0
  - DIO4 (optional, set value to -1 if not connected on your board)

#### MQTT configuration parameters
This section permits to configure:
- MQTT client activation
- MQTT Broker address
- MQTT Broker port
- MQTT Client ID (must be unique in your broker)
- MQTT Client username and password to authenticate when connecting to the broker
- TLS protection and certificate when connecting to the broker
- MQTT prefix to use for all topics (except discovery)
- MQTT prefix to use for discovery topic

#### Hardware configuration parameters
This section permits to configure GPIO for SPI HOST2 and/or HOST3 if available and used by radio and/or Ethernet module(s):
- SPI SCK
- SPI MOSI
- SPI MISO

### After building the firmware
Most of the parameters, excluding hardware configuration, can be changes using the command line / console interface available on serial interface (or USB to serial interface). Check these commands:
- config_network for configuration related to IPv4 / DHCP / DNS / NTP
- config_wifi for configuration related to Wifi
- config_mqtt for configuration related to MQTT
- io_config for configuration related to IO-HomeControl protocol
> [!IMPORTANT]
> By default the command line interface is password protected. It strongly recommanded to change the default password (before building the firmware or after loading the firmware in ESP32 chip).
In addition, a few parameters can be changed using MQTT topics.
