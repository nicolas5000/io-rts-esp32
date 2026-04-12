#pragma once

#include "RadioModule.hpp"

#include "driver/spi_master.h"
#include "driver/gpio.h"

namespace RadioLinks
{
    /// @brief This class permits to control a SX1276 radio module. Use the constructor to create the object, then use the RadioModule interface to control.
    class RadioSX1276 : public RadioModule
    {
    public:
        /// @brief Constructor for SX1276 radio module interface
        /// @param spiHost SPI Host Controller ID to use
        /// @param cs SPI - GPIO connected to CS pin
        /// @param rst GPIO connected to RST pin
        /// @param dio0 GPIO connected to DIO0 pin
        /// @param dio4 GPIO connected to DIO4 pin
        explicit RadioSX1276(spi_host_device_t spiHost, int cs, int rst, int dio0, int dio4);

        /// @brief Dump all SX1276 registers to console
        void DumpRegisters();

        /// @brief Called internally when a GPIO is triggered by SX1276, do not call directly
        /// @param gpio GPIO triggered
        void ManageInterrupt(int gpio);

        // RadioModule interface methods

        ~RadioSX1276() override;
        void Init(bool ioMode) override;
        void RegisterReceiveCallback(void (*func_ptr)(uint8_t, uint8_t[], uint32_t, float, int64_t)) override;
        RADIO_ERRCODE SetFrequency(uint32_t frequency) override;
        RADIO_ERRCODE SetModulation(Modulation modulation) override;
        RADIO_ERRCODE SetOutputPower(uint8_t txPower) override;
        RADIO_ERRCODE SetBitRate(uint32_t bitrate) override;
        RADIO_ERRCODE SetFrequencyDeviation(uint32_t deviation) override;
        RADIO_ERRCODE SetSyncWord(uint8_t size, uint8_t *syncWord) override;
        RADIO_ERRCODE SetBandwidth(uint32_t bandwidth) override;
        RADIO_ERRCODE SetPreambleLength(uint16_t preambleLen) override;
        RADIO_ERRCODE StartReceive() override;
        RADIO_ERRCODE StopReceive() override;
        RADIO_ERRCODE Send(uint8_t len, uint8_t *buffer, uint16_t preambleLen, uint32_t frequency) override;
        bool isPreambleDetected() override;

    private:
        static constexpr const char *TAG = "RadioSX1276"; // tag to use when logging to console

        spi_host_device_t mSpiHost = SPI_HOST_MAX;
        int mSpiCS;   // SPI - CS pin
        spi_device_handle_t mSpiHandle = nullptr;

        int mIoRST;  // GPIO connected to RST pin
        int mIoDIO0; // GPIO connected to DIO0 pin
        int mIoDIO4; // GPIO connected to DIO4 pin

        void (*mCallback)(uint8_t len, uint8_t buffer[], uint32_t frequency, float rssi, int64_t time_since_preamble) = nullptr;

        bool mIsReceiveMode = false;       // is currently in receivins state (set to true by calling StartReceive())
        int64_t mLastPreambleDetectedTime; // in us, use esp_timer_get_time() to fill
        uint32_t mCurrentFrequency;        // current frequency in use

        /// @brief Initialize SPI
        void spiBegin();

        /// @brief Free SPI resources
        void spiEnd();

        /// @brief Configure GPIO
        /// @param pin GPIO pin to configure
        /// @param mode GPIO mode
        /// @param pullup GPIO pullup configuration
        /// @param pulldown GPIO pulldown configuration
        void pinMode(int pin, gpio_mode_t mode, gpio_pullup_t pullup, gpio_pulldown_t pulldown);

        /// @brief Change state of a GPIO
        /// @param pin GPIO pin to change state
        /// @param value State to apply to the GPIO (0 for low level or 1 for high level)
        /// @return RADIO_ERR_NONE if no error, other value depending on error
        RADIO_ERRCODE digitalWrite(int pin, uint32_t value);

        /// @brief Read state of a GPIO
        /// @param pin GPIO pin to read value from
        /// @return Value read from the GPIO (0 or 1)
        uint32_t digitalRead(int pin);

        /// @brief Write 1 byte to SX1276 register
        /// @param regAddr Register address
        /// @param data Byte to write to register
        /// @return RADIO_ERR_NONE if no error, other value depending on error
        RADIO_ERRCODE SpiWriteByte(uint8_t regAddr, uint8_t data);

        /// @brief Write several bytes to SX1276 registers
        /// @param regAddr First register address
        /// @param data Data to write to registers
        /// @param len Number of bytes to write
        /// @return RADIO_ERR_NONE if no error, other value depending on error
        RADIO_ERRCODE SpiWriteBytes(uint8_t regAddr, uint8_t *data, uint8_t len);

        /// @brief Read several bytes from SX1276 registers
        /// @param regAddr First register address
        /// @param data Data read from registers
        /// @param len Number of bytes to read
        /// @return RADIO_ERR_NONE if no error, other value depending on error
        RADIO_ERRCODE SpiReadBytes(uint8_t regAddr, uint8_t *data, uint8_t len);

        /// @brief Read 1 byte from SX1276 register
        /// @param regAddr Register address
        /// @return Value of the byte read from register
        uint8_t SpiReadByte(uint8_t regAddr);

        /// @brief Calibrate Radio module
        /// @return RADIO_ERR_NONE if no error, other value depending on error
        RADIO_ERRCODE Calibrate();

        /// @brief Initialize all registers of the SX1276
        /// @param ioMode Initialize with IO-HomeControl compatibility
        /// @return RADIO_ERR_NONE if no error, other value depending on error
        RADIO_ERRCODE InitRegisters(bool ioMode);

        /// @brief Set SX1276 to Standby mode
        /// @return RADIO_ERR_NONE if no error, other value depending on error
        RADIO_ERRCODE Standby();
    };

}