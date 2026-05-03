#pragma once

#include <stdint.h>
#include <stddef.h>
#include "iohome_constants.h"
#include "iohome_frame.hpp"
#include "iohome_device.hpp"

namespace iohome
{
    /// @brief Create an identify IO Frame (0x1E) — makes the device physically identify itself (e.g., brief jog movement)
    /// @param frame Output IoFrame structure
    /// @param own_node_id Source node ID (3 bytes)
    /// @param dst_node_id Destination node ID (3 bytes) for output frame to create
    /// @return true on success
    bool create_identify_request(IoFrame &frame, const uint8_t *own_node_id, const uint8_t *dst_node_id);

    /// @brief Create an execute IO Frame (0x00)
    /// @param frame Output IoFrame structure
    /// @param own_node_id Source node ID (3 bytes)
    /// @param dst_node_id Destination node ID (3 bytes) for output frame to create
    /// @param is_low_power 'Low power' flag to set
    /// @param position Position to reach
    /// @param quiet Quiet mode active if true
    /// @return true on success
    bool create_execute_request(IoFrame &frame, const uint8_t *own_node_id, const uint8_t *dst_node_id, bool is_low_power, uint8_t position, bool quiet = false);

    /// @brief Create an execute tilt IO Frame (0x00) - Sets tilt position without changing cover position
    /// @param frame Output IoFrame structure
    /// @param own_node_id Source node ID (3 bytes)
    /// @param dst_node_id Destination node ID (3 bytes) for output frame to create
    /// @param is_low_power 'Low power' flag to set
    /// @param tilt_percent Tilt percentage (0 = closed, 100 = fully open)
    /// @return true on success
    bool create_execute_tilt_request(IoFrame &frame, const uint8_t *own_node_id, const uint8_t *dst_node_id, bool is_low_power, uint8_t tilt_percent);

    /// @brief Create a GetStatus IO Frame (0x03)
    /// @param frame Output IoFrame structure
    /// @param own_node_id Source node ID (3 bytes)
    /// @param dst_node_id Destination node ID (3 bytes) for output frame to create
    /// @return true on success
    bool create_getstatus03_request(IoFrame &frame, const uint8_t *own_node_id, const uint8_t *dst_node_id);

    /// @brief Create a GetStatus IO Frame (0x03) with tilt info request
    /// @param frame Output IoFrame structure
    /// @param own_node_id Source node ID (3 bytes)
    /// @param dst_node_id Destination node ID (3 bytes) for output frame to create
    /// @return true on success
    bool create_getstatus03_tilt_request(IoFrame &frame, const uint8_t *own_node_id, const uint8_t *dst_node_id);

    /// @brief Create a Discovery IO Frame (0x28)
    /// @param frame Output IoFrame structure
    /// @param own_node_id Source node ID (3 bytes)
    /// @return true on success
    bool create_discovery_request(IoFrame &frame, const uint8_t *own_node_id);

    /// @brief Process a Discovery Response IO Frame (0x29) to fill an IoDevice structure
    /// @param frame Input IoFrame structure
    /// @param device The structure to fill from IO Frame
    /// @return true on success
    bool process_discovery_response(const IoFrame &frame, IoDevice &device);

    /// @brief Create a Discovery SPE IO Frame (0x2A)
    /// @param frame Output IoFrame structure
    /// @param own_node_id Source node ID (3 bytes)
    /// @param system_key
    /// @return true on success
    bool create_discoveryspe_request(IoFrame &frame, const uint8_t *own_node_id, const uint8_t system_key[AES_KEY_SIZE]);

    /// @brief Process a Discovery SPE Response IO Frame (0x2B) to fill an IoDevice structure
    /// @param frame Input IoFrame structure
    /// @param device The structure to fill from IO Frame
    /// @return true on success
    bool process_discoveryspe_response(const IoFrame &frame, IoDevice &device);

    /// @brief Create a Discovery Confirmation IO Frame (0x2C)
    /// @param frame Output IoFrame structure
    /// @param own_node_id Source node ID (3 bytes)
    /// @param dst_node_id Destination node ID (3 bytes) for output frame to create
    /// @return true on success
    bool create_discovery_confirmation_request(IoFrame &frame, const uint8_t *own_node_id, const uint8_t *dst_node_id);

    /// @brief Create a Discovery '2E' IO Frame (0x2E)
    /// @param frame Output IoFrame structure
    /// @param own_node_id Source node ID (3 bytes)
    /// @return true on success
    bool create_discovery2E_request(IoFrame &frame, const uint8_t *own_node_id);

    /// @brief Create an InitTransfer IO Frame (0x31)
    /// @param frame Output IoFrame structure
    /// @param own_node_id Source node ID (3 bytes)
    /// @param dst_node_id Destination node ID (3 bytes) for output frame to create
    /// @return true on success
    bool create_init_transfer(IoFrame &frame, const uint8_t *own_node_id, const uint8_t *dst_node_id);

    /// @brief Create a KeyTransfer IO Frame (0x32)
    /// @param frame Output IoFrame structure
    /// @param old_frame The previous frame sent to device before receiving challenge (probably an InitTransfert)
    /// @param dest_node Destination node ID (3 bytes) for output frame to create
    /// @param src_node Source node ID (3 bytes)
    /// @param system_key The system key (16 bytes) to encode in the frame
    /// @param challenge The challenge to use to encode the system key
    /// @return true on success
    bool create_key_transfer(
        IoFrame &frame,
        IoFrame &old_frame,
        const uint8_t dest_node[NODE_ID_SIZE],
        const uint8_t src_node[NODE_ID_SIZE],
        const uint8_t system_key[AES_KEY_SIZE],
        const uint8_t challenge[HMAC_SIZE]);

    /// @brief Create challenge request frame (0x3C)
    /// @param frame Output IoFrame structure
    /// @param dest_node Destination node ID (3 bytes)
    /// @param src_node Source node ID (3 bytes)
    /// @return true on success
    bool create_challenge_request(
        IoFrame &frame,
        const uint8_t dest_node[NODE_ID_SIZE],
        const uint8_t src_node[NODE_ID_SIZE]);

    /// @brief Create challenge response frame (0x3D)
    /// @param output_frame Output IoFrame structure
    /// @param dest_node Destination node ID (3 bytes) for output frame to create
    /// @param src_node Source node ID (3 bytes) for output frame to create
    /// @param received_challenge Challenge from request 0x3C (6 bytes)
    /// @param origin_frame Frame that requested the authentication
    /// @return true on success
    bool create_challenge_response(
        IoFrame &outframe,
        const uint8_t dest_node[NODE_ID_SIZE],
        const uint8_t src_node[NODE_ID_SIZE],
        const uint8_t received_challenge[HMAC_SIZE],
        const IoFrame &origin_frame,
        const uint8_t *key);

    /// @brief Create a GetName IO Frame (0x50)
    /// @param frame Output IoFrame structure
    /// @param own_node_id Source node ID (3 bytes)
    /// @param dst_node_id Destination node ID (3 bytes) for output frame to create
    /// @return true on success
    bool create_getname_request(IoFrame &frame, const uint8_t *own_node_id, const uint8_t *dst_node_id);

    /// @brief Create a SetName IO Frame (0x52)
    /// @param frame Output IoFrame structure
    /// @param own_node_id Source node ID (3 bytes)
    /// @param dst_node_id Destination node ID (3 bytes) for output frame to create
    /// @param name Name to set (up to 15 chars)
    /// @param name_length Name length (in bytes)
    /// @return true on success
    bool create_setname_request(IoFrame &frame, const uint8_t *own_node_id, const uint8_t *dst_node_id, const char *name, uint8_t name_length);

    /// @brief Create a GetInfo1 IO Frame (0x54)
    /// @param frame Output IoFrame structure
    /// @param own_node_id Source node ID (3 bytes)
    /// @param dst_node_id Destination node ID (3 bytes) for output frame to create
    /// @return true on success
    bool create_getinfo1_request(IoFrame &frame, const uint8_t *own_node_id, const uint8_t *dst_node_id);

    /// @brief Create a GetInfo2 IO Frame (0x56)
    /// @param frame Output IoFrame structure
    /// @param own_node_id Source node ID (3 bytes)
    /// @param dst_node_id Destination node ID (3 bytes) for output frame to create
    /// @return true on success
    bool create_getinfo2_request(IoFrame &frame, const uint8_t *own_node_id, const uint8_t *dst_node_id);

    /// @brief Create a GetInfo3 IO Frame (0x58)
    /// @param frame Output IoFrame structure
    /// @param own_node_id Source node ID (3 bytes)
    /// @param dst_node_id Destination node ID (3 bytes) for output frame to create
    /// @return true on success
    bool create_getinfo3_request(IoFrame &frame, const uint8_t *own_node_id, const uint8_t *dst_node_id);

    /// @brief Create a battery query IO Frame (0x03) for function IDs 0x06 (battery-status) or 0x09 (battery-state)
    /// @param frame Output IoFrame structure
    /// @param own_node_id Source node ID (3 bytes)
    /// @param dst_node_id Destination node ID (3 bytes)
    /// @param function_id Private function ID: 0x06 = battery-status, 0x09 = battery-state
    /// @return true on success
    bool create_getbattery_request(IoFrame &frame, const uint8_t *own_node_id, const uint8_t *dst_node_id, uint8_t function_id);

    /// @brief Create a status update response IO Frame (0x72)
    /// @param frame Output IoFrame structure
    /// @param own_node_id Source node ID (3 bytes)
    /// @param dst_node_id Destination node ID (3 bytes) for output frame to create
    /// @return true on success
    bool create_status_update_response(IoFrame &frame, const uint8_t *own_node_id, const uint8_t *dst_node_id);

    /// @brief Create an error response IO Frame (0xFE)
    /// @param frame Output IoFrame structure
    /// @param own_node_id Source node ID (3 bytes)
    /// @param dst_node_id Destination node ID (3 bytes) for output frame to create
    /// @param error_code Error code to send
    /// @return true on success
    bool create_error_response(IoFrame &frame, const uint8_t *own_node_id, const uint8_t *dst_node_id, uint8_t error_code);

    /// @brief Create a SET_CONFIG1 IO Frame (0x6F)
    /// @param frame Output IoFrame structure
    /// @param own_node_id Source node ID (3 bytes)
    /// @param dst_node_id Destination node ID (3 bytes) for output frame to create
    /// @return true on success
    bool create_set_config1_command(IoFrame &frame, const uint8_t *own_node_id, const uint8_t *dst_node_id);
} // namespace iohome