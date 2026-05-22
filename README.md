# ESP32 Hand of Agent

ESP32S3 USB 复合 HID 固件 — 键盘 + 鼠标 + CDC 虚拟串口 + ST7789 LCD。

配合 PC 端 ODA（观察-决策-行动）引擎使用，实现 AI 驱动的屏幕自动化操控。

## 项目结构

```
├── README.md              ← 本文件
├── cli/                   ← 使用工具（键鼠控制）
│   ├── oda_cli.py         ← 键鼠控制命令行
│   └── skills/            ← Claude Code skill 定义
│       └── oda-hid.md
├── tools/                 ← 构建工具
│   ├── png_to_rgb565.py   ← PNG → 字模转换
│   └── prep_images.py     ← 批量图片 → storage.bin
├── assets/                ← 素材图片 (172×320 PNG)
├── main/                  ← ESP32S3 固件源码
│   ├── main.c
│   ├── hid_executor.c     ← HID 指令执行引擎
│   ├── lcd_display.c      ← ST7789 显示驱动
│   ├── cdc_protocol.c     ← CDC 二进制协议解析
│   └── usb_descriptors.c  ← USB 复合设备描述符
├── partitions.csv         ← 闪存分区表
├── sdkconfig.defaults     ← ESP-IDF 默认配置
└── build.sh               ← 一键构建 & 烧录
```

## 硬件

| 开发板 | Waveshare ESP32-S3-LCD-1.47 (USB-A 直插) |
|--------|-------------------------------------------|
| MCU | ESP32S3 (USB-OTG, 8MB PSRAM, 16MB Flash) |
| LCD | ST7789, 172×320, 3 线 SPI |

| 信号 | GPIO |
|------|------|
| LCD MOSI | 45 |
| LCD SCLK | 40 |
| LCD CS | 42 |
| LCD DC | 41 |
| LCD RST | 39 |
| LCD BL | 48 |
| BOOT | 0 (上拉, 低有效) |
| USB D-/D+ | 19 / 20 |

## USB 复合设备

一个 USB 口同时暴露：

| 接口 | 说明 |
|------|------|
| CDC ACM | 虚拟串口 (PC ↔ ESP32 指令通道) |
| HID (Report ID 1) | 键盘 (Boot Protocol) |
| HID (Report ID 2) | 鼠标 (5 键 + 滚轮) |

设备识���为 `VID:303A PID:80D1`，产品名 "ODA HID Bridge"。

## 构建 & 烧录

### 依赖

- ESP-IDF v5.4+
- Python 3.12

### 编译 & 烧录

```bash
# 构建
bash build.sh build

# 烧录 (含所有分区: bootloader + 分区表 + 固件 + 图片存储)
bash build.sh flash -p COM9
```

烧录前需要手动进入下载模式：按住 **BOOT** → 按 **RESET** 松开 → 松开 **BOOT**。屏幕会黑屏，Windows 设备管理器出现 COM 端口。

烧录完成后设备自动复位，重新枚举为 "ODA HID Bridge"。CDC 串口通常出现在 COM10。

## 功能

- **BOOT 按钮**：按下切换 LCD 方向（180° 旋转）
- **随机启动画面**：开机从 flash 存储分区随机加载图片显示
- **Splash 图片**：`assets/` 下放 172×320 PNG，构建时自动打包进 storage.bin

## 键鼠控制

通过 CDC 串口发送二进制协议指令。Python CLI 封装了所有操作：

```bash
# 打字
python cli/oda_cli.py --port COM10 type "hello world"

# 鼠标移动 (相对)
python cli/oda_cli.py --port COM10 move 100 0     # 右移 100px
python cli/oda_cli.py --port COM10 move 0 -50     # 上移 50px

# 鼠标点击
python cli/oda_cli.py --port COM10 click left     # 左键单击
python cli/oda_cli.py --port COM10 click right    # 右键单击

# 键盘按键
python cli/oda_cli.py --port COM10 tap enter      # 敲一下回车
python cli/oda_cli.py --port COM10 combo ctrl c   # Ctrl+C

# 滚轮
python cli/oda_cli.py --port COM10 scroll 3       # 向下滚 3 行

# 延时
python cli/oda_cli.py --port COM10 led 0 255 0    # 设置 LED 绿
```

## CDC 二进制协议

1 字节指令码 + 变长 payload，小端字节序。完整帧格式见 `cdc_protocol.h`。

| 指令码 | 功能 | Payload |
|--------|------|---------|
| `0x01` | 鼠标相对移动 | int16 dx, int16 dy |
| `0x02` | 鼠标绝对移动 | uint16 x, uint16 y |
| `0x03` | 鼠标点击 | uint8 button |
| `0x04` | 鼠标按下 | uint8 button |
| `0x05` | 鼠标释放 | uint8 button |
| `0x06` | 鼠标滚轮 | int8 delta |
| `0x07` | 键盘按下 | uint8 hid_code |
| `0x08` | 键盘释放 | uint8 hid_code |
| `0x09` | 键盘敲击 | uint8 hid_code, uint8 count |
| `0x0A` | 输入字符串 | uint8 len, char[len] |
| `0x0B` | 同步帧 | uint16 seq_id |
| `0x0C` | 延时 | uint16 ms |
| `0xFF` | 软复位 | (无) |

## 已知问题

- `tinyusb_config_t` **必须**是 `app_main()` 内的栈上局部变量，不能是全局变量（否则 boot loop）
- TinyUSB 与 USB Serial/JTAG 共用 GPIO19/20，不能同时启���（`CONFIG_USJ_ENABLE_USB_SERIAL_JTAG=n`）

## License

MIT
