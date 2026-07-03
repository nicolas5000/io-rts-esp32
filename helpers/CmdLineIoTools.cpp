#include "CmdLineManagement.hpp"
#include "IoHomeConfig.hpp"

#include <algorithm>
#include <vector>
#include "argtable3/argtable3.h"
#include "esp_console.h"
#include "esp_log.h"
#include <esp_timer.h>

using namespace Config;

static const char *TAG = "cmdline_mngt";
static IoRts::IoRtsManager *sIoRtsManager;
static iohome::IoHomeControl *sIoHome;

// ******************* IO DISCOVER ********************

static int do_iodiscover_cmd(int argc, char **argv)
{
    auto result = sIoHome->DiscoverAndPairDevice();
    if (result == iohome::PairResult::FAILED_KEY_MISMATCH)
        ESP_LOGW(TAG, "Discover: device has a different key — factory reset required");
    else if (result == iohome::PairResult::FAILED_NO_RESPONSE)
        ESP_LOGW(TAG, "Discover failed — no device responded");
    return 0;
}

static void register_iodiscover(void)
{
    const esp_console_cmd_t iodiscover_cmd = {
        .command = "io_discover",
        .help = "Try to find and pair an IO-HomeControl device in pairing mode and never registered",
        .hint = NULL,
        .func = &do_iodiscover_cmd,
        .argtable = NULL,
        .func_w_context = NULL,
        .context = NULL};
    ESP_ERROR_CHECK(esp_console_cmd_register(&iodiscover_cmd));
}

// ******************* IO ADD ********************

/// @brief Structure used by the 'io_add' command
static struct
{
    struct arg_str *device_id;
    struct arg_end *end;
} ioadd_args;

static int do_ioadd_cmd(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&ioadd_args);
    if (nerrors != 0)
    {
        arg_print_errors(stderr, ioadd_args.end, argv[0]);
        return 1;
    }
    std::string deviceID(ioadd_args.device_id->sval[0]);
    std::transform(deviceID.begin(), deviceID.end(), deviceID.begin(), [](unsigned char c)
                   { return std::toupper(c); }); // convert to uppercase
    sIoHome->AddDevice(deviceID);
    return 0;
}

void register_ioadd(void)
{
    ioadd_args.device_id = arg_str1(NULL, NULL, "<deviceid>", "ID of the device, 3 bytes (eg 112233)");
    ioadd_args.end = arg_end(1);

    const esp_console_cmd_t ioadd_cmd = {
        .command = "io_add",
        .help = "Add an already registered IO-HomeControl device",
        .hint = NULL,
        .func = &do_ioadd_cmd,
        .argtable = &ioadd_args,
        .func_w_context = NULL,
        .context = NULL};

    ESP_ERROR_CHECK(esp_console_cmd_register(&ioadd_cmd));
}

// ******************* IO REMOVE ********************

/// @brief Structure used by the 'io_remove' command
static struct
{
    struct arg_str *device_id;
    struct arg_end *end;
} ioremove_args;

static int do_ioremove_cmd(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&ioremove_args);
    if (nerrors != 0)
    {
        arg_print_errors(stderr, ioremove_args.end, argv[0]);
        return 1;
    }
    std::string deviceID(ioremove_args.device_id->sval[0]);
    std::transform(deviceID.begin(), deviceID.end(), deviceID.begin(), [](unsigned char c)
                   { return std::toupper(c); }); // convert to uppercase
    sIoRtsManager->RemoveIoDevice(deviceID);
    return 0;
}

void register_ioremove(void)
{
    ioremove_args.device_id = arg_str1(NULL, NULL, "<deviceid>", "ID of the device, 3 bytes (eg 112233)");
    ioremove_args.end = arg_end(1);

    const esp_console_cmd_t ioremove_cmd = {
        .command = "io_remove",
        .help = "Remove an already added IO-HomeControl device",
        .hint = NULL,
        .func = &do_ioremove_cmd,
        .argtable = &ioremove_args,
        .func_w_context = NULL,
        .context = NULL};

    ESP_ERROR_CHECK(esp_console_cmd_register(&ioremove_cmd));
}

// ******************* IO DEACTIVATE ********************

static struct
{
    struct arg_str *device_id;
    struct arg_end *end;
} iodeactivate_args;

static int do_iodeactivate_cmd(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&iodeactivate_args);
    if (nerrors != 0)
    {
        arg_print_errors(stderr, iodeactivate_args.end, argv[0]);
        return 1;
    }
    std::string deviceID(iodeactivate_args.device_id->sval[0]);
    std::transform(deviceID.begin(), deviceID.end(), deviceID.begin(), [](unsigned char c)
                   { return std::toupper(c); });
    sIoRtsManager->DeactivateDevice(deviceID);
    return 0;
}

static void register_iodeactivate(void)
{
    iodeactivate_args.device_id = arg_str1(NULL, NULL, "<deviceid>", "ID of the device (3 bytes, eg 112233)");
    iodeactivate_args.end = arg_end(1);

    const esp_console_cmd_t cmd = {
        .command = "io_deactivate",
        .help = "Deactivate a device: marks it inactive, stops radio monitoring. Reversible with io_reactivate.",
        .hint = NULL,
        .func = &do_iodeactivate_cmd,
        .argtable = &iodeactivate_args,
        .func_w_context = NULL,
        .context = NULL};
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

// ******************* IO REACTIVATE ********************

static struct
{
    struct arg_str *device_id;
    struct arg_end *end;
} ioreactivate_args;

static int do_ioreactivate_cmd(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&ioreactivate_args);
    if (nerrors != 0)
    {
        arg_print_errors(stderr, ioreactivate_args.end, argv[0]);
        return 1;
    }
    std::string deviceID(ioreactivate_args.device_id->sval[0]);
    std::transform(deviceID.begin(), deviceID.end(), deviceID.begin(), [](unsigned char c)
                   { return std::toupper(c); });
    sIoRtsManager->ReactivateDevice(deviceID);
    return 0;
}

static void register_ioreactivate(void)
{
    ioreactivate_args.device_id = arg_str1(NULL, NULL, "<deviceid>", "ID of the device (3 bytes, eg 112233)");
    ioreactivate_args.end = arg_end(1);

    const esp_console_cmd_t cmd = {
        .command = "io_reactivate",
        .help = "Re-activate a previously deactivated device: restores radio monitoring.",
        .hint = NULL,
        .func = &do_ioreactivate_cmd,
        .argtable = &ioreactivate_args,
        .func_w_context = NULL,
        .context = NULL};
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

// ******************* IO DELETE ********************

static struct
{
    struct arg_str *device_id;
    struct arg_end *end;
} iodelete_args;

static int do_iodelete_cmd(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&iodelete_args);
    if (nerrors != 0)
    {
        arg_print_errors(stderr, iodelete_args.end, argv[0]);
        return 1;
    }
    std::string deviceID(iodelete_args.device_id->sval[0]);
    std::transform(deviceID.begin(), deviceID.end(), deviceID.begin(), [](unsigned char c)
                   { return std::toupper(c); });
    if (!sIoRtsManager->DeleteDevice(deviceID))
        ESP_LOGE(TAG, "io_delete failed — device must be deactivated first (use io_deactivate)");
    return 0;
}

static void register_iodelete(void)
{
    iodelete_args.device_id = arg_str1(NULL, NULL, "<deviceid>", "ID of the device (3 bytes, eg 112233)");
    iodelete_args.end = arg_end(1);

    const esp_console_cmd_t cmd = {
        .command = "io_delete",
        .help = "Permanently delete an inactive device. Device must be deactivated first. IRREVERSIBLE.",
        .hint = NULL,
        .func = &do_iodelete_cmd,
        .argtable = &iodelete_args,
        .func_w_context = NULL,
        .context = NULL};
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

// ******************* IO DELETE INACTIVE ********************

static struct
{
    struct arg_lit *confirm;
    struct arg_end *end;
} iodeleteinactive_args;

static int do_iodeleteinactive_cmd(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&iodeleteinactive_args);
    if (nerrors != 0)
    {
        arg_print_errors(stderr, iodeleteinactive_args.end, argv[0]);
        return 1;
    }

    // Collect inactive devices under lock
    std::vector<std::pair<std::string, std::string>> inactive; // {id, name}
    {
        std::lock_guard<std::mutex> guard(sIoRtsManager->mIoDevicesMutex);
        for (const auto &[id, dev] : sIoRtsManager->mIoDevices)
            if (dev.is_deleted)
                inactive.push_back({id, std::string(dev.info.name)});
    }

    if (inactive.empty())
    {
        ESP_LOGI(TAG, "No inactive devices.");
        return 0;
    }

    ESP_LOGI(TAG, "Inactive devices (%u):", (unsigned)inactive.size());
    for (const auto &[id, name] : inactive)
        ESP_LOGI(TAG, "  %s  (%s)", id.c_str(), name.c_str());

    if (iodeleteinactive_args.confirm->count == 0)
    {
        ESP_LOGW(TAG, "Add --confirm to permanently delete all %u inactive device(s) above.", (unsigned)inactive.size());
        return 0;
    }

    for (const auto &[id, name] : inactive)
    {
        if (sIoRtsManager->DeleteDevice(id))
            ESP_LOGI(TAG, "Deleted %s (%s)", id.c_str(), name.c_str());
        else
            ESP_LOGE(TAG, "Failed to delete %s", id.c_str());
    }
    return 0;
}

static void register_iodeleteinactive(void)
{
    iodeleteinactive_args.confirm = arg_lit0(NULL, "confirm", "Actually delete (without this flag the command only lists inactive devices)");
    iodeleteinactive_args.end = arg_end(1);

    const esp_console_cmd_t cmd = {
        .command = "io_delete_inactive",
        .help = "List inactive devices. Add --confirm to permanently delete all of them. IRREVERSIBLE.",
        .hint = NULL,
        .func = &do_iodeleteinactive_cmd,
        .argtable = &iodeleteinactive_args,
        .func_w_context = NULL,
        .context = NULL};
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

// ******************* IO OPEN ********************

/// @brief Structure used by the 'io_open' command
static struct
{
    struct arg_str *device_id;
    struct arg_end *end;
} ioopen_args;

static int do_ioopen_cmd(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&ioopen_args);
    if (nerrors != 0)
    {
        arg_print_errors(stderr, ioopen_args.end, argv[0]);
        return 1;
    }
    sIoHome->OpenDevice(ioopen_args.device_id->sval[0]);
    return 0;
}

void register_ioopen(void)
{
    ioopen_args.device_id = arg_str1(NULL, NULL, "<deviceid>", "ID of the device, 3 bytes (eg 112233)");
    ioopen_args.end = arg_end(1);

    const esp_console_cmd_t ioopen_cmd = {
        .command = "io_open",
        .help = "Open (or set to 'On') an IO-HomeControl device",
        .hint = NULL,
        .func = &do_ioopen_cmd,
        .argtable = &ioopen_args,
        .func_w_context = NULL,
        .context = NULL};

    ESP_ERROR_CHECK(esp_console_cmd_register(&ioopen_cmd));
}

// ******************* IO CLOSE ********************

/// @brief Structure used by the 'io_close' command
static struct
{
    struct arg_str *device_id;
    struct arg_end *end;
} ioclose_args;

static int do_ioclose_cmd(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&ioclose_args);
    if (nerrors != 0)
    {
        arg_print_errors(stderr, ioclose_args.end, argv[0]);
        return 1;
    }
    sIoHome->CloseDevice(ioclose_args.device_id->sval[0]);
    return 0;
}

void register_ioclose(void)
{
    ioclose_args.device_id = arg_str1(NULL, NULL, "<deviceid>", "ID of the device, 3 bytes (eg 112233)");
    ioclose_args.end = arg_end(1);

    const esp_console_cmd_t ioclose_cmd = {
        .command = "io_close",
        .help = "Close (or set to 'Off') an IO-HomeControl device",
        .hint = NULL,
        .func = &do_ioclose_cmd,
        .argtable = &ioclose_args,
        .func_w_context = NULL,
        .context = NULL};

    ESP_ERROR_CHECK(esp_console_cmd_register(&ioclose_cmd));
}

// ******************* IO STOP ********************

/// @brief Structure used by the 'io_stop' command
static struct
{
    struct arg_str *device_id;
    struct arg_end *end;
} iostop_args;

static int do_iostop_cmd(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&iostop_args);
    if (nerrors != 0)
    {
        arg_print_errors(stderr, iostop_args.end, argv[0]);
        return 1;
    }
    sIoHome->StopDevice(iostop_args.device_id->sval[0]);
    return 0;
}

void register_iostop(void)
{
    iostop_args.device_id = arg_str1(NULL, NULL, "<deviceid>", "ID of the device, 3 bytes (eg 112233)");
    iostop_args.end = arg_end(1);

    const esp_console_cmd_t iostop_cmd = {
        .command = "io_stop",
        .help = "Stop a currently moving IO-HomeControl device",
        .hint = NULL,
        .func = &do_iostop_cmd,
        .argtable = &iostop_args,
        .func_w_context = NULL,
        .context = NULL};

    ESP_ERROR_CHECK(esp_console_cmd_register(&iostop_cmd));
}

// ******************* IO FAVORITE POSITION ********************

/// @brief Structure used by the 'io_setfavpos' command
static struct
{
    struct arg_str *device_id;
    struct arg_end *end;
} iosetfavpos_args;

static int do_iosetfavpos_cmd(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&iosetfavpos_args);
    if (nerrors != 0)
    {
        arg_print_errors(stderr, iosetfavpos_args.end, argv[0]);
        return 1;
    }
    sIoHome->SetDeviceToFavoritePosition(iosetfavpos_args.device_id->sval[0]);
    return 0;
}

void register_iosetfavpos(void)
{
    iosetfavpos_args.device_id = arg_str1(NULL, NULL, "<deviceid>", "ID of the device, 3 bytes (eg 112233)");
    iosetfavpos_args.end = arg_end(1);

    const esp_console_cmd_t iosetfavpos_cmd = {
        .command = "io_setfavpos",
        .help = "Set an IO-HomeControl device to favorite position (like 'My' button)",
        .hint = NULL,
        .func = &do_iosetfavpos_cmd,
        .argtable = &iosetfavpos_args,
        .func_w_context = NULL,
        .context = NULL};

    ESP_ERROR_CHECK(esp_console_cmd_register(&iosetfavpos_cmd));
}

// ******************* IO SET POSITION ********************

/// @brief Structure used by the 'io_setpos' command
static struct
{
    struct arg_str *device_id;
    struct arg_int *position;
    struct arg_end *end;
} iosetpos_args;

static int do_iosetpos_cmd(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&iosetpos_args);
    if (nerrors != 0)
    {
        arg_print_errors(stderr, iosetpos_args.end, argv[0]);
        return 1;
    }
    if (iosetpos_args.position->ival[0] >= 0 && iosetpos_args.position->ival[0] <= 100)
    {
        sIoHome->SetDevicePosition(iosetpos_args.device_id->sval[0], iosetpos_args.position->ival[0]);
    }
    else
        ESP_LOGE(TAG, "Invalid value for <position>");
    return 0;
}

void register_iosetpos(void)
{
    iosetpos_args.device_id = arg_str1(NULL, NULL, "<deviceid>", "ID of the device, 3 bytes (eg 112233)");
    iosetpos_args.position = arg_int1(NULL, NULL, "<position>", "Specify the position to reach (0 = OPEN to 100 = CLOSED)");
    iosetpos_args.end = arg_end(2);

    const esp_console_cmd_t iosetpos_cmd = {
        .command = "io_setpos",
        .help = "Set an IO-HomeControl device to a specified position",
        .hint = NULL,
        .func = &do_iosetpos_cmd,
        .argtable = &iosetpos_args,
        .func_w_context = NULL,
        .context = NULL};

    ESP_ERROR_CHECK(esp_console_cmd_register(&iosetpos_cmd));
}

// ******************* IO FORCE STATUS UPDATE ********************

/// @brief Structure used by the 'io_update' command
static struct
{
    struct arg_str *device_id;
    struct arg_end *end;
} ioupdate_args;

static int do_ioupdate_cmd(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&ioupdate_args);
    if (nerrors != 0)
    {
        arg_print_errors(stderr, ioupdate_args.end, argv[0]);
        return 1;
    }
    sIoHome->ForceDeviceStatusUpdate(ioupdate_args.device_id->sval[0]);
    return 0;
}

void register_ioupdate(void)
{
    ioupdate_args.device_id = arg_str1(NULL, NULL, "<deviceid>", "ID of the device, 3 bytes (eg 112233)");
    ioupdate_args.end = arg_end(1);

    const esp_console_cmd_t ioupdate_cmd = {
        .command = "io_update",
        .help = "Force status update for a given IO-HomeControl device",
        .hint = NULL,
        .func = &do_ioupdate_cmd,
        .argtable = &ioupdate_args,
        .func_w_context = NULL,
        .context = NULL};

    ESP_ERROR_CHECK(esp_console_cmd_register(&ioupdate_cmd));
}

// ******************* IO SET NAME ********************

/// @brief Structure used by the 'io_setname' command
static struct
{
    struct arg_str *device_id;
    struct arg_str *device_name;
    struct arg_end *end;
} iosetname_args;

static int do_iosetname_cmd(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&iosetname_args);
    if (nerrors != 0)
    {
        arg_print_errors(stderr, iosetname_args.end, argv[0]);
        return 1;
    }
    sIoHome->SetDeviceName(iosetname_args.device_id->sval[0], iosetname_args.device_name->sval[0]);
    return 0;
}

void register_iosetname(void)
{
    iosetname_args.device_id = arg_str1(NULL, NULL, "<deviceid>", "ID of the device, 3 bytes (eg 112233)");
    iosetname_args.device_name = arg_str1(NULL, NULL, "<name>", "Name of the device, 1 to 15 characters");
    iosetname_args.end = arg_end(2);

    const esp_console_cmd_t iosetname_cmd = {
        .command = "io_setname",
        .help = "Set a new name inside IO-HomeControl device configuration",
        .hint = NULL,
        .func = &do_iosetname_cmd,
        .argtable = &iosetname_args,
        .func_w_context = NULL,
        .context = NULL};

    ESP_ERROR_CHECK(esp_console_cmd_register(&iosetname_cmd));
}

// ******************* IO LINK REMOTE ********************

/// @brief Structure used by the 'io_linkremote' command
static struct
{
    struct arg_str *device_id;
    struct arg_str *remote_id;
    struct arg_end *end;
} iolinkremote_args;

static int do_iolinkremote_cmd(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&iolinkremote_args);
    if (nerrors != 0)
    {
        arg_print_errors(stderr, iolinkremote_args.end, argv[0]);
        return 1;
    }
    std::string remoteID(iolinkremote_args.remote_id->sval[0]);
    std::string deviceID(iolinkremote_args.device_id->sval[0]);
    std::transform(remoteID.begin(), remoteID.end(), remoteID.begin(), [](unsigned char c)
                   { return std::toupper(c); }); // convert to uppercase
    std::transform(deviceID.begin(), deviceID.end(), deviceID.begin(), [](unsigned char c)
                   { return std::toupper(c); }); // convert to uppercase
    sIoRtsManager->LinkRemoteToDevice(remoteID, deviceID);
    return 0;
}

void register_iolinkremote(void)
{
    iolinkremote_args.device_id = arg_str1(NULL, NULL, "<deviceid>", "ID of the device, 3 bytes (eg 112233)");
    iolinkremote_args.remote_id = arg_str1(NULL, NULL, "<remoteid>", "ID of the remote, 3 bytes (eg AABBCC)");
    iolinkremote_args.end = arg_end(2);

    const esp_console_cmd_t iolinkremote_cmd = {
        .command = "io_linkremote",
        .help = "Link a remote to a IO-HomeControl device",
        .hint = NULL,
        .func = &do_iolinkremote_cmd,
        .argtable = &iolinkremote_args,
        .func_w_context = NULL,
        .context = NULL};

    ESP_ERROR_CHECK(esp_console_cmd_register(&iolinkremote_cmd));
}

// ******************* IO REMOVE REMOTE ********************

/// @brief Structure used by the 'io_removeremote' command
static struct
{
    struct arg_str *remote_id;
    struct arg_end *end;
} ioremoveremote_args;

static int do_ioremoveremote_cmd(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&ioremoveremote_args);
    if (nerrors != 0)
    {
        arg_print_errors(stderr, ioremoveremote_args.end, argv[0]);
        return 1;
    }
    std::string remoteID(ioremoveremote_args.remote_id->sval[0]);
    std::transform(remoteID.begin(), remoteID.end(), remoteID.begin(), [](unsigned char c)
                   { return std::toupper(c); }); // convert to uppercase
    sIoRtsManager->RemoveIoRemote(remoteID);
    return 0;
}

void register_ioremoveremote(void)
{
    ioremoveremote_args.remote_id = arg_str1(NULL, NULL, "<remoteid>", "ID of the remote, 3 bytes (eg AABBCC)");
    ioremoveremote_args.end = arg_end(1);

    const esp_console_cmd_t ioremoveremote_cmd = {
        .command = "io_removeremote",
        .help = "Remove an IO remote",
        .hint = NULL,
        .func = &do_ioremoveremote_cmd,
        .argtable = &ioremoveremote_args,
        .func_w_context = NULL,
        .context = NULL};

    ESP_ERROR_CHECK(esp_console_cmd_register(&ioremoveremote_cmd));
}

// ******************* IO CONFIG DEVICE FEEDBACK ********************

/// @brief Structure used by the 'io_devicefeedback' command
static struct
{
    struct arg_str *device_id;
    struct arg_end *end;
} iodevicefeedback_args;

static int do_iodevicefeedback_cmd(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&iodevicefeedback_args);
    if (nerrors != 0)
    {
        arg_print_errors(stderr, iodevicefeedback_args.end, argv[0]);
        return 1;
    }
    sIoHome->ConfigureDeviceToSendStatus(iodevicefeedback_args.device_id->sval[0]);
    return 0;
}

void register_iodevicefeedback(void)
{
    iodevicefeedback_args.device_id = arg_str1(NULL, NULL, "<deviceid>", "ID of the device, 3 bytes (eg 112233)");
    iodevicefeedback_args.end = arg_end(1);

    const esp_console_cmd_t iodevicefeedback_cmd = {
        .command = "io_devicefeedback",
        .help = "Configure IO-HomeControl device to send its status automatically (not supported by all devices)",
        .hint = NULL,
        .func = &do_iodevicefeedback_cmd,
        .argtable = &iodevicefeedback_args,
        .func_w_context = NULL,
        .context = NULL};

    ESP_ERROR_CHECK(esp_console_cmd_register(&iodevicefeedback_cmd));
}

// ******************* IO IDENTIFY DEVICE ********************

/// @brief Structure used by the 'io_identify' command
static struct
{
    struct arg_str *device_id;
    struct arg_end *end;
} ioidentify_args;

static int do_ioidentify_cmd(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&ioidentify_args);
    if (nerrors != 0)
    {
        arg_print_errors(stderr, ioidentify_args.end, argv[0]);
        return 1;
    }
    sIoHome->IdentifyDevice(ioidentify_args.device_id->sval[0]);
    return 0;
}

void register_ioidentify(void)
{
    ioidentify_args.device_id = arg_str1(NULL, NULL, "<deviceid>", "ID of the device, 3 bytes (eg 112233)");
    ioidentify_args.end = arg_end(1);

    const esp_console_cmd_t ioidentify_cmd = {
        .command = "io_identify",
        .help = "Identify an IO-HomeControl device (device will physically identify itself, e.g., brief jog movement)",
        .hint = NULL,
        .func = &do_ioidentify_cmd,
        .argtable = &ioidentify_args,
        .func_w_context = NULL,
        .context = NULL};

    ESP_ERROR_CHECK(esp_console_cmd_register(&ioidentify_cmd));
}

// ******************* IO INVERT DEVICE ********************

/// @brief Structure used by the 'io_invertdevice' command
static struct
{
    struct arg_str *device_id;
    struct arg_end *end;
} ioinvertdevice_args;

static int do_ioinvertdevice_cmd(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&ioinvertdevice_args);
    if (nerrors != 0)
    {
        arg_print_errors(stderr, ioinvertdevice_args.end, argv[0]);
        return 1;
    }
    sIoHome->InvertOpenClosePositionForDevice(ioinvertdevice_args.device_id->sval[0]);
    return 0;
}

void register_ioinvertdevice(void)
{
    ioinvertdevice_args.device_id = arg_str1(NULL, NULL, "<deviceid>", "ID of the device, 3 bytes (eg 112233)");
    ioinvertdevice_args.end = arg_end(1);

    const esp_console_cmd_t ioinvertdevice_cmd = {
        .command = "io_invertdevice",
        .help = "Configure IO-HomeControl device to invert OPEN and CLOSE commands",
        .hint = NULL,
        .func = &do_ioinvertdevice_cmd,
        .argtable = &ioinvertdevice_args,
        .func_w_context = NULL,
        .context = NULL};

    ESP_ERROR_CHECK(esp_console_cmd_register(&ioinvertdevice_cmd));
}

// ******************* IO SEND RAW ********************

/// @brief Structure used by the 'io_sendraw' command
static struct
{
    struct arg_str *raw_frame;
    struct arg_int *frequency;
    struct arg_end *end;
} iosendraw_args;

static int do_iosendraw_cmd(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&iosendraw_args);
    if (nerrors != 0)
    {
        arg_print_errors(stderr, iosendraw_args.end, argv[0]);
        return 1;
    }
    sIoHome->SendRaw(iosendraw_args.raw_frame->sval[0], iosendraw_args.frequency->ival[0]);
    return 0;
}

void register_iosendraw(void)
{
    iosendraw_args.raw_frame = arg_str1(NULL, NULL, "<raw frame>", "String representation of an IO frame from CTRL0 to end of data (without CRC), 9 to 36 bytes.");
    iosendraw_args.frequency = arg_int1(NULL, NULL, "<frequency>", "frequency in Hz (868950000 to send on channel 2)");
    iosendraw_args.end = arg_end(2);

    const esp_console_cmd_t iosendraw_cmd = {
        .command = "io_sendraw",
        .help = "Send an IO frame from given string representation and frequency, waits for response if relevant (no 'end' flag), manages authentication.",
        .hint = NULL,
        .func = &do_iosendraw_cmd,
        .argtable = &iosendraw_args,
        .func_w_context = NULL,
        .context = NULL};

    ESP_ERROR_CHECK(esp_console_cmd_register(&iosendraw_cmd));
}

// ******************* IO GET BATTERY ********************

static struct
{
    struct arg_str *device_id;
    struct arg_end *end;
} iogetbattery_args;

static int do_iogetbattery_cmd(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&iogetbattery_args);
    if (nerrors != 0)
    {
        arg_print_errors(stderr, iogetbattery_args.end, argv[0]);
        return 1;
    }
    sIoHome->DeviceGetBattery(iogetbattery_args.device_id->sval[0]);
    return 0;
}

void register_iogetbattery(void)
{
    iogetbattery_args.device_id = arg_str1(NULL, NULL, "<deviceid>", "ID of the device, 3 bytes (eg 112233)");
    iogetbattery_args.end = arg_end(1);

    const esp_console_cmd_t cmd = {
        .command = "io_getbattery",
        .help = "Query battery-status (0x06) and battery-state (0x09) of an IO-HomeControl device",
        .hint = NULL,
        .func = &do_iogetbattery_cmd,
        .argtable = &iogetbattery_args,
        .func_w_context = NULL,
        .context = NULL};

    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

// ******************* IO LIST DEVICES ********************
static int do_iolistdevices_cmd(int argc, char **argv)
{
    std::lock_guard<std::mutex> guard(sIoRtsManager->mIoDevicesMutex);
    for (auto it = sIoRtsManager->mIoDevices.begin(); it != sIoRtsManager->mIoDevices.end(); ++it)
    {
        int32_t last_update = (esp_timer_get_time() - it->second.last_status_timestamp) / 1000000;        // seconds ago
        int32_t next_update = (it->second.next_status_update_timestamp - esp_timer_get_time()) / 1000000; // seconds in the future
        ESP_LOGI(TAG, "Device ID=%s, Name=%s, type=0x%02X, subtype=0x%02X, Manufacturer=%s, Deleted=%s, Inverted=%s, updated %ds ago, next update in %d seconds",
                 it->first.c_str(), it->second.info.name,
                 it->second.info.device_type, it->second.info.device_subtype, IoDeviceManufacturer(it->second.info.manufacturer).c_str(),
                 it->second.is_deleted ? "Yes" : "No", it->second.info.is_openclose_inverted ? "Yes" : "No",
                 last_update, next_update);
    }
    return 0;
}

void register_iolistdevices(void)
{
    const esp_console_cmd_t iolistdevices_cmd = {
        .command = "io_listdevices",
        .help = "List IO devices currently registered.",
        .hint = NULL,
        .func = &do_iolistdevices_cmd,
        .argtable = NULL,
        .func_w_context = NULL,
        .context = NULL};

    ESP_ERROR_CHECK(esp_console_cmd_register(&iolistdevices_cmd));
}

// ******************* IO CONFIG ********************

/// @brief Structure used by the 'io_config' command
static struct
{
    struct arg_lit *read;
    struct arg_lit *del;
    struct arg_int *logging_state;
    struct arg_int *passive_state;
    struct arg_str *io_system_key;
    struct arg_str *node_id;
    struct arg_int *tx_power;
    struct arg_int *ignore_auto_update;
    struct arg_end *end;
} ioconfig_args;

static int do_ioconfig_cmd(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&ioconfig_args);
    if (nerrors != 0)
    {
        arg_print_errors(stderr, ioconfig_args.end, argv[0]);
        return 1;
    }
    if (ioconfig_args.read->count > 0)
    {
        // Read IO layer configuration
        ESP_LOGI(TAG, "Logging status: %s", IoHomeConfig::isLoggingEnabled() ? "enabled" : "disabled");
        ESP_LOGI(TAG, "Passive mode: %s", IoHomeConfig::isPassiveModeEnabled() ? "enabled" : "disabled");
        ESP_LOGI(TAG, "System key: %s", IoHomeConfig::GetIoSystemKey().c_str());
        ESP_LOGI(TAG, "Our node ID: %s", IoHomeConfig::GetIoNodeId().c_str());
        ESP_LOGI(TAG, "Tx power: %d", IoHomeConfig::GetTxPower());
        ESP_LOGI(TAG, "Ignore auto-update: %s", IoHomeConfig::isIgnoreAutoUpdateEnabled() ? "enabled" : "disabled");
    }
    else if (ioconfig_args.del->count > 0)
    {
        IoHomeConfig::DeleteIoHomeConfig();
        ESP_LOGI(TAG, "IO Home configuration restored to default values. New configuration will be applied after reboot!");
    }
    else
    {
        esp_err_t err;
        // Set configuration
        if (ioconfig_args.logging_state->count > 0)
        {
            err = IoHomeConfig::ActivateLogging(ioconfig_args.logging_state->ival[0] != 0);
            if (err != ESP_OK)
            {
                ESP_LOGE(TAG, "Failed to set logging state to configuration storage! (%d)", err);
            }
            else
            {
                ESP_LOGI(TAG, "Logging state set to configuration storage: %s", IoHomeConfig::isLoggingEnabled() ? "enabled" : "disabled");
            }
        }
        if (ioconfig_args.passive_state->count > 0)
        {
            err = IoHomeConfig::ActivatePassiveMode(ioconfig_args.passive_state->ival[0] != 0);
            if (err != ESP_OK)
            {
                ESP_LOGE(TAG, "Failed to set passive mode to configuration storage! (%d)", err);
            }
            else
            {
                ESP_LOGI(TAG, "Passive mode set to configuration storage: %s", IoHomeConfig::isPassiveModeEnabled() ? "enabled" : "disabled");
            }
        }
        if (ioconfig_args.io_system_key->count > 0)
        {
            if (strlen(ioconfig_args.io_system_key->sval[0]) != 32)
            {
                ESP_LOGE(TAG, "Invalid value for system key, must be representation of 16 bytes! (provided %s)", ioconfig_args.io_system_key->sval[0]);
            }
            else
            {
                err = IoHomeConfig::SetIoSystemKey(ioconfig_args.io_system_key->sval[0]);
                if (err != ESP_OK)
                {
                    ESP_LOGE(TAG, "Failed to set system key to configuration storage! (%d)", err);
                }
                else
                {
                    ESP_LOGI(TAG, "System key set to configuration storage: %s", IoHomeConfig::GetIoSystemKey().c_str());
                }
            }
        }
        if (ioconfig_args.node_id->count > 0)
        {
            if (strlen(ioconfig_args.node_id->sval[0]) != 6)
            {
                ESP_LOGE(TAG, "Invalid value for node ID, must be representation of 3 bytes! (provided %s)", ioconfig_args.node_id->sval[0]);
            }
            else
            {
                err = IoHomeConfig::SetIoNodeId(ioconfig_args.node_id->sval[0]);
                if (err != ESP_OK)
                {
                    ESP_LOGE(TAG, "Failed to set node ID to configuration storage! (%d)", err);
                }
                else
                {
                    ESP_LOGI(TAG, "Node ID set to configuration storage: %s", IoHomeConfig::GetIoNodeId().c_str());
                }
            }
        }
        if (ioconfig_args.tx_power->count > 0)
        {
            if (ioconfig_args.tx_power->ival[0] < 0 || ioconfig_args.tx_power->ival[0] > 20)
            {
                ESP_LOGE(TAG, "Invalid value for Tx power, must be between 0 and 20! (provided %d)", ioconfig_args.tx_power->ival[0]);
            }
            else
            {
                err = IoHomeConfig::SetTxPower(ioconfig_args.tx_power->ival[0]);
                if (err != ESP_OK)
                {
                    ESP_LOGE(TAG, "Failed to set Tx power to configuration storage! (%d)", err);
                }
                else
                {
                    ESP_LOGI(TAG, "Tx power set to configuration storage: %d", IoHomeConfig::GetTxPower());
                }
            }
        }
        if (ioconfig_args.ignore_auto_update->count > 0)
        {
            err = IoHomeConfig::SetIgnoreAutoUpdate(ioconfig_args.ignore_auto_update->ival[0] != 0);
            if (err != ESP_OK)
            {
                ESP_LOGE(TAG, "Failed to set ignore auto-update to configuration storage! (%d)", err);
            }
            else
            {
                ESP_LOGI(TAG, "Ignore auto-update set to configuration storage: %s", IoHomeConfig::isIgnoreAutoUpdateEnabled() ? "enabled" : "disabled");
            }
        }
        ESP_LOGI(TAG, "New configuration will be applied after reboot!");
    }
    return 0;
}

void register_ioconfig(void)
{
    ioconfig_args.read = arg_lit0("r", "read", "Read current configuration from storage (no other argument required)");
    ioconfig_args.del = arg_lit0("d", "delete", "Delete current configuration in storage (no other argument required)");
    ioconfig_args.logging_state = arg_int0(NULL, "logging", "<state>", "1 to enable logging in IO HomeControl layer, 0 to disable");
    ioconfig_args.passive_state = arg_int0(NULL, "passive", "<state>", "1 to enable passive state in IO HomeControl layer, 0 to disable");
    ioconfig_args.io_system_key = arg_str0(NULL, "key", "<system key>", "IO System key (string representation of 16 bytes)");
    ioconfig_args.node_id = arg_str0(NULL, "id", "<node ID>", "Board Node ID (string representation of 3 bytes, eg 112233)");
    ioconfig_args.tx_power = arg_int0(NULL, "power", "<power>", "Tx power (range 0-20)");
    ioconfig_args.ignore_auto_update = arg_int0(NULL, "ignoreautoupdate", "<state>", "1 to ignore auto-update flag and use timer value, 0 to trust auto-update");
    ioconfig_args.end = arg_end(8);

    const esp_console_cmd_t ioconfig_cmd = {
        .command = "io_config",
        .help = "Configure IO layer",
        .hint = NULL,
        .func = &do_ioconfig_cmd,
        .argtable = &ioconfig_args,
        .func_w_context = NULL,
        .context = NULL};

    ESP_ERROR_CHECK(esp_console_cmd_register(&ioconfig_cmd));
}

// ******************* IO SET TILT ********************

/// @brief Structure used by the 'io_tilt' command
static struct
{
    struct arg_str *device_id;
    struct arg_int *tilt;
    struct arg_end *end;
} iotilt_args;

static int do_iotilt_cmd(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&iotilt_args);
    if (nerrors != 0)
    {
        arg_print_errors(stderr, iotilt_args.end, argv[0]);
        return 1;
    }
    if (iotilt_args.tilt->ival[0] >= 0 && iotilt_args.tilt->ival[0] <= 100)
    {
        sIoHome->SetDeviceTilt(iotilt_args.device_id->sval[0], iotilt_args.tilt->ival[0]);
    }
    else
        ESP_LOGE(TAG, "Invalid value for <tilt>");
    return 0;
}

void register_iotilt(void)
{
    iotilt_args.device_id = arg_str1(NULL, NULL, "<deviceid>", "ID of the device, 3 bytes (eg 112233)");
    iotilt_args.tilt = arg_int1(NULL, NULL, "<tilt>", "Specify the tilt (0 = CLOSED to 100 = OPEN)");
    iotilt_args.end = arg_end(2);

    const esp_console_cmd_t iotilt_cmd = {
        .command = "io_tilt",
        .help = "Set an IO-HomeControl device to a specified tilt angle",
        .hint = NULL,
        .func = &do_iotilt_cmd,
        .argtable = &iotilt_args,
        .func_w_context = NULL,
        .context = NULL};

    ESP_ERROR_CHECK(esp_console_cmd_register(&iotilt_cmd));
}

// ******************* IO Register commands ********************

void register_io_cmdline_tools(IoRts::IoRtsManager *io_rts_manager)
{
    sIoRtsManager = io_rts_manager;
    sIoHome = io_rts_manager->mIoHome;
    register_iodiscover();
    register_ioadd();
    register_ioremove();
    register_iodeactivate();
    register_ioreactivate();
    register_iodelete();
    register_iodeleteinactive();
    register_ioopen();
    register_ioclose();
    register_iostop();
    register_iosetfavpos();
    register_iosetpos();
    register_ioupdate();
    register_iosetname();
    register_iolinkremote();
    register_ioremoveremote();
    register_iodevicefeedback();
    register_ioidentify();
    register_ioinvertdevice();
    register_iosendraw();
    register_iogetbattery();
    register_iolistdevices();
    register_ioconfig();
    register_iotilt();
}
