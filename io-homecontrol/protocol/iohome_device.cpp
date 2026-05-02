#include "iohome_device.hpp"

namespace iohome
{
    bool deviceTypeSupportsTilt(DeviceType type)
    {
        switch (type)
        {
        case DeviceType::VENETIAN_BLIND:
        case DeviceType::EXTERNAL_VENETIAN_BLIND:
        case DeviceType::LOUVRE_BLIND:
        case DeviceType::BLIND:
            return true;
        default:
            return false;
        }
    }

    bool hasReachedTargetPosition(uint16_t targetPos, uint16_t currentPos)
    {
        if (targetPos > CMD_PARAM_STATUS_POS_MAX || currentPos > CMD_PARAM_STATUS_POS_MAX)
            return false;
        uint16_t diff = (targetPos > currentPos) ? (targetPos - currentPos) : (currentPos - targetPos);
        return diff <= CMD_PARAM_STATUS_POS_TOLERANCE;
    }
    std::string IoDeviceManufacturer(Manufacturer mf)
    {
        switch (mf)
        {
        case Manufacturer::VELUX:
            return "Velux";
        case Manufacturer::SOMFY:
            return "Somfy";
        case Manufacturer::HONEYWELL:
            return "Honeywell";
        case Manufacturer::HORMANN:
            return "Hormann";
        case Manufacturer::ASSA_ABLOY:
            return "Assa Abloy";
        case Manufacturer::NIKO:
            return "Niko";
        case Manufacturer::WINDOW_MASTER:
            return "Window Master";
        case Manufacturer::RENSON:
            return "Renson";
        case Manufacturer::CIAT:
            return "Ciat";
        case Manufacturer::SECUYOU:
            return "Secuyou";
        case Manufacturer::OVERKIZ:
            return "Overkiz";
        case Manufacturer::ATLANTIC_GROUP:
            return "Atlantic";
        case Manufacturer::UNKNOWN:
        default:
            return "unknown";
        }
    }
}