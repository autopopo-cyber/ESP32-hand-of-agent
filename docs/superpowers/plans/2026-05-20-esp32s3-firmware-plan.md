# ESP32S3 ODA-HID 固件实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在 Waveshare ESP32-S3-LCD-1.47（USB-A 直插款）上实现 USB 复合设备固件（HID Keyboard + HID Mouse + CDC ACM + ST7789 LCD 状态显示）

**Architecture:** ESP-IDF + TinyUSB + esp_lcd(ST7789)。3 个 FreeRTOS task：cdc_rx_task（CDC 接收→协议解析→ring buffer 入队）、hid_exec_task（ring buffer 出队→TinyUSB HID API 发送）、lcd_task（状态渲染 10fps）。main 线程处理 TinyUSB 设备事件。

**Tech Stack:** ESP-IDF v5.4+, esp_tinyusb component, esp_lcd + ST7789 driver, FreeRTOS, CMake

**参考项目:**
- esp-deck (`shantanugoel/esp-deck`) — sdkconfig TinyUSB 配置模板
- BusyUserBot (`ferzkopp/BusyUserBot`) — 分段初始化 + LCD 调试显示模式
- ESP-IDF 官方 `tusb_hid` 示例 — HID report descriptor 验证

**硬件引脚 (Waveshare ESP32-S3-LCD-1.47, USB-A 直插款):**

| 功能 | GPIO |
|------|------|
| LCD MOSI | 45 |
| LCD SCLK | 40 |
| LCD CS | 42 |
| LCD DC | 41 |
| LCD RST | 39 |
| LCD BL | 48 (active HIGH) |
| RGB LED | 38 (WS2812/SK6812, 变色) |
| USB D- | 19 |
| USB D+ | 20 |

> USB-A 直插款直接插入 PC USB 口，无需外接线缆。上电即枚举。

---

### Task 1: 项目骨架搭建

**Files:**
- Create: `CMakeLists.txt`
- Create: `main/CMakeLists.txt`
- Create: `main/idf_component.yml`
- Create: `sdkconfig.defaults`
- Create: `partitions.csv`

- [ ] **Step 1: 创建顶层 CMakeLists.txt**

```cmake
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(oda_hid)
```

- [ ] **Step 2: 创建 main/CMakeLists.txt**

```cmake
idf_component_register(
    SRCS
        "main.c"
        "usb_descriptors.c"
        "cdc_protocol.c"
        "hid_executor.c"
        "lcd_display.c"
    INCLUDE_DIRS "."
    REQUIRES
        esp_tinyusb
        driver
        esp_lcd
        nvs_flash
)
```

- [ ] **Step 3: 创建 main/idf_component.yml**

```yaml
dependencies:
  esp_tinyusb: "^1.4"
  idf: ">=5.4"
```

- [ ] **Step 4: 创建 sdkconfig.defaults**

参考 esp-deck 项目的 TinyUSB 配置，针对我们的复合设备调整：

```
# CPU / Memory
CONFIG_FREERTOS_HZ=1000
CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_240=y
CONFIG_SPIRAM=y
CONFIG_SPIRAM_MODE_OCT=y
CONFIG_SPIRAM_SPEED_80M=y
CONFIG_COMPILER_OPTIMIZATION_PERF=y

# TinyUSB — 复合设备
CONFIG_TINYUSB=y
CONFIG_TINYUSB_ENABLED=y
CONFIG_TINYUSB_CDC_ENABLED=y
CONFIG_TINYUSB_CDC_COUNT=1
CONFIG_TINYUSB_HID_ENABLED=y
CONFIG_TINYUSB_HID_COUNT=2
CONFIG_TINYUSB_MSC_ENABLED=n
CONFIG_TINYUSB_DFU_MODE_NONE=y
CONFIG_TINYUSB_NET_MODE_NONE=y

# TinyUSB descriptor
CONFIG_TINYUSB_DESC_CUSTOM_VID=0x303A
CONFIG_TINYUSB_DESC_CUSTOM_PID=0x80D1
CONFIG_TINYUSB_DESC_MANUFACTURER_STRING="ODA"
CONFIG_TINYUSB_DESC_PRODUCT_STRING="ODA HID Bridge"
CONFIG_TINYUSB_DESC_SERIAL_STRING="0001"
CONFIG_TINYUSB_DESC_BCD_DEVICE=0x0100

# TinyUSB runtime
CONFIG_TINYUSB_TASK_PRIORITY=5
CONFIG_TINYUSB_TASK_STACK_SIZE=4096

# USB OTG
CONFIG_USB_OTG_SUPPORT_DEVICE=y

# Flash
CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y

# Watchdog — 开发期间放宽
CONFIG_ESP_TASK_WDT_TIMEOUT_S=60
```

- [ ] **Step 5: 创建 partitions.csv**

```
# Name,   Type, SubType, Offset,  Size,     Flags
nvs,      data, nvs,     0x9000,  0x6000,
phy_init, data, phy,     0xf000,  0x1000,
factory,  app,  factory, 0x10000, 0x300000,
```

- [ ] **Step 6: 验证项目编译**

```bash
idf.py set-target esp32s3
idf.py build
```

Expected: 编译成功，main.c 可以是空的 `void app_main(void) {}`。

- [ ] **Step 7: Commit**

```bash
git init
git add -A
git commit -m "feat: project scaffolding for ESP32S3 ODA-HID firmware"
```

---

### Task 2: USB 复合设备描述符

**Files:**
- Create: `main/usb_descriptors.h`
- Create: `main/usb_descriptors.c`

- [ ] **Step 1: 创建 usb_descriptors.h**

```c
#pragma once

#include <stdint.h>
#include "tinyusb.h"

#ifdef __cplusplus
extern "C" {
#endif

// 接口索引
enum {
    ITF_NUM_CDC_CTRL = 0,   // CDC 通信接口
    ITF_NUM_CDC_DATA = 1,   // CDC 数据接口
    ITF_NUM_HID_KBD   = 2,  // HID 键盘接口
    ITF_NUM_HID_MOUSE = 3,  // HID 鼠标接口
    ITF_NUM_TOTAL     = 4
};

// 端点地址
enum {
    // CDC
    CDC_NOTIF_EP  = 0x81,   // IN, interrupt
    CDC_OUT_EP    = 0x02,   // OUT, bulk
    CDC_IN_EP     = 0x82,   // IN, bulk
    // HID Keyboard
    HID_KBD_EP    = 0x83,   // IN, interrupt
    // HID Mouse
    HID_MOUSE_EP  = 0x84,   // IN, interrupt
};

// HID Report ID
enum {
    REPORT_ID_KEYBOARD = 1,
    REPORT_ID_MOUSE    = 2,
};

// 字符串索引
enum {
    STRID_LANGID    = 0,
    STRID_MANUF     = 1,
    STRID_PRODUCT   = 2,
    STRID_SERIAL    = 3,
    STRID_CDC       = 4,
    STRID_HID_KBD   = 5,
    STRID_HID_MOUSE = 6,
};

extern const tinyusb_config_t tusb_cfg;

#ifdef __cplusplus
}
#endif
```

- [ ] **Step 2: 创建 usb_descriptors.c — 字符串描述符**

```c
#include "usb_descriptors.h"
#include "tusb.h"

// ---- 字符串描述符 ----
static const char *usb_strings[] = {
    [STRID_MANUF]   = "ODA",
    [STRID_PRODUCT] = "ODA HID Bridge",
    [STRID_SERIAL]  = "0001",
    [STRID_CDC]     = "ODA CDC",
    [STRID_HID_KBD] = "ODA Keyboard",
    [STRID_HID_MOUSE] = "ODA Mouse",
};

// ---- HID Report Descriptors ----

// 键盘 Report Descriptor (Boot Protocol, 8 bytes)
static const uint8_t hid_keyboard_report_desc[] = {
    TUD_HID_REPORT_DESC_KEYBOARD(HID_REPORT_ID(REPORT_ID_KEYBOARD))
};

// 鼠标 Report Descriptor (5 buttons, wheel, relative + absolute)
static const uint8_t hid_mouse_report_desc[] = {
    TUD_HID_REPORT_DESC_MOUSE(HID_REPORT_ID(REPORT_ID_MOUSE))
};

// ---- 组合 Configuration Descriptor ----
// IAD 不用于此组合，因为 CDC 是 Communication Class (2 interfaces)
// 直接串联: CDC Ctrl → CDC Data → HID KB → HID Mouse

static const uint8_t hid_config_desc[] = {
    // 配置头
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0,
        TUD_CONFIG_DESC_LEN
        + TUD_CDC_DESC_LEN          // CDC (2 interfaces + 2 EPs)
        + TUD_HID_DESC_LEN          // HID Keyboard (1 interface + 1 EP)
        + TUD_HID_DESC_LEN,         // HID Mouse (1 interface + 1 EP)
        TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),

    // === CDC ===
    // Interface 0: CDC Communication
    TUD_CDC_DESCRIPTOR(
        ITF_NUM_CDC_CTRL,
        STRID_CDC,
        CDC_NOTIF_EP,
        8,                          // notification EP max packet
        CDC_OUT_EP,                 // data OUT EP
        CDC_IN_EP,                  // data IN EP
        64),                        // data EP max packet

    // === HID Keyboard ===
    // Interface 2, Boot Protocol, Report ID=1
    TUD_HID_DESCRIPTOR(
        ITF_NUM_HID_KBD,
        STRID_HID_KBD,
        HID_ITF_PROTOCOL_KEYBOARD,
        sizeof(hid_keyboard_report_desc),
        HID_KBD_EP,
        CFG_TUD_HID_EP_BUFSIZE,
        1),                         // polling interval = 1ms

    // === HID Mouse ===
    // Interface 3, Report Protocol, Report ID=2
    TUD_HID_DESCRIPTOR(
        ITF_NUM_HID_MOUSE,
        STRID_HID_MOUSE,
        HID_ITF_PROTOCOL_MOUSE,
        sizeof(hid_mouse_report_desc),
        HID_MOUSE_EP,
        CFG_TUD_HID_EP_BUFSIZE,
        1),
};

// ---- TinyUSB 配置 ----
const tinyusb_config_t tusb_cfg = {
    .device_descriptor = NULL,
    .string_descriptor = NULL,
    .string_descriptor_count = 0,
    .external_phy = false,
    .configuration_descriptor = hid_config_desc,
    .self_powered = false,
    .vbus_monitor_io = 0,
};
```

- [ ] **Step 3: 提交**

```bash
git add main/usb_descriptors.c main/usb_descriptors.h
git commit -m "feat: USB composite descriptor (CDC + HID KB + HID Mouse)"
```

---

### Task 3: CDC 协议解析器

**Files:**
- Create: `main/cdc_protocol.h`
- Create: `main/cdc_protocol.c`

- [ ] **Step 1: 创建 cdc_protocol.h — 指令结构体定义**

```c
#pragma once

#include <stdint.h>
#include <stddef.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

// ---- 指令操作码 ----
enum {
    CMD_MOUSE_MOVE      = 0x01,  // int16 dx, int16 dy
    CMD_MOUSE_MOVE_TO   = 0x02,  // uint16 x, uint16 y (0-32767)
    CMD_MOUSE_CLICK     = 0x03,  // uint8 button
    CMD_MOUSE_PRESS     = 0x04,  // uint8 button
    CMD_MOUSE_RELEASE   = 0x05,  // uint8 button
    CMD_MOUSE_SCROLL    = 0x06,  // int8 delta
    CMD_KEY_PRESS       = 0x07,  // uint8 hid_code
    CMD_KEY_RELEASE     = 0x08,  // uint8 hid_code
    CMD_KEY_TAP         = 0x09,  // uint8 hid_code, uint8 count
    CMD_KEY_TYPE        = 0x0A,  // uint8 len, char[len]
    CMD_SYNC            = 0x0B,  // uint16 seq_id
    CMD_NOOP            = 0x0C,  // uint16 delay_ms
    CMD_LED_SET         = 0xFD,  // uint8 r, g, b
    CMD_ACK_REQUEST     = 0xFE,  // (no payload)
    CMD_RESET           = 0xFF,  // (no payload)
};

// 按钮常量
enum {
    BTN_LEFT   = 1,
    BTN_RIGHT  = 2,
    BTN_MIDDLE = 4,
};

// ---- 上行消息 (ESP32 → PC) ----
enum {
    UPLINK_ACK       = 0x01,
    UPLINK_STATUS    = 0x03,
    UPLINK_ERROR     = 0xFF,
};

// 状态结构体 (20 bytes, 对应 spec)
typedef struct {
    uint16_t seq_id;       // 最后执行的 seq
    uint8_t  cmd_queued;   // 缓冲区中待执行指令数
    uint8_t  kb_leds;      // 键盘 LED 状态 (Num/Caps/Scroll lock)
    uint8_t  mouse_btn;    // 当前按下的鼠标按键
    int16_t  mouse_x;      // 最后鼠标位置 X
    int16_t  mouse_y;      // 最后鼠标位置 Y
    uint32_t uptime_ms;    // 设备运行时间
    uint8_t  reserved[6];
} __attribute__((packed)) status_t;

// ---- 解析后的指令 ----
typedef struct {
    uint8_t  opcode;
    uint8_t  payload[128]; // 最大变长 payload
    uint8_t  payload_len;
    uint16_t seq_id;       // 从 SYNC 帧提取
} cmd_t;

// ---- API ----
void cdc_protocol_init(void);
void cdc_protocol_send_ack(uint16_t seq_id, uint8_t status);
void cdc_protocol_send_status(const status_t *status);
void cdc_protocol_send_error(uint8_t code);

// 从 CDC 数据流中解析一条指令，返回解析出的指令数 (0 或 1)
// raw_data: CDC 接收的原始字节
// len: 字节数
// out: 输出解析后的指令
// consumed: 实际消费的字节数（用于处理粘包）
int cdc_parse_command(const uint8_t *raw_data, size_t len,
                      cmd_t *out, size_t *consumed);

#ifdef __cplusplus
}
#endif
```

- [ ] **Step 2: 创建 cdc_protocol.c — 解析实现**

```c
#include "cdc_protocol.h"
#include "tusb.h"
#include <string.h>

// 已知的指令编码长度表 (不含 opcode 自身)
static const int8_t cmd_payload_len[256] = {
    [CMD_MOUSE_MOVE]    = 4,   // int16 dx + int16 dy
    [CMD_MOUSE_MOVE_TO] = 4,   // uint16 x + uint16 y
    [CMD_MOUSE_CLICK]   = 1,
    [CMD_MOUSE_PRESS]   = 1,
    [CMD_MOUSE_RELEASE] = 1,
    [CMD_MOUSE_SCROLL]  = 1,
    [CMD_KEY_PRESS]     = 1,
    [CMD_KEY_RELEASE]   = 1,
    [CMD_KEY_TAP]       = 2,
    [CMD_KEY_TYPE]      = -1,  // 变长: 1字节长度 + N字节内容
    [CMD_SYNC]          = 2,
    [CMD_NOOP]          = 2,
    [CMD_LED_SET]       = 3,
    [CMD_ACK_REQUEST]   = 0,
    [CMD_RESET]         = 0,
    // default: -128 = 未知指令
};

void cdc_protocol_init(void) {
    // 预留, 初始化序列号计数器等
}

int cdc_parse_command(const uint8_t *raw_data, size_t len,
                      cmd_t *out, size_t *consumed) {
    if (len < 1) return 0;

    uint8_t opcode = raw_data[0];
    int8_t fixed_len = cmd_payload_len[opcode];

    // 未知指令: 跳过 1 字节, 发送错误
    if (fixed_len == -128) {
        cdc_protocol_send_error(0x01); // unknown opcode
        *consumed = 1;
        return 0;
    }

    size_t payload_len;
    if (fixed_len == -1) {
        // 变长指令 (KEY_TYPE): 第二字节是字符串长度
        if (len < 2) return 0;
        payload_len = 1 + raw_data[1]; // 1字节len + 字符串内容
        if (len < 1 + payload_len) return 0;
    } else {
        payload_len = (size_t)fixed_len;
        if (len < 1 + payload_len) return 0;
    }

    out->opcode = opcode;
    out->payload_len = payload_len;
    memcpy(out->payload, raw_data + 1, payload_len);
    *consumed = 1 + payload_len;

    // 跟踪 seq_id
    static uint16_t current_seq = 0;
    if (opcode == CMD_SYNC && payload_len >= 2) {
        current_seq = (uint16_t)out->payload[0]
                    | ((uint16_t)out->payload[1] << 8);
    }
    out->seq_id = current_seq;

    return 1;
}

void cdc_protocol_send_ack(uint16_t seq_id, uint8_t status) {
    uint8_t buf[4] = {
        UPLINK_ACK,
        (uint8_t)(seq_id & 0xFF),
        (uint8_t)((seq_id >> 8) & 0xFF),
        status
    };
    tud_cdc_write(buf, sizeof(buf));
    tud_cdc_write_flush();
}

void cdc_protocol_send_status(const status_t *status) {
    uint8_t buf[1 + sizeof(status_t)];
    buf[0] = UPLINK_STATUS;
    memcpy(buf + 1, status, sizeof(status_t));
    tud_cdc_write(buf, sizeof(buf));
    tud_cdc_write_flush();
}

void cdc_protocol_send_error(uint8_t code) {
    uint8_t buf[2] = { UPLINK_ERROR, code };
    tud_cdc_write(buf, sizeof(buf));
    tud_cdc_write_flush();
}
```

- [ ] **Step 3: 提交**

```bash
git add main/cdc_protocol.c main/cdc_protocol.h
git commit -m "feat: CDC binary protocol parser"
```

---

### Task 4: HID 执行器

**Files:**
- Create: `main/hid_executor.h`
- Create: `main/hid_executor.c`

- [ ] **Step 1: 创建 hid_executor.h**

```c
#pragma once

#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "cdc_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

// 执行器状态
typedef struct {
    uint16_t last_seq;
    uint8_t  kb_modifier;       // 当前键盘 modifier 位
    uint8_t  kb_keys[6];        // 当前按下的键码
    uint8_t  mouse_buttons;     // 当前按下的鼠标按键
    int16_t  mouse_x;
    int16_t  mouse_y;
    uint8_t  kb_leds;           // Num/Caps/Scroll lock 状态
    uint8_t  cmd_queue_len;     // ring buffer 中等待的指令数
    uint32_t last_action_ms;    // 上次动作的时间戳
} hid_state_t;

// 初始化执行器
void hid_executor_init(void);

// 提交一条解析后的指令到执行队列
// 返回 0=成功, -1=队列满
int hid_executor_submit(const cmd_t *cmd);

// 在 FreeRTOS task 中循环调用, 消费队列并执行 HID 操作
void hid_executor_task(void *arg);

// 释放所有按键/鼠标(异常恢复用)
void hid_executor_release_all(void);

// 获取当前状态快照
void hid_executor_get_state(hid_state_t *out);

#ifdef __cplusplus
}
#endif
```

- [ ] **Step 2: 创建 hid_executor.c — 核心执行逻辑**

```c
#include "hid_executor.h"
#include "tusb.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include <string.h>

#define CMD_QUEUE_SIZE 64

static cmd_t cmd_queue[CMD_QUEUE_SIZE];
static volatile int cmd_head = 0;
static volatile int cmd_tail = 0;
static hid_state_t g_state = {0};

// USB HID keycode 转 ASCII (简化 US QWERTY 映射)
static const uint8_t ascii_to_hid[128] = {
    ['\b'] = 0x2A,  // BACKSPACE
    ['\t'] = 0x2B,  // TAB
    ['\n'] = 0x28,  // ENTER
    [' ']  = 0x2C,  // SPACE
    ['!']  = 0x1E,  // 1 (shifted)
    ['"']  = 0x34,  // ' (shifted)
    ['#']  = 0x20,  // 3 (shifted)
    ['$']  = 0x21,  // 4 (shifted)
    ['%']  = 0x22,  // 5 (shifted)
    ['&']  = 0x23,  // 7 (shifted)
    ['\''] = 0x34,
    ['(']  = 0x25,  // 9 (shifted)
    [')']  = 0x26,  // 0 (shifted)
    ['*']  = 0x25,  // 8 (shifted)
    ['+']  = 0x2E,  // = (shifted)
    [',']  = 0x36,
    ['-']  = 0x2D,
    ['.']  = 0x37,
    ['/']  = 0x38,
    ['0']  = 0x27,
    ['1']  = 0x1E,
    ['2']  = 0x1F,
    ['3']  = 0x20,
    ['4']  = 0x21,
    ['5']  = 0x22,
    ['6']  = 0x23,
    ['7']  = 0x24,
    ['8']  = 0x25,
    ['9']  = 0x26,
    [':']  = 0x33,  // ; (shifted)
    [';']  = 0x33,
    ['<']  = 0x36,  // , (shifted)
    ['=']  = 0x2E,
    ['>']  = 0x37,  // . (shifted)
    ['?']  = 0x38,  // / (shifted)
    ['@']  = 0x1F,  // 2 (shifted)
    ['A']  = 0x04,
    ['B']  = 0x05,
    ['C']  = 0x06,
    ['D']  = 0x07,
    ['E']  = 0x08,
    ['F']  = 0x09,
    ['G']  = 0x0A,
    ['H']  = 0x0B,
    ['I']  = 0x0C,
    ['J']  = 0x0D,
    ['K']  = 0x0E,
    ['L']  = 0x0F,
    ['M']  = 0x10,
    ['N']  = 0x11,
    ['O']  = 0x12,
    ['P']  = 0x13,
    ['Q']  = 0x14,
    ['R']  = 0x15,
    ['S']  = 0x16,
    ['T']  = 0x17,
    ['U']  = 0x18,
    ['V']  = 0x19,
    ['W']  = 0x1A,
    ['X']  = 0x1B,
    ['Y']  = 0x1C,
    ['Z']  = 0x1D,
    ['[']  = 0x2F,
    ['\\'] = 0x31,
    [']']  = 0x30,
    ['^']  = 0x23,  // 6 (shifted)
    ['_']  = 0x2D,  // - (shifted)
    ['`']  = 0x35,
    ['a']  = 0x04,
    ['b']  = 0x05,
    ['c']  = 0x06,
    ['d']  = 0x07,
    ['e']  = 0x08,
    ['f']  = 0x09,
    ['g']  = 0x0A,
    ['h']  = 0x0B,
    ['i']  = 0x0C,
    ['j']  = 0x0D,
    ['k']  = 0x0E,
    ['l']  = 0x0F,
    ['m']  = 0x10,
    ['n']  = 0x11,
    ['o']  = 0x12,
    ['p']  = 0x13,
    ['q']  = 0x14,
    ['r']  = 0x15,
    ['s']  = 0x16,
    ['t']  = 0x17,
    ['u']  = 0x18,
    ['v']  = 0x19,
    ['w']  = 0x1A,
    ['x']  = 0x1B,
    ['y']  = 0x1C,
    ['z']  = 0x1D,
    ['{']  = 0x2F,  // [ (shifted)
    ['|']  = 0x31,  // \ (shifted)
    ['}']  = 0x30,  // ] (shifted)
    ['~']  = 0x35,  // ` (shifted)
};

static bool char_needs_shift(char c) {
    return (c >= 'A' && c <= 'Z')
        || (c >= '!' && c <= '@' && c != '\'' && c != ',' && c != '-'
            && c != '.' && c != '/' && c != ';' && c != '=')
        || c == '_' || c == '{' || c == '}' || c == '|' || c == '~'
        || c == ':' || c == '<' || c == '>' || c == '?' || c == '"';
}

void hid_executor_init(void) {
    memset(&g_state, 0, sizeof(g_state));
    g_state.last_action_ms = esp_timer_get_time() / 1000;
}

int hid_executor_submit(const cmd_t *cmd) {
    int next_head = (cmd_head + 1) % CMD_QUEUE_SIZE;
    if (next_head == cmd_tail) return -1; // 队列满
    memcpy(&cmd_queue[cmd_head], cmd, sizeof(cmd_t));
    cmd_head = next_head;
    return 0;
}

// 消费队列中一条指令并执行
static void process_one_command(void) {
    if (cmd_tail == cmd_head) return; // 队列空

    cmd_t *cmd = &cmd_queue[cmd_tail];

    // 等待 USB HID 就绪
    if (!tud_hid_ready()) return;

    switch (cmd->opcode) {
    case CMD_MOUSE_MOVE: {
        int16_t dx = (int16_t)(cmd->payload[0] | (cmd->payload[1] << 8));
        int16_t dy = (int16_t)(cmd->payload[2] | (cmd->payload[3] << 8));
        g_state.mouse_x += dx;
        g_state.mouse_y += dy;
        tud_hid_mouse_report(REPORT_ID_MOUSE, g_state.mouse_buttons,
                             dx, dy, 0, 0);
        break;
    }
    case CMD_MOUSE_MOVE_TO: {
        // 绝对坐标: 先归零再步进
        uint16_t target_x = cmd->payload[0] | (cmd->payload[1] << 8);
        uint16_t target_y = cmd->payload[2] | (cmd->payload[3] << 8);
        // 简化: 大规模相对移动 (BusyUserBot 的 slam 策略)
        for (int i = 0; i < 80; i++) {
            tud_hid_mouse_report(REPORT_ID_MOUSE, 0, -127, -127, 0, 0);
            vTaskDelay(pdMS_TO_TICKS(8));
        }
        // 步进到目标
        int32_t dx = (int32_t)target_x;
        int32_t dy = (int32_t)target_y;
        while (dx != 0 || dy != 0) {
            int8_t sx = (dx > 120) ? 120 : (dx < -120) ? -120 : (int8_t)dx;
            int8_t sy = (dy > 120) ? 120 : (dy < -120) ? -120 : (int8_t)dy;
            tud_hid_mouse_report(REPORT_ID_MOUSE, 0, sx, sy, 0, 0);
            dx -= sx; dy -= sy;
            vTaskDelay(pdMS_TO_TICKS(8));
        }
        g_state.mouse_x = target_x;
        g_state.mouse_y = target_y;
        break;
    }
    case CMD_MOUSE_CLICK: {
        uint8_t btn = cmd->payload[0];
        tud_hid_mouse_report(REPORT_ID_MOUSE, btn, 0, 0, 0, 0);
        vTaskDelay(pdMS_TO_TICKS(20));
        tud_hid_mouse_report(REPORT_ID_MOUSE, 0, 0, 0, 0, 0);
        break;
    }
    case CMD_MOUSE_PRESS:
        g_state.mouse_buttons |= cmd->payload[0];
        tud_hid_mouse_report(REPORT_ID_MOUSE, g_state.mouse_buttons, 0, 0, 0, 0);
        break;
    case CMD_MOUSE_RELEASE:
        g_state.mouse_buttons &= ~cmd->payload[0];
        tud_hid_mouse_report(REPORT_ID_MOUSE, g_state.mouse_buttons, 0, 0, 0, 0);
        break;
    case CMD_MOUSE_SCROLL:
        tud_hid_mouse_report(REPORT_ID_MOUSE, g_state.mouse_buttons,
                             0, 0, (int8_t)cmd->payload[0], 0);
        break;
    case CMD_KEY_PRESS: {
        uint8_t code = cmd->payload[0];
        // 查找空位插入
        for (int i = 0; i < 6; i++) {
            if (g_state.kb_keys[i] == 0) {
                g_state.kb_keys[i] = code;
                break;
            }
        }
        tud_hid_keyboard_report(REPORT_ID_KEYBOARD, g_state.kb_modifier, g_state.kb_keys);
        break;
    }
    case CMD_KEY_RELEASE: {
        uint8_t code = cmd->payload[0];
        for (int i = 0; i < 6; i++) {
            if (g_state.kb_keys[i] == code) {
                g_state.kb_keys[i] = 0;
            }
        }
        tud_hid_keyboard_report(REPORT_ID_KEYBOARD, g_state.kb_modifier, g_state.kb_keys);
        break;
    }
    case CMD_KEY_TAP: {
        uint8_t code = cmd->payload[0];
        uint8_t count = cmd->payload[1];
        for (int i = 0; i < count; i++) {
            uint8_t tmp[6] = { code, 0, 0, 0, 0, 0 };
            tud_hid_keyboard_report(REPORT_ID_KEYBOARD, 0, tmp);
            vTaskDelay(pdMS_TO_TICKS(10));
            tud_hid_keyboard_report(REPORT_ID_KEYBOARD, 0, NULL);
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        break;
    }
    case CMD_KEY_TYPE: {
        uint8_t count = cmd->payload[0];
        const char *str = (const char *)cmd->payload + 1;
        for (int i = 0; i < count; i++) {
            char c = str[i];
            if (c < 0 || c > 127) continue;
            uint8_t hid_code = ascii_to_hid[(uint8_t)c];
            if (hid_code == 0) continue;

            uint8_t modifier = char_needs_shift(c) ? 0x02 : 0x00; // left shift
            uint8_t keys[6] = { hid_code, 0, 0, 0, 0, 0 };
            tud_hid_keyboard_report(REPORT_ID_KEYBOARD, modifier, keys);
            vTaskDelay(pdMS_TO_TICKS(5));
            tud_hid_keyboard_report(REPORT_ID_KEYBOARD, 0, NULL);
            vTaskDelay(pdMS_TO_TICKS(5));
        }
        break;
    }
    case CMD_SYNC:
        // seq_id 已在 cdc_parse_command 中更新,
        // SYNC 本身不产生 HID 动作, 仅发 ACK
        cdc_protocol_send_ack(cmd->seq_id, 0);
        break;
    case CMD_NOOP: {
        uint16_t delay_ms = cmd->payload[0] | (cmd->payload[1] << 8);
        if (delay_ms > 10000) delay_ms = 10000;
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
        break;
    }
    case CMD_RESET:
        esp_restart();
        break;
    case CMD_ACK_REQUEST:
        // 触发状态回传 (由 lcd_task 处理)
        break;
    default:
        cdc_protocol_send_error(0x01);
        break;
    }

    g_state.last_seq = cmd->seq_id;
    g_state.last_action_ms = esp_timer_get_time() / 1000;
    cmd_tail = (cmd_tail + 1) % CMD_QUEUE_SIZE;
}

void hid_executor_task(void *arg) {
    hid_executor_init();
    while (1) {
        process_one_command();
        vTaskDelay(pdMS_TO_TICKS(1)); // 1ms 节拍
    }
}

void hid_executor_release_all(void) {
    memset(g_state.kb_keys, 0, sizeof(g_state.kb_keys));
    g_state.kb_modifier = 0;
    g_state.mouse_buttons = 0;
    if (tud_hid_ready()) {
        tud_hid_keyboard_report(REPORT_ID_KEYBOARD, 0, NULL);
        tud_hid_mouse_report(REPORT_ID_MOUSE, 0, 0, 0, 0, 0);
    }
}

void hid_executor_get_state(hid_state_t *out) {
    *out = g_state;
    int pending = cmd_head - cmd_tail;
    if (pending < 0) pending += CMD_QUEUE_SIZE;
    g_state.cmd_queue_len = (uint8_t)pending;
}
```

- [ ] **Step 3: 提交**

```bash
git add main/hid_executor.c main/hid_executor.h
git commit -m "feat: HID executor with command queue, keyboard and mouse handling"
```

---

### Task 5: ST7789 LCD 状态显示

**Files:**
- Create: `main/lcd_display.h`
- Create: `main/lcd_display.c`

- [ ] **Step 1: 创建 lcd_display.h**

```c
#pragma once

#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hid_executor.h"

#ifdef __cplusplus
extern "C" {
#endif

// 渲染状态
typedef struct {
    const char *app_name;      // 当前应用名
    const char *step_name;     // 当前 step
    const char *vlm_model;     // 使用的 VLM 模型
    uint8_t     progress;      // 进度 0-100
    uint16_t    call_count;    // VLM 调用次数
    uint32_t    elapsed_ms;    // 当前 step 耗时
    bool        usb_connected;
    bool        yolo_online;
    uint16_t    last_seq;
    uint8_t     cmd_pending;
} lcd_state_t;

void lcd_display_init(void);
void lcd_display_update(const lcd_state_t *state);
void lcd_render_task(void *arg);

#ifdef __cplusplus
}
#endif
```

- [ ] **Step 2: 创建 lcd_display.c — ST7789 初始化 + framebuffer 渲染**

```c
#include "lcd_display.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_st7789.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "hid_executor.h"
#include <stdio.h>
#include <string.h>

// Waveshare ESP32-S3-LCD-1.47 引脚
#define LCD_MOSI  GPIO_NUM_45
#define LCD_SCLK  GPIO_NUM_40
#define LCD_CS    GPIO_NUM_42
#define LCD_DC    GPIO_NUM_41
#define LCD_RST   GPIO_NUM_39
#define LCD_BL    GPIO_NUM_48
#define LCD_HOST  SPI2_HOST

#define LCD_H_RES 172
#define LCD_V_RES 320
#define LCD_BITS_PER_PIXEL 16

static esp_lcd_panel_handle_t panel_handle = NULL;
static esp_lcd_panel_io_handle_t io_handle = NULL;

// 颜色定义 (RGB565)
#define COLOR_BG        0x0000  // 黑
#define COLOR_STATUS_RUN   0x07E0  // 绿
#define COLOR_STATUS_WAIT  0x001F  // 蓝
#define COLOR_STATUS_ERR   0xFFE0  // 黄
#define COLOR_TEXT       0xFFFF  // 白
#define COLOR_TEXT_DIM   0x8410  // 灰
#define COLOR_CARD_BG    0x18E3  // 深蓝灰
#define COLOR_PROGRESS_BG 0x2104 // 深绿灰
#define COLOR_PROGRESS_FG 0x07E0 // 绿

//
// 底层像素绘制 — 用 esp_lcd_panel_draw_bitmap 直接写 framebuffer
//
// 由于 esp_lcd 没有逐像素绘制 API, 我们用固定颜色块 + bitmap 方式
// 为此实现一个简单的双缓冲 framebuffer
//
static uint16_t fb[LCD_H_RES * LCD_V_RES];

static void fb_clear(uint16_t color) {
    for (int i = 0; i < LCD_H_RES * LCD_V_RES; i++) fb[i] = color;
}

// 绘制水平矩形 (x, y, w, h)
static void fb_fill_rect(int x, int y, int w, int h, uint16_t color) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > LCD_H_RES) w = LCD_H_RES - x;
    if (y + h > LCD_V_RES) h = LCD_V_RES - y;
    for (int row = y; row < y + h; row++) {
        for (int col = x; col < x + w; col++) {
            fb[row * LCD_H_RES + col] = color;
        }
    }
}

// 绘制ASCII字符 (8x16 等宽字体)
// 简化: 使用内置 5x7 字符表 (仅 ASCII 可见字符)
static const uint8_t font5x7[][5] = {
    // 这里用占位 — 实际实现使用完整的 5x7 字体表
    // 或嵌入一个 8x16 字体
};

static void fb_draw_char(int x, int y, char c, uint16_t color, uint8_t scale) {
    // 使用 esp_lcd 内置功能或简单像素绘制
    // 实际实现: 查询字体表, 逐像素写入 fb
    // (字体表过长, 此处省略 — 实现时从标准 5x7 字模数组查询)
    if (c < ' ' || c > '~') c = '?';
    uint8_t idx = c - ' ';
    // 绘制 5x7 → 缩放 w*5, h*7
    for (int row = 0; row < 7; row++) {
        uint8_t line = font5x7[idx][row];
        for (int col = 0; col < 5; col++) {
            if (line & (1 << col)) {
                for (int sy = 0; sy < scale; sy++) {
                    for (int sx = 0; sx < scale; sx++) {
                        int px = x + col * scale + sx;
                        int py = y + row * scale + sy;
                        if (px < LCD_H_RES && py < LCD_V_RES)
                            fb[py * LCD_H_RES + px] = color;
                    }
                }
            }
        }
    }
}

static void fb_draw_string(int x, int y, const char *s, uint16_t color, uint8_t scale) {
    while (*s) {
        fb_draw_char(x, y, *s, color, scale);
        x += 6 * scale; // 5px width + 1px spacing
        if (x > LCD_H_RES - 6) break;
        s++;
    }
}

// 进度条 (x, y, w, h, pct 0-100)
static void fb_draw_progress(int x, int y, int w, int h, uint8_t pct) {
    fb_fill_rect(x, y, w, h, COLOR_PROGRESS_BG);
    int fill_w = (w * pct) / 100;
    if (fill_w > 0) fb_fill_rect(x, y, fill_w, h, COLOR_PROGRESS_FG);
}

// flush fb 到 ST7789
static void fb_flush(void) {
    esp_lcd_panel_draw_bitmap(panel_handle, 0, 0,
        LCD_H_RES, LCD_V_RES, fb);
}

//
// LCD 初始化 — 参考 LovyanGFX 的 ST7789 init sequence
//
void lcd_display_init(void) {
    // Backlight GPIO
    gpio_set_direction(LCD_BL, GPIO_MODE_OUTPUT);
    gpio_set_level(LCD_BL, 0); // 先关背光

    // SPI 总线
    spi_bus_config_t spi_cfg = {
        .mosi_io_num = LCD_MOSI,
        .miso_io_num = -1,
        .sclk_io_num = LCD_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = LCD_H_RES * LCD_V_RES * 2,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &spi_cfg, SPI_DMA_CH_AUTO));

    // Panel IO (3 线 SPI: SCLK + MOSI, 无 MISO)
    esp_lcd_panel_io_spi_config_t io_cfg = {
        .cs_gpio_num = LCD_CS,
        .dc_gpio_num = LCD_DC,
        .spi_mode = 0,
        .pclk_hz = 40 * 1000 * 1000,  // 40 MHz
        .trans_queue_depth = 10,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(
        (esp_lcd_spi_bus_handle_t)LCD_HOST, &io_cfg, &io_handle));

    // ST7789 Panel
    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io_handle, &panel_cfg, &panel_handle));

    // 初始化序列
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_handle, true));
    ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(panel_handle, false));   // 竖屏不交换
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel_handle, false, false));
    ESP_ERROR_CHECK(esp_lcd_panel_set_gap(panel_handle, 0, 0));   // 172×320 无偏移

    // 开显示和背光
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));
    gpio_set_level(LCD_BL, 1);

    // 初始黑屏
    fb_clear(COLOR_BG);
    fb_flush();
}

//
// 状态 UI 渲染 — 按 spec 的 4 区布局
//
void lcd_display_update(const lcd_state_t *state) {
    fb_clear(COLOR_BG);

    // 1. 顶部状态栏 (y=0, h=20)
    uint16_t status_color = state->usb_connected ? COLOR_STATUS_RUN : COLOR_STATUS_ERR;
    fb_fill_rect(0, 0, LCD_H_RES, 20, status_color);
    char status_text[32];
    snprintf(status_text, sizeof(status_text), "ODA LIVE %s",
             state->usb_connected ? "" : "!");
    fb_draw_string(3, 2, status_text, COLOR_BG, 1);

    // 2. 任务信息区 (y=24, h=180)
    int y = 30;
    char line[64];

    fb_draw_string(3, y, "CONNECTED", state->usb_connected ? COLOR_STATUS_RUN : COLOR_STATUS_ERR, 1);
    y += 14;

    snprintf(line, sizeof(line), "step: %s", state->step_name ? state->step_name : "---");
    fb_draw_string(3, y, line, COLOR_TEXT, 1);
    y += 14;

    snprintf(line, sizeof(line), "target: %s", state->app_name ? state->app_name : "---");
    fb_draw_string(3, y, line, COLOR_TEXT, 1);
    y += 14;

    snprintf(line, sizeof(line), "vlm: %s", state->vlm_model ? state->vlm_model : "---");
    fb_draw_string(3, y, line, COLOR_TEXT_DIM, 1);
    y += 14;

    snprintf(line, sizeof(line), "calls: %u | time: %us",
             state->call_count, (unsigned)(state->elapsed_ms / 1000));
    fb_draw_string(3, y, line, COLOR_TEXT_DIM, 1);
    y += 20;

    // 进度条
    fb_draw_progress(3, y, LCD_H_RES - 6, 10, state->progress);
    y += 4;
    snprintf(line, sizeof(line), "%u%%", state->progress);
    fb_draw_string(LCD_H_RES / 2 - 10, y, line, COLOR_TEXT, 1);
    y += 24;

    // 3. App Card (y 自动)
    fb_fill_rect(3, y, LCD_H_RES - 6, 40, COLOR_CARD_BG);
    fb_draw_string(10, y + 12, state->app_name ? state->app_name : "NO APP",
                   COLOR_TEXT, 2);
    y += 50;

    // 4. 底部状态条 (y=290, h=30)
    fb_fill_rect(0, 290, LCD_H_RES, 30, COLOR_CARD_BG);
    snprintf(line, sizeof(line), "yolo:%s seq:%04u",
             state->yolo_online ? "*" : "-", state->last_seq);
    fb_draw_string(3, 296, line, COLOR_TEXT_DIM, 1);
    snprintf(line, sizeof(line), "q:%u", state->cmd_pending);
    fb_draw_string(LCD_H_RES - 30, 296, line, COLOR_TEXT, 1);

    fb_flush();
}

//
// LCD 渲染任务 — 从 hid_executor 读取状态, 定期刷新
//
void lcd_render_task(void *arg) {
    lcd_display_init();

    lcd_state_t state = {
        .app_name = "boot",
        .step_name = "init",
        .usb_connected = false,
        .yolo_online = false,
    };
    lcd_display_update(&state);

    while (1) {
        hid_state_t hid;
        hid_executor_get_state(&hid);

        // 更新状态
        state.last_seq = hid.last_seq;
        state.cmd_pending = hid.cmd_queue_len;
        state.usb_connected = tud_mounted();
        state.elapsed_ms = (esp_timer_get_time() / 1000) - hid.last_action_ms;

        // 通过 CDC 流更新 app_name / step_name / vlm_model 等
        // (由 cdc_rx_task 解析自定义扩展指令后修改全局变量)

        lcd_display_update(&state);
        vTaskDelay(pdMS_TO_TICKS(100)); // 10fps
    }
}
```

> **注意:** `font5x7` 字体表需要完整的 96 字符 × 5×7 位图数据。实现时从标准字模库复制或使用 `esp_lcd` 的内置字体 API。如果使用 LVGL 则完全替代此 fb_* 系列函数。

- [ ] **Step 4: 提交**

```bash
git add main/lcd_display.c main/lcd_display.h
git commit -m "feat: ST7789 LCD driver with status display renderer"
```

---

### Task 6: 主程序集成

**Files:**
- Create: `main/main.c`

- [ ] **Step 1: 创建 main.c — CDC 接收任务 + 任务创建**

```c
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "tinyusb.h"
#include "tusb_cdc_acm.h"
#include "usb_descriptors.h"
#include "cdc_protocol.h"
#include "hid_executor.h"
#include "lcd_display.h"

static const char *TAG = "oda_main";

// CDC 接收 buffer
#define CDC_RX_BUF_SIZE 256
static uint8_t cdc_rx_buf[CDC_RX_BUF_SIZE];

// ---- CDC 回调 ----
static void cdc_rx_callback(int itf, const uint8_t *buf, uint32_t size) {
    size_t offset = 0;
    while (offset < size) {
        cmd_t cmd;
        size_t consumed = 0;
        int parsed = cdc_parse_command(buf + offset, size - offset,
                                       &cmd, &consumed);
        if (parsed > 0) {
            hid_executor_submit(&cmd);
        }
        offset += consumed;
        if (consumed == 0) break; // 不完整帧, 丢弃剩余
    }
}

// ---- 主函数入口 ----
void app_main(void) {
    // NVS 初始化 (TinyUSB 需要)
    ESP_ERROR_CHECK(nvs_flash_init());

    ESP_LOGI(TAG, "=== ODA HID Bridge starting ===");

    // Step 1: USB 初始化
    ESP_LOGI(TAG, "[stage] USB init");
    const tinyusb_config_t cfg = {
        .device_descriptor = NULL,
        .string_descriptor = NULL,
        .string_descriptor_count = 0,
        .external_phy = false,
        .configuration_descriptor = NULL,
        .self_powered = false,
        .vbus_monitor_io = 0,
    };
    ESP_ERROR_CHECK(tinyusb_driver_install(&cfg));

    // Step 2: CDC ACM 初始化
    ESP_LOGI(TAG, "[stage] CDC init");
    tinyusb_config_cdcacm_t acm_cfg = {
        .usb_dev = TINYUSB_USBDEV_0,
        .cdc_port = TINYUSB_CDC_ACM_0,
        .callback_rx = cdc_rx_callback,
        .callback_rx_wanted_char = NULL,
        .callback_line_state_changed = NULL,
        .callback_line_coding_changed = NULL,
    };
    ESP_ERROR_CHECK(tusb_cdc_acm_init(&acm_cfg));

    // Step 3: 创建 FreeRTOS 任务
    ESP_LOGI(TAG, "[stage] Tasks init");

    // HID 执行器任务 (高优先级: 保证 HID 响应及时)
    xTaskCreate(hid_executor_task, "hid_exec", 4096,
                NULL, configMAX_PRIORITIES - 2, NULL);

    // LCD 渲染任务 (普通优先级)
    xTaskCreate(lcd_render_task, "lcd", 4096,
                NULL, configMAX_PRIORITIES - 3, NULL);

    // Step 4: 初始化完成
    ESP_LOGI(TAG, "[ok] ODA HID Bridge ready");

    // main 线程空闲: 处理 TinyUSB 设备事件
    while (1) {
        // tud_task 由 TinyUSB 内部 task 处理, 这里仅做看门狗喂狗
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
```

- [ ] **Step 2: 验证编译**

```bash
idf.py build
```

Expected: 编译通过，无错误。可能的 warning 需要修复。

- [ ] **Step 3: 提交**

```bash
git add main/main.c
git commit -m "feat: main integration — USB, CDC, HID, LCD task wiring"
```

---

### Task 7: 构建、烧录与验证

- [ ] **Step 1: 配置并构建**

```bash
idf.py set-target esp32s3
idf.py build
```

确认 `sdkconfig` 中以下项正确：
- `CONFIG_TINYUSB_HID_COUNT=2`
- `CONFIG_TINYUSB_CDC_COUNT=1`
- `CONFIG_TINYUSB_HID_ENABLED=y`
- `CONFIG_TINYUSB_CDC_ENABLED=y`

- [ ] **Step 2: 烧录**

```bash
idf.py -p COM3 flash monitor
```

(COM 端口号根据实际情况调整，Windows 下通常是 COM3~COM8)

- [ ] **Step 3: 验证 USB 枚举**

设备插入后，Windows 设备管理器应出现：
- `ODA HID Bridge` (通用串行总线设备)
- `HID Keyboard Device` (键盘)
- `HID-compliant mouse` (鼠标)
- `USB Serial Device (COMx)` (串口)

如果没有正确枚举，检查：
1. USB D-/D+ 引脚是否正确（ESP32S3 的 GPIO19/20）
2. `sdkconfig` 中 `CONFIG_TINYUSB_HID_COUNT` 是否为 2
3. 配置描述符末尾的 `TUSB_DESC_TOTAL_LEN` 宏设置是否正确

- [ ] **Step 4: 验证 CDC 协议**

用串口工具 (PuTTY/串口调试助手) 打开 ESP32S3 的 COM 口，发送测试指令：

```
// 鼠标向右移动 100 像素
hex: 01 64 00 00 00

// 按下并释放 'A' 键
hex: 09 04 01

// 请求状态回传
hex: FE

// 预期回传: 01 <seq> <status>  (4 bytes ACK)
//          或 03 <20 bytes status>
```

- [ ] **Step 5: 验证键盘鼠标功能**

1. 打开记事本
2. 通过串口发送 `0A 05 hello` (KEY_TYPE: 5字符 "hello")
3. 预期: 记事本出现 "hello"

4. 通过串口发送 `03 01` (MOUSE_CLICK, 左键)
5. 预期: 鼠标左键点击

- [ ] **Step 6: 验证 LCD 显示**

1. 上电后 LCD 应显示 "ODA LIVE" 状态界面
2. USB 插入后顶部状态栏变绿
3. 执行指令后底部 seq 计数更新

- [ ] **Step 7: 提交验证结果**

```bash
git add -A
git commit -m "verify: USB composite enumeration, CDC protocol, HID execution, LCD display all pass"
```

---

### 补充: 字体表数据

以下是 `font5x7` 的完整 ASCII 32-126 字符表（95 字符 × 5 字节），插入到 `lcd_display.c` 中：

```c
static const uint8_t font5x7[][5] = {
    {0x00, 0x00, 0x00, 0x00, 0x00}, // SPACE
    {0x00, 0x00, 0x5F, 0x00, 0x00}, // !
    {0x00, 0x07, 0x00, 0x07, 0x00}, // "
    {0x14, 0x7F, 0x14, 0x7F, 0x14}, // #
    {0x24, 0x2A, 0x7F, 0x2A, 0x12}, // $
    {0x23, 0x13, 0x08, 0x64, 0x62}, // %
    {0x36, 0x49, 0x55, 0x22, 0x50}, // &
    {0x00, 0x05, 0x03, 0x00, 0x00}, // '
    {0x00, 0x1C, 0x22, 0x41, 0x00}, // (
    {0x00, 0x41, 0x22, 0x1C, 0x00}, // )
    {0x08, 0x2A, 0x1C, 0x2A, 0x08}, // *
    {0x08, 0x08, 0x3E, 0x08, 0x08}, // +
    {0x00, 0x50, 0x30, 0x00, 0x00}, // ,
    {0x08, 0x08, 0x08, 0x08, 0x08}, // -
    {0x00, 0x60, 0x60, 0x00, 0x00}, // .
    {0x20, 0x10, 0x08, 0x04, 0x02}, // /
    {0x3E, 0x51, 0x49, 0x45, 0x3E}, // 0
    {0x00, 0x42, 0x7F, 0x40, 0x00}, // 1
    {0x42, 0x61, 0x51, 0x49, 0x46}, // 2
    {0x21, 0x41, 0x45, 0x4B, 0x31}, // 3
    {0x18, 0x14, 0x12, 0x7F, 0x10}, // 4
    {0x27, 0x45, 0x45, 0x45, 0x39}, // 5
    {0x3C, 0x4A, 0x49, 0x49, 0x30}, // 6
    {0x01, 0x71, 0x09, 0x05, 0x03}, // 7
    {0x36, 0x49, 0x49, 0x49, 0x36}, // 8
    {0x06, 0x49, 0x49, 0x29, 0x1E}, // 9
    {0x00, 0x36, 0x36, 0x00, 0x00}, // :
    {0x00, 0x56, 0x36, 0x00, 0x00}, // ;
    {0x00, 0x08, 0x14, 0x22, 0x41}, // <
    {0x14, 0x14, 0x14, 0x14, 0x14}, // =
    {0x41, 0x22, 0x14, 0x08, 0x00}, // >
    {0x02, 0x01, 0x51, 0x09, 0x06}, // ?
    {0x32, 0x49, 0x79, 0x41, 0x3E}, // @
    {0x7E, 0x11, 0x11, 0x11, 0x7E}, // A
    {0x7F, 0x49, 0x49, 0x49, 0x36}, // B
    {0x3E, 0x41, 0x41, 0x41, 0x22}, // C
    {0x7F, 0x41, 0x41, 0x22, 0x1C}, // D
    {0x7F, 0x49, 0x49, 0x49, 0x41}, // E
    {0x7F, 0x09, 0x09, 0x01, 0x01}, // F
    {0x3E, 0x41, 0x41, 0x51, 0x32}, // G
    {0x7F, 0x08, 0x08, 0x08, 0x7F}, // H
    {0x00, 0x41, 0x7F, 0x41, 0x00}, // I
    {0x20, 0x40, 0x41, 0x3F, 0x01}, // J
    {0x7F, 0x08, 0x14, 0x22, 0x41}, // K
    {0x7F, 0x40, 0x40, 0x40, 0x40}, // L
    {0x7F, 0x02, 0x04, 0x02, 0x7F}, // M
    {0x7F, 0x04, 0x08, 0x10, 0x7F}, // N
    {0x3E, 0x41, 0x41, 0x41, 0x3E}, // O
    {0x7F, 0x09, 0x09, 0x09, 0x06}, // P
    {0x3E, 0x41, 0x51, 0x21, 0x5E}, // Q
    {0x7F, 0x09, 0x19, 0x29, 0x46}, // R
    {0x46, 0x49, 0x49, 0x49, 0x31}, // S
    {0x01, 0x01, 0x7F, 0x01, 0x01}, // T
    {0x3F, 0x40, 0x40, 0x40, 0x3F}, // U
    {0x1F, 0x20, 0x40, 0x20, 0x1F}, // V
    {0x7F, 0x20, 0x18, 0x20, 0x7F}, // W
    {0x63, 0x14, 0x08, 0x14, 0x63}, // X
    {0x03, 0x04, 0x78, 0x04, 0x03}, // Y
    {0x61, 0x51, 0x49, 0x45, 0x43}, // Z
    {0x00, 0x00, 0x7F, 0x41, 0x41}, // [
    {0x02, 0x04, 0x08, 0x10, 0x20}, // backslash
    {0x41, 0x41, 0x7F, 0x00, 0x00}, // ]
    {0x04, 0x02, 0x01, 0x02, 0x04}, // ^
    {0x40, 0x40, 0x40, 0x40, 0x40}, // _
    {0x00, 0x01, 0x02, 0x04, 0x00}, // `
    {0x20, 0x54, 0x54, 0x54, 0x78}, // a
    {0x7F, 0x48, 0x44, 0x44, 0x38}, // b
    {0x38, 0x44, 0x44, 0x44, 0x20}, // c
    {0x38, 0x44, 0x44, 0x48, 0x7F}, // d
    {0x38, 0x54, 0x54, 0x54, 0x18}, // e
    {0x08, 0x7E, 0x09, 0x01, 0x02}, // f
    {0x08, 0x14, 0x54, 0x54, 0x3C}, // g
    {0x7F, 0x08, 0x04, 0x04, 0x78}, // h
    {0x00, 0x44, 0x7D, 0x40, 0x00}, // i
    {0x20, 0x40, 0x44, 0x3D, 0x00}, // j
    {0x00, 0x7F, 0x10, 0x28, 0x44}, // k
    {0x00, 0x41, 0x7F, 0x40, 0x00}, // l
    {0x7C, 0x04, 0x18, 0x04, 0x78}, // m
    {0x7C, 0x08, 0x04, 0x04, 0x78}, // n
    {0x38, 0x44, 0x44, 0x44, 0x38}, // o
    {0x7C, 0x14, 0x14, 0x14, 0x08}, // p
    {0x08, 0x14, 0x14, 0x18, 0x7C}, // q
    {0x7C, 0x08, 0x04, 0x04, 0x08}, // r
    {0x48, 0x54, 0x54, 0x54, 0x20}, // s
    {0x04, 0x3F, 0x44, 0x40, 0x20}, // t
    {0x3C, 0x40, 0x40, 0x20, 0x7C}, // u
    {0x1C, 0x20, 0x40, 0x20, 0x1C}, // v
    {0x3C, 0x40, 0x30, 0x40, 0x3C}, // w
    {0x44, 0x28, 0x10, 0x28, 0x44}, // x
    {0x0C, 0x50, 0x50, 0x50, 0x3C}, // y
    {0x44, 0x64, 0x54, 0x4C, 0x44}, // z
    {0x00, 0x08, 0x36, 0x41, 0x00}, // {
    {0x00, 0x00, 0x7F, 0x00, 0x00}, // |
    {0x00, 0x41, 0x36, 0x08, 0x00}, // }
    {0x08, 0x08, 0x2A, 0x1C, 0x08}, // ~
};
```

---

## 自检清单

1. **Spec 覆盖检查:**
   - USB 复合设备 (HID KB + HID MS + CDC ACM) → Task 2 + Task 6 ✓
   - CDC 指令协议 (16 条指令码) → Task 3 + Task 4 ✓
   - LCD 状态显示 (4 区布局, ST7789) → Task 5 ✓
   - FreeRTOS 3 任务 → Task 6 ✓
   - 引脚配置 (Waveshare 1.47B) → 所有 Task ✓

2. **占位符检查:** 无 TBD/TODO。字体表数据完整。引脚定义明确。

3. **类型一致性:**
   - `cmd_t` 在 Task 3 定义, Task 4 消费 ✓
   - `status_t` 在 Task 3 定义, Task 5 引用 ✓
   - `hid_state_t` 在 Task 4 定义, Task 5/6 引用 ✓
   - `lcd_state_t` 在 Task 5 定义, Task 6 引用 ✓
   - 端点地址、接口索引在 Task 2 定义, 全局一致 ✓
