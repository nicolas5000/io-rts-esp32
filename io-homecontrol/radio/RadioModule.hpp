#pragma once

#include <stdint.h>

namespace RadioLinks
{

    enum Modulation : uint32_t
    {
        OOK = 0x00,
        FSK,
        LoRa
    };

    enum RADIO_ERRCODE : int16_t
    {
        RADIO_ERR_INVALID_OUTPUT_POWER = -9,
        RADIO_ERR_INVALID_SYNCWORD = -8,
        RADIO_ERR_INVALID_BANDWIDTH = -7,
        RADIO_ERR_INVALID_MODULATION = -6,
        RADIO_ERR_INVALID_GPIO = -5,
        RADIO_ERR_BUSY = -4,
        RADIO_ERR_HARDWARE = -3,
        RADIO_ERR_NOT_INITIALIZED = -2,
        RADIO_ERR_NULL_POINTER = -1,
        RADIO_ERR_NONE = 0,
    };

    class RadioModule
    {
    public:
        // Life cycle

        virtual ~RadioModule() {};

        /// @brief Initialize ESP32 hardware (SPI, GPIO...) and radio module (registers...)
        /// @param ioMode true if using IO protocol specific registers, false otherwise
        virtual void Init(bool ioMode) = 0;

        /// @brief Register callback that will be called when a frame is received on radio link
        /// @param func_ptr Callback that will receive buffer length, buffer, frequency, rssi, time since preambule values
        virtual void RegisterReceiveCallback(void (*func_ptr)(uint8_t len, uint8_t buffer[], uint32_t frequency, float rssi, int64_t time_since_preamble)) = 0;

        // Configuration

        /// @brief Set frequency
        /// @param frequency Frequency in Hz
        /// @return RADIO_ERR_NONE if no error, other value depending on error
        virtual RADIO_ERRCODE SetFrequency(uint32_t frequency) = 0;

        /// @brief Set modulation
        /// @param modulation OOK, FSK...
        /// @return RADIO_ERR_NONE if no error, other value depending on error
        virtual RADIO_ERRCODE SetModulation(Modulation modulation) = 0;

        /// @brief Set Output (Tx) power
        /// @param txPower Power in dBm
        /// @return RADIO_ERR_NONE if no error, other value depending on error
        virtual RADIO_ERRCODE SetOutputPower(uint8_t txPower) = 0;

        /// @brief Set bitrate
        /// @param bitrate bits per second
        /// @return RADIO_ERR_NONE if no error, other value depending on error
        virtual RADIO_ERRCODE SetBitRate(uint32_t bitrate) = 0;

        /// @brief Set frequency deviation
        /// @param deviation Frequency deviation in Hz
        /// @return RADIO_ERR_NONE if no error, other value depending on error
        virtual RADIO_ERRCODE SetFrequencyDeviation(uint32_t deviation) = 0;

        /// @brief Set sync word
        /// @param size Sync word size (in bytes)
        /// @param syncWord Sync word
        /// @return RADIO_ERR_NONE if no error, other value depending on error
        virtual RADIO_ERRCODE SetSyncWord(uint8_t size, uint8_t *syncWord) = 0;

        /// @brief Set bandwidth
        /// @param bandwidth Bandwidth in Hz
        /// @return RADIO_ERR_NONE if no error, other value depending on error
        virtual RADIO_ERRCODE SetBandwidth(uint32_t bandwidth) = 0;

        /// @brief Set preamble length. Radio must be in standby mode (not receiving, not sending) before calling.
        /// @param preambleLen Preamble length in bytes
        /// @return RADIO_ERR_NONE if no error, other value depending on error
        virtual RADIO_ERRCODE SetPreambleLength(uint16_t preambleLen) = 0;

        // Operations

        /// @brief Start listening for incoming frames
        /// @return RADIO_ERR_NONE if no error, other value depending on error
        virtual RADIO_ERRCODE StartReceive() = 0;

        /// @brief Stop listening for incoming frames
        /// @return RADIO_ERR_NONE if no error, other value depending on error
        virtual RADIO_ERRCODE StopReceive() = 0;

        /// @brief Send a frame on radio link
        /// @param len Length of the frame to send, in bytes
        /// @param buffer Buffer containing the frame to send
        /// @param preambleLen Preamble length before sending the frame
        /// @param frequency Frequency to use to send the frame
        /// @return RADIO_ERR_NONE if no error, other value depending on error
        virtual RADIO_ERRCODE Send(uint8_t len, uint8_t *buffer, uint16_t preambleLen, uint32_t frequency) = 0;

        /// @brief Call to know if a preamble has been detected since last call to this method
        /// @return true if a preamble has been detected since last call to this method, false otherwise.
        virtual bool isPreambleDetected() = 0;
    };

}