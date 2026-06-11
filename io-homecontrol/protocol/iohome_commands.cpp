#include "iohome_commands.hpp"
#include "iohome_crypto.h"
#include <string.h>

#include <time.h>
#include <stdlib.h>
#define GET_TIME_MS() (clock() * 1000 / CLOCKS_PER_SEC)
#define GET_TIME_US() (clock() * 1000000 / CLOCKS_PER_SEC)

namespace iohome
{
    bool create_identify_request(IoFrame &frame, const uint8_t *own_node_id, const uint8_t *dst_node_id, bool is_low_power)
    {
        init_frame(frame, true, true, false, is_low_power);

        // Set source and destination
        set_destination(frame, dst_node_id);
        set_source(frame, own_node_id);

        // Set data: 0x01 = Command Originator (User), 0xFF = identify parameter
        uint8_t data[2] = {0x01, 0xFF};

        return set_command(frame, CMD_IDENTIFY, data, sizeof(data));
    }

    bool create_execute_request(IoFrame &frame, const uint8_t *own_node_id, const uint8_t *dst_node_id, bool is_low_power, uint8_t position, bool quiet)
    {
        // Initialize frame
        init_frame(frame, true, true, false, is_low_power);

        // Set source and destination
        set_destination(frame, dst_node_id);
        set_source(frame, own_node_id);

        // Set data
        uint8_t data[8];
        size_t param_len = 8;
        data[0] = 0x01; // Command Originator => 0x01 = User: User Remote Control causing Actuator Action (value sent by connectivity kit)
        data[1] = 0x67; // ACEI => 0x67 = 0b01100111 = Priority level "User Level 2" / 0011 TBD / IsValid (value sent by connectivity kit)
        if (position <= 100)
        {
            // Real position
            data[2] = 2 * position;        // 100 is closed, 0 is open
            data[3] = 0x00;                // TBD (value sent by connectivity kit)
            data[4] = 0x80;                // TBD (value sent by connectivity kit)
            data[5] = 0xD8;                // TBD (value sent by connectivity kit)
            data[6] = quiet ? 0x05 : 0x06; // TBD (value sent by connectivity kit)
            data[7] = 0x00;                // TBD (value sent by connectivity kit)
        }
        else
        {
            // STOP or "MY" (favorite)
            data[2] = position;
            data[3] = 0x00; // TBD (value sent by connectivity kit)
            data[4] = 0x00; // TBD (value sent by connectivity kit)
            data[5] = 0x00; // TBD (value sent by connectivity kit)
            param_len = 6;
        }

        return set_command(frame, CMD_EXECUTE_REQUEST, data, param_len);
    }

    bool create_execute_tilt_request(IoFrame &frame, const uint8_t *own_node_id, const uint8_t *dst_node_id, bool is_low_power, uint8_t tilt_percent)
    {
        // Initialize frame
        init_frame(frame, true, true, false, is_low_power);

        // Set source and destination
        set_destination(frame, dst_node_id);
        set_source(frame, own_node_id);

        // Set data - tilt command uses 8-byte payload with D2 marker
        // Format: 01 E7 D2 00 20 TT TT 00
        // Where TTTT = tilt as "closed percentage" of 0xC800
        // tilt_percent is "open" percentage (0=closed, 100=open), so we invert it
        uint8_t data[8];
        data[0] = 0x01; // Command Originator => User Remote Control
        data[1] = 0xE7; // ACEI => same as captured tilt commands
        data[2] = CMD_PARAM_POSITION_UNKNOWN; // 0xD4 = CMD_PARAM_POSITION_UNKNOWN parameter marker -> should not stop/change pos if moving
        data[3] = 0x00;
        data[4] = 0x20; // separator/flag (as observed in captures)
        // Convert open percentage to closed value: closed_value = (100 - tilt_percent) * 0xC800 / 100
        uint16_t tilt_value = (uint16_t)((100 - tilt_percent) * CMD_PARAM_STATUS_POS_MAX / 100);
        data[5] = (tilt_value >> 8) & 0xFF;
        data[6] = tilt_value & 0xFF;
        data[7] = 0x00;

        return set_command(frame, CMD_EXECUTE_REQUEST, data, 8);
    }

    bool create_getstatus03_request(IoFrame &frame, const uint8_t *own_node_id, const uint8_t *dst_node_id)
    {
        // Initialize frame
        init_frame(frame, true, true, false, true);

        // Set source and destination
        set_destination(frame, dst_node_id);
        set_source(frame, own_node_id);

        // Set data
        uint8_t data[3] = {0x03, 0x00, 0x00};

        return set_command(frame, CMD_PRIVATE, data, 3);
    }

    bool create_getstatus03_tilt_request(IoFrame &frame, const uint8_t *own_node_id, const uint8_t *dst_node_id)
    {
        // Initialize frame - same as regular status but with extended data to request tilt info
        init_frame(frame, true, true, false, true);

        // Set source and destination
        set_destination(frame, dst_node_id);
        set_source(frame, own_node_id);

        // Set data: 03 20 01 00 - requests tilt-extended status response (16 bytes)
        // This is what the Tahoma sends to get tilt info from venetian blind devices
        uint8_t data[4] = {0x03, 0x20, 0x01, 0x00};

        return set_command(frame, CMD_PRIVATE, data, 4);
    }

    bool create_discovery_request(IoFrame &frame, const uint8_t *own_node_id)
    {
        // Initialize frame for broadcast
        init_frame(frame, true, true, true, false);

        // Set source and broadcast destination
        set_destination(frame, BROADCAST_DISCOVER_ADDRESS);
        set_source(frame, own_node_id);

        return set_command(frame, CMD_DISCOVER_REQUEST);
    }

    bool process_discovery_response(const IoFrame &frame, IoDevice &device)
    {
        if (frame.command_id != CMD_DISCOVER_RESPONSE)
            return false;

        memcpy(device.info.node_id, frame.src_node, NODE_ID_SIZE);

        // Extract device info from frame data
        if (frame.data_len >= 2)
        {
            device.info.device_type = static_cast<DeviceType>(frame.data[0] << 2 | frame.data[1] >> 6);
            device.info.device_subtype = frame.data[1] & CMD_PARAM_SUBTYPE_MASK;
            if (device.info.device_type == DeviceType::HORIZONTAL_AWNING)
                device.info.is_openclose_inverted = true;
        }
        else
        {
            device.info.device_type = DeviceType::UNKNOWN;
            device.info.device_subtype = 0x00;
        }

        if (frame.data_len >= 6)
        {
            device.info.manufacturer = static_cast<Manufacturer>(frame.data[5]);
        }
        else
        {
            device.info.manufacturer = Manufacturer::UNKNOWN;
        }

        return true;
    }

    bool create_discoveryspe_request(IoFrame &frame, const uint8_t *own_node_id, const uint8_t system_key[AES_KEY_SIZE])
    {
        // Initialize frame
        init_frame(frame, true, true, true, true);

        // Set source and bradcast destination
        set_destination(frame, BROADCAST_DISCOVER_ADDRESS);
        set_source(frame, own_node_id);

        // Set Data: 6 random bytes + 6 HMAC
        uint8_t id = CMD_DISCOVER_SPE_REQUEST;
        uint8_t data[12];
        crypto::generate_challenge(data);
        iohome::crypto::create_2w_hmac(&id, 1, data, system_key, data + HMAC_SIZE);

        return set_command(frame, id, data, sizeof(data));
    }

    bool process_discoveryspe_response(const IoFrame &frame, IoDevice &device)
    {
        if (frame.command_id != CMD_DISCOVER_SPE_RESPONSE)
            return false;

        memcpy(device.info.node_id, frame.src_node, NODE_ID_SIZE); // should not be necessary...

        // Extract device info from frame data
        if (frame.data_len >= 2)
        {
            device.info.device_type = static_cast<DeviceType>(frame.data[0] << 2 | frame.data[1] >> 6);
            device.info.device_subtype = frame.data[1] & CMD_PARAM_SUBTYPE_MASK;
        }
        else
        {
            device.info.device_type = DeviceType::UNKNOWN;
            device.info.device_subtype = 0x00;
        }

        if (frame.data_len >= 6)
        {
            device.info.manufacturer = static_cast<Manufacturer>(frame.data[5]);
        }
        else
        {
            device.info.manufacturer = Manufacturer::UNKNOWN;
        }

        return true;
    }

    bool create_discovery_response(IoFrame &frame, const uint8_t *own_node_id, const uint8_t *dst_node_id, uint8_t device_type)
    {
        init_frame(frame, true, true, false, false);
        set_destination(frame, dst_node_id);
        set_source(frame, own_node_id);
        // Data layout matches observed CMD 0x29 responses from Somfy devices:
        // [0-1] device type/subtype encoded as: type = (data[0] << 2) | (data[1] >> 6), subtype = data[1] & 0x3F
        // [2-4] own node ID repeated in data field
        // [5]   manufacturer (0x02 = Somfy)
        // [6-8] padding/counter
        static uint8_t s_counter = 0;
        uint8_t data[9] = {
            static_cast<uint8_t>(device_type >> 2),           // upper 6 bits of device type
            static_cast<uint8_t>((device_type & 0x03) << 6),  // lower 2 bits of device type, subtype = 0
            own_node_id[0], own_node_id[1], own_node_id[2],
            0x02, 0xDC, 0x00, s_counter++
        };
        return set_command(frame, CMD_DISCOVER_RESPONSE, data, sizeof(data));
    }

    bool create_discovery_confirmation_request(IoFrame &frame, const uint8_t *own_node_id, const uint8_t *dst_node_id)
    {
        // Initialize frame
        init_frame(frame, true, true, false, true);

        // Set source and destination
        set_destination(frame, dst_node_id);
        set_source(frame, own_node_id);

        return set_command(frame, CMD_DISCOVER_CONFIRMATION);
    }

    bool create_discovery_confirmation_ack(IoFrame &frame, const uint8_t *own_node_id, const uint8_t *dst_node_id)
    {
        init_frame(frame, true, true, false, false);
        set_destination(frame, dst_node_id);
        set_source(frame, own_node_id);
        return set_command(frame, CMD_DISCOVER_CONFIRMATION_ACK);
    }

    bool create_key_transfer_confirmation(IoFrame &frame, const uint8_t *own_node_id, const uint8_t *dst_node_id)
    {
        init_frame(frame, true, false, false, false);
        set_destination(frame, dst_node_id);
        set_source(frame, own_node_id);
        return set_command(frame, CMD_KEY_TRANSFER_CONFIRMATION);
    }

    bool create_discovery2E_request(IoFrame &frame, const uint8_t *own_node_id)
    {
        // Initialize frame for broadcast
        init_frame(frame, true, true, true, true);

        // Set source and broadcast destination
        set_destination(frame, BROADCAST_DISCOVER2E_ADDRESS);
        set_source(frame, own_node_id);

        uint8_t data[1] = {0x00};

        return set_command(frame, CMD_DISCOVER2E_REQUEST, data, sizeof(data));
    }

    bool create_init_transfer(IoFrame &frame, const uint8_t *own_node_id, const uint8_t *dst_node_id)
    {
        // Initialize frame
        init_frame(frame, true, true, false, true);

        // Set source and destination
        set_destination(frame, dst_node_id);
        set_source(frame, own_node_id);

        return set_command(frame, CMD_KEY_INIT_TRANSFER);
    }

    bool create_launch_key_transfer(IoFrame &frame, const uint8_t *own_node_id, const uint8_t *dst_node_id, const uint8_t challenge[HMAC_SIZE])
    {
        init_frame(frame, true, true, false, true);
        set_destination(frame, dst_node_id);
        set_source(frame, own_node_id);
        return set_command(frame, CMD_LAUNCH_KEY_TRANSFER, challenge, HMAC_SIZE);
    }

    bool create_key_transfer(
        IoFrame &frame,
        IoFrame &old_frame,
        const uint8_t dest_node[NODE_ID_SIZE],
        const uint8_t src_node[NODE_ID_SIZE],
        const uint8_t system_key[AES_KEY_SIZE],
        const uint8_t challenge[HMAC_SIZE])
    {
        // Initialize frame for 2W mode
        init_frame(frame, true, false, false, false);

        // Set source and destination
        set_destination(frame, dest_node);
        set_source(frame, src_node);

        // Encrypt system key with transfer key and challenge
        uint8_t encrypted_key[AES_KEY_SIZE];
        if (!crypto::crypt_2w_key(&old_frame.command_id, 1, challenge, system_key, encrypted_key))
        {
            return false;
        }

        // Set command 0x31 with encrypted key
        return set_command(frame, CMD_KEY_TRANSFER, encrypted_key, AES_KEY_SIZE);
    }

    bool create_challenge_request(
        IoFrame &frame,
        const uint8_t dest_node[NODE_ID_SIZE],
        const uint8_t src_node[NODE_ID_SIZE])
    {
        // Initialize frame for 2W mode
        init_frame(frame);

        // Set source and destination
        set_destination(frame, dest_node);
        set_source(frame, src_node);

        // Generate new challenge
        uint8_t challenge[HMAC_SIZE];
        crypto::generate_challenge(challenge);

        // Set command 0x3C with challenge as parameters
        if (!set_command(frame, CMD_CHALLENGE_REQUEST, challenge, HMAC_SIZE))
        {
            return false;
        }
        return true;
    }

    bool create_challenge_response(
        IoFrame &outframe,
        const uint8_t dest_node[NODE_ID_SIZE],
        const uint8_t src_node[NODE_ID_SIZE],
        const uint8_t received_challenge[HMAC_SIZE],
        const IoFrame &origin_frame,
        const uint8_t *key)
    {
        // Initialize frame for 2W mode, inheriting LOW_POWER from origin frame
        init_frame(outframe, true, false, false, (origin_frame.ctrl_byte_1 & CTRL1_LOW_POWER) != 0);

        // Set source and destination
        set_destination(outframe, dest_node);
        set_source(outframe, src_node);

        // Compute challenge response
        uint8_t frame_data[FRAME_MAX_SIZE];
        uint8_t hmac[HMAC_SIZE];
        frame_data[0] = origin_frame.command_id;
        memcpy(frame_data + 1, origin_frame.data, origin_frame.data_len);
        if (!crypto::create_2w_hmac(frame_data, origin_frame.data_len + 1, received_challenge, key, hmac))
        {
            return false;
        }

        // Set command 0x3D with challenge as parameters
        if (!set_command(outframe, CMD_CHALLENGE_RESPONSE, hmac, HMAC_SIZE))
        {
            return false;
        }
        return true;
    }

    bool create_getname_request(IoFrame &frame, const uint8_t *own_node_id, const uint8_t *dst_node_id, bool is_low_power)
    {
        init_frame(frame, true, true, false, is_low_power);

        // Set source and destination
        set_destination(frame, dst_node_id);
        set_source(frame, own_node_id);

        return set_command(frame, CMD_GET_NAME);
    }

    bool create_setname_request(IoFrame &frame, const uint8_t *own_node_id, const uint8_t *dst_node_id, const char *name, uint8_t name_length)
    {
        // Initialize frame
        init_frame(frame, true, true, false, false);

        // Set source and destination
        set_destination(frame, dst_node_id);
        set_source(frame, own_node_id);

        // Set data
        uint8_t data[CMD_PARAM_NAME_MAXSIZE / 2];
        memset(data, 0, sizeof(data));
        memcpy(data, name, name_length < sizeof(data) ? name_length : sizeof(data) - 1);

        return set_command(frame, CMD_SET_NAME, data, sizeof(data));
    }

    bool create_getinfo1_request(IoFrame &frame, const uint8_t *own_node_id, const uint8_t *dst_node_id, bool is_low_power)
    {
        init_frame(frame, true, true, false, is_low_power);

        // Set source and destination
        set_destination(frame, dst_node_id);
        set_source(frame, own_node_id);

        return set_command(frame, CMD_GET_GENERAL_INFO1);
    }

    bool create_getinfo2_request(IoFrame &frame, const uint8_t *own_node_id, const uint8_t *dst_node_id, bool is_low_power)
    {
        init_frame(frame, true, true, false, is_low_power);

        // Set source and destination
        set_destination(frame, dst_node_id);
        set_source(frame, own_node_id);

        return set_command(frame, CMD_GET_GENERAL_INFO2);
    }

    bool create_getinfo3_request(IoFrame &frame, const uint8_t *own_node_id, const uint8_t *dst_node_id, bool is_low_power)
    {
        init_frame(frame, true, true, false, is_low_power);

        // Set source and destination
        set_destination(frame, dst_node_id);
        set_source(frame, own_node_id);

        return set_command(frame, CMD_GET_GENERAL_INFO3);
    }

    bool create_status_update_response(IoFrame &frame, const uint8_t *own_node_id, const uint8_t *dst_node_id)
    {
        // Initialize frame
        init_frame(frame, true, false, true, false);

        // Set source and destination
        set_destination(frame, dst_node_id);
        set_source(frame, own_node_id);
        uint8_t data[2] = {0x05, 0x00};

        return set_command(frame, CMD_STATUS_UPDATE_RESPONSE, data, sizeof(data));
    }

    bool create_error_response(IoFrame &frame, const uint8_t *own_node_id, const uint8_t *dst_node_id, uint8_t error_code)
    {
        // Initialize frame
        init_frame(frame, true, false, true, false);

        // Set source and destination
        set_destination(frame, dst_node_id);
        set_source(frame, own_node_id);

        return set_command(frame, CMD_ERROR_RESPONSE, &error_code, 1);
    }

    bool create_set_config1_command(IoFrame &frame, const uint8_t *own_node_id, const uint8_t *dst_node_id, bool is_low_power)
    {
        init_frame(frame, true, true, false, is_low_power);

        // Set source and destination
        set_destination(frame, dst_node_id);
        set_source(frame, own_node_id);

        uint8_t data[5] = {0xE0, 0x10, 0x0A, 0x08, 0x00};               // observed on connectivity kit at pairing, also observed E020010800
        return set_command(frame, CMD_SET_CONFIG1, data, sizeof(data)); // device shall reply with command 0x70 and data 0x05 if OK, FE 08 if KO
    }

    bool create_getbattery_request(IoFrame &frame, const uint8_t *own_node_id, const uint8_t *dst_node_id, uint8_t function_id)
    {
        init_frame(frame, true, true, false, true);
        set_destination(frame, dst_node_id);
        set_source(frame, own_node_id);
        uint8_t data[3] = {function_id, 0x00, 0x00};
        return set_command(frame, CMD_PRIVATE, data, sizeof(data));
    }

} // namespace iohome