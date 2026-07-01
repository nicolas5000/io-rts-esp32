#include "oled_display.h"
#include "sdkconfig.h"

#if CONFIG_OLED_ENABLED

#include "esp_log.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_app_desc.h"

static const char *TAG = "oled";

#define I2C_PORT    I2C_NUM_0
#define OLED_COLS   128
#define OLED_PAGES  8
#define OLED_CHAR_W  6
#define MAX_CHARS   (OLED_COLS / OLED_CHAR_W)  /* 21 chars per row */

static i2c_master_bus_handle_t s_bus = NULL;
static i2c_master_dev_handle_t s_dev = NULL;

/* SSD1306 initialisation sequence for 128x64, page-addressing mode */
static const uint8_t INIT_CMDS[] = {
    0xAE,        /* display off */
    0xD5, 0x80,  /* clock div / oscillator */
    0xA8, 0x3F,  /* multiplex ratio: 64 rows */
    0xD3, 0x00,  /* display offset: 0 */
    0x40,        /* start line: 0 */
    0x8D, 0x14,  /* charge pump enable */
    0x20, 0x02,  /* memory addressing: page mode */
    0xA1,        /* segment remap: col 127 → SEG0 */
    0xC8,        /* COM scan direction: remapped */
    0xDA, 0x12,  /* COM pins hardware config */
    0x81, 0xCF,  /* contrast */
    0xD9, 0xF1,  /* pre-charge period */
    0xDB, 0x40,  /* VCOMH deselect level */
    0xA4,        /* output follows RAM */
    0xA6,        /* normal display (not inverted) */
    0xAF,        /* display on */
};

/* Standard 6×8 ASCII font, characters 0x20–0x7F.
   Each entry is 6 column bytes; bit 0 = top pixel, bit 7 = bottom pixel. */
static const uint8_t font6x8[96][OLED_CHAR_W] = {
    {0x00,0x00,0x00,0x00,0x00,0x00}, /* 0x20   */
    {0x00,0x00,0x5F,0x00,0x00,0x00}, /* 0x21 ! */
    {0x00,0x07,0x00,0x07,0x00,0x00}, /* 0x22 " */
    {0x14,0x7F,0x14,0x7F,0x14,0x00}, /* 0x23 # */
    {0x24,0x2A,0x7F,0x2A,0x12,0x00}, /* 0x24 $ */
    {0x23,0x13,0x08,0x64,0x62,0x00}, /* 0x25 % */
    {0x36,0x49,0x55,0x22,0x50,0x00}, /* 0x26 & */
    {0x00,0x05,0x03,0x00,0x00,0x00}, /* 0x27 ' */
    {0x00,0x1C,0x22,0x41,0x00,0x00}, /* 0x28 ( */
    {0x00,0x41,0x22,0x1C,0x00,0x00}, /* 0x29 ) */
    {0x08,0x2A,0x1C,0x2A,0x08,0x00}, /* 0x2A * */
    {0x08,0x08,0x3E,0x08,0x08,0x00}, /* 0x2B + */
    {0x00,0x50,0x30,0x00,0x00,0x00}, /* 0x2C , */
    {0x08,0x08,0x08,0x08,0x08,0x00}, /* 0x2D - */
    {0x00,0x60,0x60,0x00,0x00,0x00}, /* 0x2E . */
    {0x20,0x10,0x08,0x04,0x02,0x00}, /* 0x2F / */
    {0x3E,0x51,0x49,0x45,0x3E,0x00}, /* 0x30 0 */
    {0x00,0x42,0x7F,0x40,0x00,0x00}, /* 0x31 1 */
    {0x42,0x61,0x51,0x49,0x46,0x00}, /* 0x32 2 */
    {0x21,0x41,0x45,0x4B,0x31,0x00}, /* 0x33 3 */
    {0x18,0x14,0x12,0x7F,0x10,0x00}, /* 0x34 4 */
    {0x27,0x45,0x45,0x45,0x39,0x00}, /* 0x35 5 */
    {0x3C,0x4A,0x49,0x49,0x30,0x00}, /* 0x36 6 */
    {0x01,0x71,0x09,0x05,0x03,0x00}, /* 0x37 7 */
    {0x36,0x49,0x49,0x49,0x36,0x00}, /* 0x38 8 */
    {0x06,0x49,0x49,0x29,0x1E,0x00}, /* 0x39 9 */
    {0x00,0x36,0x36,0x00,0x00,0x00}, /* 0x3A : */
    {0x00,0x56,0x36,0x00,0x00,0x00}, /* 0x3B ; */
    {0x08,0x14,0x22,0x41,0x00,0x00}, /* 0x3C < */
    {0x14,0x14,0x14,0x14,0x14,0x00}, /* 0x3D = */
    {0x00,0x41,0x22,0x14,0x08,0x00}, /* 0x3E > */
    {0x02,0x01,0x51,0x09,0x06,0x00}, /* 0x3F ? */
    {0x32,0x49,0x79,0x41,0x3E,0x00}, /* 0x40 @ */
    {0x7E,0x11,0x11,0x11,0x7E,0x00}, /* 0x41 A */
    {0x7F,0x49,0x49,0x49,0x36,0x00}, /* 0x42 B */
    {0x3E,0x41,0x41,0x41,0x22,0x00}, /* 0x43 C */
    {0x7F,0x41,0x41,0x22,0x1C,0x00}, /* 0x44 D */
    {0x7F,0x49,0x49,0x49,0x41,0x00}, /* 0x45 E */
    {0x7F,0x09,0x09,0x01,0x01,0x00}, /* 0x46 F */
    {0x3E,0x41,0x41,0x49,0x7A,0x00}, /* 0x47 G */
    {0x7F,0x08,0x08,0x08,0x7F,0x00}, /* 0x48 H */
    {0x00,0x41,0x7F,0x41,0x00,0x00}, /* 0x49 I */
    {0x20,0x40,0x41,0x3F,0x01,0x00}, /* 0x4A J */
    {0x7F,0x08,0x14,0x22,0x41,0x00}, /* 0x4B K */
    {0x7F,0x40,0x40,0x40,0x40,0x00}, /* 0x4C L */
    {0x7F,0x02,0x04,0x02,0x7F,0x00}, /* 0x4D M */
    {0x7F,0x04,0x08,0x10,0x7F,0x00}, /* 0x4E N */
    {0x3E,0x41,0x41,0x41,0x3E,0x00}, /* 0x4F O */
    {0x7F,0x09,0x09,0x09,0x06,0x00}, /* 0x50 P */
    {0x3E,0x41,0x51,0x21,0x5E,0x00}, /* 0x51 Q */
    {0x7F,0x09,0x19,0x29,0x46,0x00}, /* 0x52 R */
    {0x46,0x49,0x49,0x49,0x31,0x00}, /* 0x53 S */
    {0x01,0x01,0x7F,0x01,0x01,0x00}, /* 0x54 T */
    {0x3F,0x40,0x40,0x40,0x3F,0x00}, /* 0x55 U */
    {0x1F,0x20,0x40,0x20,0x1F,0x00}, /* 0x56 V */
    {0x7F,0x20,0x18,0x20,0x7F,0x00}, /* 0x57 W */
    {0x63,0x14,0x08,0x14,0x63,0x00}, /* 0x58 X */
    {0x03,0x04,0x78,0x04,0x03,0x00}, /* 0x59 Y */
    {0x61,0x51,0x49,0x45,0x43,0x00}, /* 0x5A Z */
    {0x00,0x7F,0x41,0x41,0x00,0x00}, /* 0x5B [ */
    {0x02,0x04,0x08,0x10,0x20,0x00}, /* 0x5C \ */
    {0x00,0x41,0x41,0x7F,0x00,0x00}, /* 0x5D ] */
    {0x04,0x02,0x01,0x02,0x04,0x00}, /* 0x5E ^ */
    {0x40,0x40,0x40,0x40,0x40,0x00}, /* 0x5F _ */
    {0x00,0x01,0x02,0x04,0x00,0x00}, /* 0x60 ` */
    {0x20,0x54,0x54,0x54,0x78,0x00}, /* 0x61 a */
    {0x7F,0x48,0x44,0x44,0x38,0x00}, /* 0x62 b */
    {0x38,0x44,0x44,0x44,0x20,0x00}, /* 0x63 c */
    {0x38,0x44,0x44,0x48,0x7F,0x00}, /* 0x64 d */
    {0x38,0x54,0x54,0x54,0x18,0x00}, /* 0x65 e */
    {0x08,0x7E,0x09,0x01,0x02,0x00}, /* 0x66 f */
    {0x08,0x14,0x54,0x54,0x3C,0x00}, /* 0x67 g */
    {0x7F,0x08,0x04,0x04,0x78,0x00}, /* 0x68 h */
    {0x00,0x44,0x7D,0x40,0x00,0x00}, /* 0x69 i */
    {0x20,0x40,0x44,0x3D,0x00,0x00}, /* 0x6A j */
    {0x7F,0x10,0x28,0x44,0x00,0x00}, /* 0x6B k */
    {0x00,0x41,0x7F,0x40,0x00,0x00}, /* 0x6C l */
    {0x7C,0x04,0x18,0x04,0x78,0x00}, /* 0x6D m */
    {0x7C,0x08,0x04,0x04,0x78,0x00}, /* 0x6E n */
    {0x38,0x44,0x44,0x44,0x38,0x00}, /* 0x6F o */
    {0x7C,0x14,0x14,0x14,0x08,0x00}, /* 0x70 p */
    {0x08,0x14,0x14,0x18,0x7C,0x00}, /* 0x71 q */
    {0x7C,0x08,0x04,0x04,0x08,0x00}, /* 0x72 r */
    {0x48,0x54,0x54,0x54,0x20,0x00}, /* 0x73 s */
    {0x04,0x3F,0x44,0x40,0x20,0x00}, /* 0x74 t */
    {0x3C,0x40,0x40,0x20,0x7C,0x00}, /* 0x75 u */
    {0x1C,0x20,0x40,0x20,0x1C,0x00}, /* 0x76 v */
    {0x3C,0x40,0x30,0x40,0x3C,0x00}, /* 0x77 w */
    {0x44,0x28,0x10,0x28,0x44,0x00}, /* 0x78 x */
    {0x0C,0x50,0x50,0x50,0x3C,0x00}, /* 0x79 y */
    {0x44,0x64,0x54,0x4C,0x44,0x00}, /* 0x7A z */
    {0x00,0x08,0x36,0x41,0x00,0x00}, /* 0x7B { */
    {0x00,0x00,0x7F,0x00,0x00,0x00}, /* 0x7C | */
    {0x00,0x41,0x36,0x08,0x00,0x00}, /* 0x7D } */
    {0x08,0x08,0x2A,0x1C,0x08,0x00}, /* 0x7E ~ */
    {0xFF,0xFF,0xFF,0xFF,0xFF,0x00}, /* 0x7F DEL */
};

/* ---- Icon bitmaps for TX / RX direction (6 x 8 px, column-oriented, bit 0 = top) ---- */

static const uint8_t icon_tx[OLED_CHAR_W] = {
    0x20, 0x11, 0x09, 0x05, 0x01, 0x1F
};

static const uint8_t icon_rx[OLED_CHAR_W] = {
    0x7C, 0x40, 0x50, 0x48, 0x44, 0x02
};

/* ---- Device line state (combines TX+RX per device) ---- */

#define MAX_DEV_LINES 4

typedef struct {
    char device_id[7];
    char tx_cmd[5];
    char rx_cmd[3];
    int  rssi;
    bool has_tx;
    bool has_rx;
    bool rx_first;
    TickType_t last_update;
} oled_dev_line_t;

/* Row mapping for each device line slot */
static const uint8_t DEV_LINE_ROWS[MAX_DEV_LINES] = {2, 3, 4, 5};

/* ---- Queue / task types (internal) ---- */

#define OLED_QUEUE_DEPTH 8

typedef enum { OLED_EVT_TX, OLED_EVT_RX, OLED_EVT_STATUS } oled_evt_type_t;

typedef struct {
    oled_evt_type_t type;
    char device_id[7];
    char cmd_str[22];
    int  position_pct;
    int  rssi;
} oled_evt_t;

static StaticQueue_t  s_queue_buf;
static uint8_t        s_queue_storage[OLED_QUEUE_DEPTH * sizeof(oled_evt_t)];
static QueueHandle_t  s_queue;

static StaticTask_t   s_task_buf;
static StackType_t    s_task_stack[2048];

static oled_dev_line_t s_dev_lines[MAX_DEV_LINES];
static int s_next_slot = 0;

static TickType_t s_status_time = 0;
static char s_version_str[9] = "v0.00.00";


/* ---- Low-level I2C helpers (called only from oled_task or init) ---- */

static esp_err_t send_cmd(uint8_t cmd)
{
    uint8_t buf[2] = {0x00, cmd};
    return i2c_master_transmit(s_dev, buf, sizeof(buf), pdMS_TO_TICKS(10));
}

static esp_err_t send_data(const uint8_t *data, size_t len)
{
    uint8_t buf[OLED_COLS + 1];
    buf[0] = 0x40;
    memcpy(buf + 1, data, len);
    return i2c_master_transmit(s_dev, buf, len + 1, pdMS_TO_TICKS(50));
}

static void oled_print_line(uint8_t row, const char *text)
{
    if (row >= OLED_PAGES || s_dev == NULL) return;

    uint8_t line[OLED_COLS];
    memset(line, 0, sizeof(line));
    if (text) {
        for (size_t i = 0; i < MAX_CHARS && text[i]; i++) {
            uint8_t c = (uint8_t)text[i];
            if (c < 0x20 || c > 0x7F) c = 0x20;
            memcpy(&line[i * OLED_CHAR_W], font6x8[c - 0x20], OLED_CHAR_W);
        }
    }
    send_cmd(0xB0 | row);
    send_cmd(0x00);
    send_cmd(0x10);
    send_data(line, OLED_COLS);
}

static void oled_init_version(void)
{
    const esp_app_desc_t *desc = esp_app_get_description();
    int major = 0, minor = 0, patch = 0;
    const char *v = desc->version;
    if (v[0] == 'v' || v[0] == 'V') v++;
    sscanf(v, "%d.%d.%d", &major, &minor, &patch);
    snprintf(s_version_str, sizeof(s_version_str), "v%d.%d.%d", major, minor, patch);
}

static void oled_render_status_line(const char *msg)
{
    if (s_dev == NULL) return;

    uint8_t line[OLED_COLS];
    memset(line, 0, sizeof(line));

    if (msg && msg[0]) {
        size_t ver_chars = strlen(s_version_str);
        size_t max_chars = (OLED_COLS - (int)(ver_chars * OLED_CHAR_W)) / OLED_CHAR_W;
        size_t len = strlen(msg);
        if (len > max_chars) len = max_chars;
        for (size_t i = 0; i < len; i++) {
            uint8_t c = (uint8_t)msg[i];
            if (c < 0x20 || c > 0x7F) c = 0x20;
            memcpy(&line[i * OLED_CHAR_W], font6x8[c - 0x20], OLED_CHAR_W);
        }
    }

    size_t ver_len = strlen(s_version_str);
    int start = OLED_COLS - (int)(ver_len * OLED_CHAR_W);
    for (size_t i = 0; i < ver_len; i++) {
        uint8_t c = (uint8_t)s_version_str[i];
        if (c < 0x20 || c > 0x7F) c = 0x20;
        memcpy(&line[start + i * OLED_CHAR_W], font6x8[c - 0x20], OLED_CHAR_W);
    }

    send_cmd(0xB0 | 7);
    send_cmd(0x00);
    send_cmd(0x10);
    send_data(line, OLED_COLS);
}

/* ---- Draw a solid 1-pixel horizontal line across the full width at a given page and row ---- */

static void oled_draw_hline(uint8_t row, uint8_t pixel_row)
{
    if (row >= OLED_PAGES || pixel_row > 7 || s_dev == NULL) return;
    uint8_t line[OLED_COLS];
    memset(line, 1 << pixel_row, sizeof(line));
    send_cmd(0xB0 | row);
    send_cmd(0x00);
    send_cmd(0x10);
    send_data(line, OLED_COLS);
}

/* ---- RSSI bar graph (3 vertical bars, right-aligned at cols 120-127) ---- */

static const uint8_t RSSI_BARS[4][8] = {
    {0x80,0x80,0x00,0x80,0x80,0x00,0x80,0x80},
    {0xE0,0xE0,0x00,0x80,0x80,0x00,0x80,0x80},
    {0xE0,0xE0,0x00,0xF8,0xF8,0x00,0x80,0x80},
    {0xE0,0xE0,0x00,0xF8,0xF8,0x00,0xFE,0xFE},
};

/* ---- Network-status icons (8 x 8 px, column-oriented, bit 0 = top) ---- */

#ifdef CONFIG_CONNECTIVITY_CHOICE_ETH
/* RJ45 Ethernet connector */
static const uint8_t icon_lan_on[8] = {
    0x7F, 0x41, 0x1D, 0x3D, 0x3D, 0x1D, 0x41, 0x7F
};
#endif

/* Question-mark glyph for "no connectivity configured" */
static const uint8_t icon_lan_off[8] = {
    0x04, 0x22, 0x41, 0x01, 0x08, 0x12, 0x24, 0x00
};

static void oled_draw_rssi_bars(uint8_t line[OLED_COLS], int rssi)
{
    int level;
    if (rssi == 0)                level = 0;
    else if (rssi >= -65)         level = 3;
    else if (rssi >= -80)         level = 2;
    else if (rssi >= -95)         level = 1;
    else                          level = 0;
    memcpy(&line[OLED_COLS - 8], RSSI_BARS[level], 8);
}

/* ---- Header row: IO logo + "control" + right-aligned network icon ---- */

/* Sentinel passed to oled_draw_header to indicate "no credentials configured" */
#define RSSI_NO_CREDS 127

static void oled_draw_header(int state)
{
    uint8_t line[OLED_COLS];
    memset(line, 0, sizeof(line));
    static const uint8_t logo_rows[][2] = {
        {0xff, 0xf0}, {0x88, 0x10}, {0x88, 0x10}, {0x88, 0x10},
        {0x88, 0x10}, {0x88, 0x10}, {0x8f, 0xf0}
    };
    for (int col = 0; col < 12; col++) {
        uint8_t byte = 0;
        for (int row = 0; row < 7; row++) {
            uint16_t r = ((uint16_t)logo_rows[row][0] << 8) | logo_rows[row][1];
            if (r & (0x8000 >> col)) byte |= (1 << row);
        }
        line[col] = byte << 1;
    }
    int pos = 16;
    const char *title = "control";
    for (size_t i = 0; title[i]; i++) {
        uint8_t c = (uint8_t)title[i];
        if (c < 0x20 || c > 0x7F) c = 0x20;
        memcpy(&line[pos + i * OLED_CHAR_W], font6x8[c - 0x20], OLED_CHAR_W);
    }

#ifdef CONFIG_CONNECTIVITY_CHOICE_ETH
    memcpy(&line[OLED_COLS - 8], state ? icon_lan_on : icon_lan_off, 8);
#else
    if (state == RSSI_NO_CREDS)
        memcpy(&line[OLED_COLS - 8], icon_lan_off, 8);
    else
        oled_draw_rssi_bars(line, state);
#endif

    send_cmd(0xB0 | 0);
    send_cmd(0x00);
    send_cmd(0x10);
    send_data(line, OLED_COLS);
}

/* ---- Per-device line helpers ---- */

static int find_or_alloc_line(const char *device_id)
{
    if (!device_id) device_id = "";
    for (int i = 0; i < MAX_DEV_LINES; i++) {
        if (strcmp(s_dev_lines[i].device_id, device_id) == 0) return i;
    }
    for (int i = 0; i < MAX_DEV_LINES; i++) {
        if (s_dev_lines[i].device_id[0] == '\0') return i;
    }
    int slot = s_next_slot;
    s_next_slot = (s_next_slot + 1) % MAX_DEV_LINES;
    memset(&s_dev_lines[slot], 0, sizeof(oled_dev_line_t));
    return slot;
}

/* Layout per device line (128 px):
 *  0-35   device name (6 chars)
 *  36-47  gap
 *  48-53  first icon (TX or RX, 6 px)
 *  54-56  gap (3 px)
 *  57-80  first cmd text (4 or 2 chars)
 *  81-83  gap (3 px)
 *  84-89  second icon (RX or TX, 6 px)
 *  90-92  gap (3 px)
 *  93-104 second cmd text (2 or 4 chars)
 *  105-119 gap
 *  120-127 RSSI signal bars (8 px)
 *
 *  TX part always uses 48/57 when first, 84/93 when second.
 *  RX part always uses 84/93 when first, 48/57 when second.
 *  Order depends on which event (TX or RX) arrived first for that line.
 */

static void oled_render_dev_line(int slot)
{
    int row = DEV_LINE_ROWS[slot];
    oled_dev_line_t *dev = &s_dev_lines[slot];
    uint8_t line[OLED_COLS];
    memset(line, 0, sizeof(line));

    if (dev->device_id[0]) {
        for (size_t i = 0; i < 6 && dev->device_id[i]; i++) {
            uint8_t c = (uint8_t)dev->device_id[i];
            if (c < 0x20 || c > 0x7F) c = 0x20;
            memcpy(&line[i * OLED_CHAR_W], font6x8[c - 0x20], OLED_CHAR_W);
        }
    }

    int tx_icon, tx_text, rx_icon, rx_text;
    if (dev->rx_first) {
        rx_icon = 48;   rx_text = 57;
        tx_icon = 84;   tx_text = 93;
    } else {
        tx_icon = 48;   tx_text = 57;
        rx_icon = 84;   rx_text = 93;
    }

    if (dev->has_tx) {
        memcpy(&line[tx_icon], icon_tx, OLED_CHAR_W);
        for (size_t i = 0; i < 4 && dev->tx_cmd[i]; i++) {
            uint8_t c = (uint8_t)dev->tx_cmd[i];
            if (c < 0x20 || c > 0x7F) c = 0x20;
            memcpy(&line[tx_text + i * OLED_CHAR_W], font6x8[c - 0x20], OLED_CHAR_W);
        }
    }

    if (dev->has_rx) {
        memcpy(&line[rx_icon], icon_rx, OLED_CHAR_W);
        for (size_t i = 0; i < 2 && dev->rx_cmd[i]; i++) {
            uint8_t c = (uint8_t)dev->rx_cmd[i];
            if (c < 0x20 || c > 0x7F) c = 0x20;
            memcpy(&line[rx_text + i * OLED_CHAR_W], font6x8[c - 0x20], OLED_CHAR_W);
        }
        oled_draw_rssi_bars(line, dev->rssi);
    }

    send_cmd(0xB0 | row);
    send_cmd(0x00);
    send_cmd(0x10);
    send_data(line, OLED_COLS);
}

/* ---- OLED task: sole owner of I2C after init ---- */

static void oled_task(void *arg)
{
    (void)arg;
    oled_evt_t evt;
    TickType_t last_cleanup = 0;
    TickType_t last_net_update = 0;
    for (;;) {
        if (xQueueReceive(s_queue, &evt, pdMS_TO_TICKS(2000))) {
            switch (evt.type) {
            case OLED_EVT_TX: {
                int slot = find_or_alloc_line(evt.device_id);
                oled_dev_line_t *dev = &s_dev_lines[slot];
                if (!dev->has_tx && !dev->has_rx) dev->rx_first = false;
                memcpy(dev->device_id, evt.device_id, sizeof(dev->device_id));
                const char *cmd = evt.cmd_str;
                if (cmd[0] == '0' && (cmd[1] == 'x' || cmd[1] == 'X')) cmd += 2;
                snprintf(dev->tx_cmd, sizeof(dev->tx_cmd), "%.4s", cmd);
                dev->has_tx = true;
                dev->last_update = xTaskGetTickCount();
                oled_render_dev_line(slot);
                break;
            }
            case OLED_EVT_RX: {
                int slot = find_or_alloc_line(evt.device_id);
                oled_dev_line_t *dev = &s_dev_lines[slot];
                if (!dev->has_tx && !dev->has_rx) dev->rx_first = true;
                memcpy(dev->device_id, evt.device_id, sizeof(dev->device_id));
                snprintf(dev->rx_cmd, sizeof(dev->rx_cmd), "%.2s", evt.cmd_str);
                dev->rssi  = evt.rssi;
                dev->has_rx = true;
                dev->last_update = xTaskGetTickCount();
                oled_render_dev_line(slot);
                break;
            }
            case OLED_EVT_STATUS:
                oled_render_status_line(evt.cmd_str);
                s_status_time = evt.cmd_str[0] ? xTaskGetTickCount() : 0;
                break;
            }
        }

        TickType_t now = xTaskGetTickCount();

        if ((now - last_cleanup) >= pdMS_TO_TICKS(2000)) {
            last_cleanup = now;
            if (s_status_time && (now - s_status_time) >= pdMS_TO_TICKS(4000)) {
                s_status_time = 0;
                oled_render_status_line("");
            }
            TickType_t timeout = pdMS_TO_TICKS(30000);
            for (int i = 0; i < MAX_DEV_LINES; i++) {
                if (s_dev_lines[i].device_id[0] &&
                    (now - s_dev_lines[i].last_update) >= timeout)
                    memset(&s_dev_lines[i], 0, sizeof(oled_dev_line_t));
            }
            int kept = 0;
            for (int i = 0; i < MAX_DEV_LINES; i++) {
                if (s_dev_lines[i].device_id[0]) {
                    if (i != kept) {
                        s_dev_lines[kept] = s_dev_lines[i];
                        memset(&s_dev_lines[i], 0, sizeof(oled_dev_line_t));
                    }
                    kept++;
                }
            }
            s_next_slot = (kept < MAX_DEV_LINES) ? kept : 0;
            for (int i = 0; i < MAX_DEV_LINES; i++)
                oled_render_dev_line(i);
        }

        if ((now - last_net_update) >= pdMS_TO_TICKS(10000)) {
            last_net_update = now;
#ifdef CONFIG_CONNECTIVITY_CHOICE_ETH
            esp_netif_t *eth = esp_netif_get_handle_from_ifkey("eth");
            if (eth) {
                esp_netif_ip_info_t ip;
                oled_draw_header(esp_netif_get_ip_info(eth, &ip) == ESP_OK);
            } else {
                oled_draw_header(0);
            }
#else
            wifi_ap_record_t ap;
            int level;
            if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
                level = ap.rssi;
            } else {
                wifi_config_t cfg;
                if (esp_wifi_get_config(WIFI_IF_STA, &cfg) == ESP_OK && cfg.sta.ssid[0])
                    level = 0;
                else
                    level = RSSI_NO_CREDS;
            }
            oled_draw_header(level);
#endif
        }
    }
}

/* ---- Public API ---- */

esp_err_t oled_init(void)
{
    /* Hardware reset — Heltec boards require this before I2C init */
    if (CONFIG_OLED_RST_GPIO >= 0) {
        gpio_set_direction(CONFIG_OLED_RST_GPIO, GPIO_MODE_OUTPUT);
        gpio_set_level(CONFIG_OLED_RST_GPIO, 0);
        vTaskDelay(pdMS_TO_TICKS(10));
        gpio_set_level(CONFIG_OLED_RST_GPIO, 1);
        vTaskDelay(pdMS_TO_TICKS(100));  /* SSD1306 needs ~100ms after RST before accepting commands */
    }

    /* I2C master bus — only create if not already initialised */
    esp_err_t ret = i2c_master_get_bus_handle(I2C_PORT, &s_bus);
    if (ret != ESP_OK) {
        i2c_master_bus_config_t bus_cfg = {
            .i2c_port            = I2C_PORT,
            .sda_io_num          = CONFIG_OLED_SDA_GPIO,
            .scl_io_num          = CONFIG_OLED_SCL_GPIO,
            .clk_source          = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt   = 7,
            .flags.enable_internal_pullup = true,
        };
        ret = i2c_new_master_bus(&bus_cfg, &s_bus);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "I2C bus init failed: %s", esp_err_to_name(ret));
            return ret;
        }
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = CONFIG_OLED_I2C_ADDR,
        .scl_speed_hz    = CONFIG_OLED_I2C_FREQ_HZ,
    };
    ret = i2c_master_bus_add_device(s_bus, &dev_cfg, &s_dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C device add failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Send SSD1306 initialisation sequence — retry once on failure */
    for (int attempt = 0; attempt < 2; attempt++) {
        ret = ESP_OK;
        for (size_t i = 0; i < sizeof(INIT_CMDS); i++) {
            ret = send_cmd(INIT_CMDS[i]);
            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "init cmd 0x%02X failed (attempt %d): %s",
                         INIT_CMDS[i], attempt + 1, esp_err_to_name(ret));
                break;
            }
        }
        if (ret == ESP_OK) break;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "display init failed after retry");
        return ret;
    }

    /* Clear display, device-line state, then draw static layout — done before task starts */
    for (uint8_t p = 0; p < OLED_PAGES; p++)
        oled_print_line(p, NULL);
    memset(s_dev_lines, 0, sizeof(s_dev_lines));
    s_next_slot = 0;
    oled_draw_header(0);
    oled_draw_hline(1, 3);
    oled_draw_hline(6, 3);
    oled_init_version();
    oled_render_status_line("");

    /* Create queue and task — all I2C access goes through the task from here */
    s_queue = xQueueCreateStatic(OLED_QUEUE_DEPTH, sizeof(oled_evt_t),
                                 s_queue_storage, &s_queue_buf);
    xTaskCreateStatic(oled_task, "oled_task", 2048, NULL,
                      tskIDLE_PRIORITY + 1, s_task_stack, &s_task_buf);

    ESP_LOGI(TAG, "display init OK");
    return ESP_OK;
}

void oled_show_tx(const char *cmd_name, const char *device_id)
{
    oled_evt_t evt = { .type = OLED_EVT_TX, .position_pct = -1, .rssi = 0 };
    snprintf(evt.device_id, sizeof(evt.device_id), "%.6s", device_id ? device_id : "??????");
    snprintf(evt.cmd_str,   sizeof(evt.cmd_str),   "%.21s", cmd_name  ? cmd_name  : "");
    xQueueSend(s_queue, &evt, 0);
}

void oled_show_rx(const char *device_id, const char *cmd_hex,
                  int position_pct, int rssi)
{
    oled_evt_t evt = { .type = OLED_EVT_RX, .position_pct = position_pct, .rssi = rssi };
    snprintf(evt.device_id, sizeof(evt.device_id), "%.6s", device_id ? device_id : "??????");
    snprintf(evt.cmd_str,   sizeof(evt.cmd_str),   "%.2s",  cmd_hex   ? cmd_hex   : "??");
    xQueueSend(s_queue, &evt, 0);
}

void oled_show_status(const char *msg)
{
    oled_evt_t evt = { .type = OLED_EVT_STATUS, .position_pct = -1, .rssi = 0 };
    snprintf(evt.cmd_str, sizeof(evt.cmd_str), "%.21s", msg ? msg : "");
    xQueueSend(s_queue, &evt, 0);
}

#endif /* CONFIG_OLED_ENABLED */
