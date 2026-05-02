/**
 * @file iohome_constants.h
 * @brief io-homecontrol Protocol Constants and Definitions
 * @author iown-homecontrol project
 *
 * Constants and definitions for the io-homecontrol protocol
 * including frame sizes, sync words, keys, and command IDs.
 */

#pragma once

namespace iohome
{

  // ============================================================================
  // Physical Layer Constants
  // ============================================================================

  // Frequency Configuration
  constexpr uint32_t FREQUENCY_CHANNEL_1 = 868250000; // Hz (2W only)
  constexpr uint32_t FREQUENCY_CHANNEL_2 = 868950000; // Hz (1W/2W primary)
  constexpr uint32_t FREQUENCY_CHANNEL_3 = 869850000; // Hz (2W only)

  // Modulation Parameters
  constexpr uint32_t BIT_RATE = 38400;            // bps
  constexpr uint32_t BAND_WIDTH = 250000;         // Hz
  constexpr uint32_t FREQ_DEVIATION = 19200;      // Hz
  constexpr uint16_t LONG_PREAMBLE_LENGTH = 1024; // 8192 bits, for start frame. Note that 4096 is not enough for solar (low power) blinds!
  constexpr uint16_t SHORT_PREAMBLE_LENGTH = 8;   // 64 bits, for other frames

  // Sync Word
  constexpr uint32_t SYNC_WORD = 0x33FF55;
  constexpr uint8_t SYNC_WORD_LEN = 3;

  // Frequency Hopping
  constexpr int32_t CHANNEL_HOP_TIME_US = 2700;              // microseconds per channel if no preamble detected
  constexpr int32_t CHANNEL_PREAMBLE_TIME_US = 9500;         // microseconds before retry if preamble detected
  constexpr int32_t CHANNEL_RESPONSE_TIME_US = 50000;        // microseconds to wait after a command with no 'end' flag
  constexpr int32_t CHANNEL_RESPONSE_START_TIME_US = 300000; // microseconds to wait after a command with 'start' flag

  // ============================================================================
  // Data Link Layer Constants
  // ============================================================================

  // Frame Sizes
  constexpr uint8_t FRAME_MIN_SIZE = 9;       // bytes (without data) = CTRL_BYTE_0 (1) + CTRL_BYTE_1 (1) + DST (3) + SRC (3) + CMD (1)
  constexpr uint8_t FRAME_MAX_SIZE = 32;      // bytes (max 23 bytes data)
  constexpr uint8_t FRAME_MAX_DATA_SIZE = 23; // maximum data bytes after CMD

  // Frame Field Sizes
  constexpr uint8_t NODE_ID_SIZE = 3;
  constexpr uint8_t HMAC_SIZE = 6;

  // Control Byte 0
  constexpr uint8_t CTRL0_END = 0x80;              // bit 7
  constexpr uint8_t CTRL0_START = 0x40;            // bit 6
  constexpr uint8_t CTRL0_PROTOCOL_MASK = 0x20;    // bit 5 (1W/2W)
  constexpr uint8_t CTRL0_PROTOCOL_MODE_1W = 0x20; // bit 5 (1W)
  constexpr uint8_t CTRL0_PROTOCOL_MODE_2W = 0x00; // bit 5 (2W)
  constexpr uint8_t CTRL0_LENGTH_MASK = 0x1F;      // bits 4-0

  // Control Byte 1
  constexpr uint8_t CTRL1_USE_BEACON = 0x80;       // bit 7
  constexpr uint8_t CTRL1_ROUTED = 0x40;           // bit 6
  constexpr uint8_t CTRL1_LOW_POWER = 0x20;        // bit 5
  constexpr uint8_t CTRL1_ACK = 0x10;              // bit 4
  constexpr uint8_t CTRL1_PROTOCOL_VERSION = 0x0F; // bits 3-0

  // ============================================================================
  // Cryptography Constants
  // ============================================================================

  // AES-128 Parameters
  constexpr uint8_t AES_KEY_SIZE = 16;   // 128 bits
  constexpr uint8_t AES_BLOCK_SIZE = 16; // 128 bits
  constexpr uint8_t IV_SIZE = 16;        // 128 bits

  // Transfer Key (hardcoded, used for key obfuscation during pairing)
  constexpr uint8_t TRANSFER_KEY[AES_KEY_SIZE] = {
      0x34, 0xC3, 0x46, 0x6E, 0xD8, 0x8F, 0x4E, 0x8E,
      0x16, 0xAA, 0x47, 0x39, 0x49, 0x88, 0x43, 0x73};

  // IV Padding Value
  constexpr uint8_t IV_PADDING = 0x55;

  // ============================================================================
  // Command IDs
  // ============================================================================

  // Normal use commands (Execute, Get Status...)
  constexpr uint8_t CMD_EXECUTE_REQUEST = 0x00;   // Authentication needed
  constexpr uint8_t CMD_PRIVATE = 0x03;           // No authentication needed
  constexpr uint8_t CMD_PRIVATE_RESPONSE = 0x04;  // Response to 0x00 and 0x03
  constexpr uint8_t CMD_PRIVATE2 = 0x0C;          // No authentication needed
  constexpr uint8_t CMD_PRIVATE2_RESPONSE = 0x0D; // Response to 0x0C

  // Identify command
  constexpr uint8_t CMD_IDENTIFY = 0x1E; // Authentication needed — makes the device physically identify itself (e.g., brief jog movement)

  // Discovery Commands
  constexpr uint8_t CMD_DISCOVER_REQUEST = 0x28;          // No authentication needed, broadcast to 0x00003b
  constexpr uint8_t CMD_DISCOVER_RESPONSE = 0x29;         // Response to 0x28 and 0x2E
  constexpr uint8_t CMD_DISCOVER_SPE_REQUEST = 0x2A;      // No authentication needed, broadcast to 0x00003b
  constexpr uint8_t CMD_DISCOVER_SPE_RESPONSE = 0x2B;     // Response to 0x2A
  constexpr uint8_t CMD_DISCOVER_CONFIRMATION = 0x2C;     // No authentication needed, sent after receiving 0x29
  constexpr uint8_t CMD_DISCOVER_CONFIRMATION_ACK = 0x2D; // Response to 0x2C
  constexpr uint8_t CMD_DISCOVER2E_REQUEST = 0x2E;        // No authentication needed, broadcast to 0x00003f

  // Key Exchange Commands
  constexpr uint8_t CMD_KEY_INIT_TRANSFER = 0x31;         // Request challenge, sent after 0x2D
  constexpr uint8_t CMD_KEY_TRANSFER = 0x32;              // Response to 0x31 after receiving 0x3C challenge (contains system key protected by TRANSFER_KEY)
  constexpr uint8_t CMD_KEY_TRANSFER_CONFIRMATION = 0x33; // Response to 0X32

  // Authentication Commands (0x3C-0x3D)
  constexpr uint8_t CMD_CHALLENGE_REQUEST = 0x3C;  // Challenge received after sending a command with authentication required (or 0x31)
  constexpr uint8_t CMD_CHALLENGE_RESPONSE = 0x3D; // Response to 0x3C

  // Unknown commands
  constexpr uint8_t CMD_UNKNOWN46_REQUEST = 0x46;  // Authentication needed
  constexpr uint8_t CMD_UNKNOWN46_RESPONSE = 0x47; // Response to 0x46
  constexpr uint8_t CMD_UNKNOWN4A_REQUEST = 0x4A;  // No authentication needed
  constexpr uint8_t CMD_UNKNOWN4A_RESPONSE = 0x4B; // Response to 0x4A

  // Configuration Commands (0x50-0x59)
  constexpr uint8_t CMD_GET_NAME = 0x50;                   // No authentication needed
  constexpr uint8_t CMD_GET_NAME_RESPONSE = 0x51;          // Response to 0x51
  constexpr uint8_t CMD_SET_NAME = 0x52;                   // Authentication needed
  constexpr uint8_t CMD_SET_NAME_ANSWER = 0x53;            // Response to 0x52
  constexpr uint8_t CMD_GET_GENERAL_INFO1 = 0x54;          // No authentication needed
  constexpr uint8_t CMD_GET_GENERAL_INFO1_RESPONSE = 0x55; // Response to 0x54
  constexpr uint8_t CMD_GET_GENERAL_INFO2 = 0x56;          // No authentication needed
  constexpr uint8_t CMD_GET_GENERAL_INFO2_RESPONSE = 0x57; // Response to 0x56
  constexpr uint8_t CMD_GET_GENERAL_INFO3 = 0x58;          // No authentication needed
  constexpr uint8_t CMD_GET_GENERAL_INFO3_RESPONSE = 0x59; // Response to 0x58
  constexpr uint8_t CMD_SET_CONFIG1 = 0x6F;                // Authentication needed
  constexpr uint8_t CMD_SET_CONFIG1_RESPONSE = 0x70;       // Response to 0x6F

  constexpr uint8_t CMD_STATUS_UPDATE = 0x71;          // Sent by device to the Tahoma on channel 1/3 (when 1W is used to control the device and when controlled by Tahoma itself when shutter position is reached), authentication needed
  constexpr uint8_t CMD_STATUS_UPDATE_RESPONSE = 0x72; // Response to 0x71 by the Tahoma after authentication

  // Error response
  constexpr uint8_t CMD_ERROR_RESPONSE = 0xFE; // Response to any command with an error

  // ============================================================================
  // Constants used in commands
  // ============================================================================

  constexpr uint8_t CMD_PARAM_POSITION_STOP = 0xD2;     // STOP POSITION
  constexpr uint8_t CMD_PARAM_POSITION_UNKNOWN = 0xD4;  // UNKNOWN POSITION
  constexpr uint8_t CMD_PARAM_POSITION_FAVORITE = 0xD8; // FAVORITE POSITION

  constexpr uint8_t CMD_PARAM_NAME_MAXSIZE = 32; // 32 to accommodate Latin-1 -> UTF-8 expansion (each non-ASCII byte may become 2 bytes)
  constexpr uint8_t CMD_PARAM_INFO1_MAXSIZE = 16;
  constexpr uint8_t CMD_PARAM_INFO2_MAXSIZE = 16;

  constexpr uint8_t CMD_PARAM_SUBTYPE_MASK = 0x3F;

  constexpr uint8_t CMD_PARAM_STATUS_STOPPED = 0x01;    // In CMD_PRIVATE_RESPONSE and CMD_STATUS_UPDATE, byte0 & 0x01 mens device is not moving
  constexpr uint8_t CMD_PARAM_STATUS_EXPECTED = 0x80;   // In CMD_PRIVATE_RESPONSE and CMD_STATUS_UPDATE, byte1 & 0x80 mens device should send CMD_STATUS_UPDATE
  constexpr uint16_t CMD_PARAM_STATUS_POS_MAX = 0xC800; // 100% closed
  constexpr uint16_t CMD_PARAM_STATUS_POS_TOLERANCE = 100; // ~0.2% tolerance for position comparison (51200 = 100%)

  constexpr uint8_t CMD_PARAM_ERROR_BAD_AUTH = 0x18;

  // ============================================================================
  // Device Types, Manufacturer
  // ============================================================================

  enum class DeviceType : uint8_t
  {
    UNKNOWN = 0x00,
    VENETIAN_BLIND = 0x01,
    ROLLER_SHUTTER = 0x02,
    AWNING = 0x03,
    WINDOW_OPENER = 0x04,
    GARAGE_OPENER = 0x05,
    LIGHT = 0x06,
    GATE_OPENER = 0x07,
    ROLLING_DOOR_OPENER = 0x08,
    LOCK = 0x09,
    BLIND = 0x0A,
    UNKNOWN_0B = 0x0B,
    BEACON = 0x0C,
    DUAL_SHUTTER = 0x0D,
    HEATING_TEMPERATURE_INTERFACE = 0x0E,
    ON_OFF_SWITCH = 0x0F,
    HORIZONTAL_AWNING = 0x10,
    EXTERNAL_VENETIAN_BLIND = 0x11,
    LOUVRE_BLIND = 0x12,
    CURTAIN_TRACK = 0x13,
    VENTILATION_POINT = 0x14,
    EXTERIOR_HEATING = 0x15,
    HEAT_PUMP = 0x16,
    INTRUSION_ALARM = 0x17,
    SWINGING_SHUTTER = 0x18
  };

  enum class Manufacturer : uint8_t
  {
    UNKNOWN = 0x00,
    VELUX = 0x01,
    SOMFY = 0x02,
    HONEYWELL = 0x03,
    HORMANN = 0x04,
    ASSA_ABLOY = 0x05,
    NIKO = 0x06,
    WINDOW_MASTER = 0x07,
    RENSON = 0x08,
    CIAT = 0x09,
    SECUYOU = 0x0A,
    OVERKIZ = 0x0B,
    ATLANTIC_GROUP = 0x0C
  };

  // ============================================================================
  // Broadcast Addresses
  // ============================================================================

  constexpr uint8_t BROADCAST_DISCOVER_ADDRESS[NODE_ID_SIZE] = {0x00, 0x00, 0x3B};
  constexpr uint8_t BROADCAST_DISCOVER2E_ADDRESS[NODE_ID_SIZE] = {0x00, 0x00, 0x3F};

} // namespace iohome
