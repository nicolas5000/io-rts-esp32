#pragma once

#include <stdint.h>
#include <stddef.h>
#include <string>
#include "iohome_constants.h"
#include "iohome_frame.hpp"

namespace iohome
{
    constexpr float UNKNOWN_POSITION = 212.0;
    constexpr float SWITCH_LIGHT_ON_POSITION = 0.0;
    constexpr float SWITCH_LIGHT_OFF_POSITION = 100.0;

    struct IoDeviceInformation
    {
        uint8_t node_id[NODE_ID_SIZE];       // Device node ID
        char name[CMD_PARAM_NAME_MAXSIZE];   // Device Name
        char info1[CMD_PARAM_INFO1_MAXSIZE]; // Device INFO1
        char info2[CMD_PARAM_INFO2_MAXSIZE]; // Device INFO2
        Manufacturer manufacturer;           // Device manufacturer
        DeviceType device_type;              // Device type
        uint8_t device_subtype;              // Device sub-type
        bool is_openclose_inverted;          // Device OPEN/CLOSE is inverted (0 for CLOSED, 100 for OPEN)
    };

    struct IoDevice
    {
        IoDeviceInformation info;             // Device static information (no changes during use)
        float position;                       // Position between 0.0 and 100.0 or UNKNOWN_POSITION if unknown. 0 is open / 0% closed / light on / switch on, 100 is 100% closed / light off / switch off.
        float target;                         // Position between 0.0 and 100.0 or UNKNOWN_POSITION if unknown. 0 is open / 0% closed / light on / switch on, 100 is 100% closed / light off / switch off.
        float tilt;                           // Tilt between 0.0 (closed) and 100.0 (fully open) or UNKNOWN_POSITION if unknown/unsupported.
        bool is_stopped;                      // true if stopped, false if moving.
        bool is_deleted;                      // true if device has been deleted (will not be created at next boot)
        int64_t last_status_timestamp;        // Timestamp of the last received status, in us (use esp_timer_get_time() to fill and compare to local date&time!)
        int64_t next_status_update_timestamp; // Timestamp of the next planned status update, in us (use esp_timer_get_time() to fill and compare to local date&time!)
    };

    /// @brief Check if a device type supports tilt control
    /// @param type Device type
    /// @return true if the device type supports tilt
    bool deviceTypeSupportsTilt(DeviceType type);

    /// @brief Check if target and current positions are close enough to consider the device as having reached its target
    /// @param targetPos Target position (raw value, 0 to CMD_PARAM_STATUS_POS_MAX)
    /// @param currentPos Current position (raw value, 0 to CMD_PARAM_STATUS_POS_MAX)
    /// @return true if both positions are valid and within tolerance
    bool hasReachedTargetPosition(uint16_t targetPos, uint16_t currentPos);

    std::string IoDeviceManufacturer(Manufacturer mf);
    
} // namespace iohome