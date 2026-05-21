# ESP32S3 ODA HID — 系统设计规格

日期: 2026-05-20
状态: 已确认

## 1. 概述

将 ESP32S3 开发板（带 1.44 寸 172×320 ST7789 LCD）打造成一个 USB 复合 HID 设备（键盘 + 鼠标 + 虚拟串口），配合 PC 端 ODA（观察-决策-行动）引擎，通过 VLM 驱动的 4 级图像缩放金字塔实现通用屏幕自动化 RPA。

### 1.1 硬件规格

| 项目 | 规格 |
|------|------|
| MCU | ESP32S3 (USB-OTG) |
| LCD 驱动 | ST7789 |
| LCD 分辨路 | 172×320 (竖屏) |
| LCD 色深 | 262K (18-bit) |
| LCD 接口 | 3 线 SPI |
| 触摸 | 无 |

### 1.2 系统全景

```
Cloud: Playbook Registry / Results Collector / YOLO Training Pipeline
    │
    ▼ HTTP (按需)
PC: ODA Engine (截屏 → VLM 金字塔 → 状态机 → HID 指令队列)
    │
    ▼ USB CDC
ESP32S3: HID Keyboard + HID Mouse + CDC ACM + LCD Status Display
```

## 2. 架构分层

### 2.1 模块边界

| 模块 | 位置 | 职责 |
|------|------|------|
| ESP32S3 固件 | 本仓库 | HID 键鼠执行、LCD 状态渲染、CDC 协议解析 |
| ODA Engine | PC (Python) | 截屏、图像金字塔、VLM 调用、Playbook 状态机、HID 指令生成 |
| YOLO 旁路 | PC 本地推理 | 页面特征快速检测，替代 VLM 热路径 |
| 训练管线 | 云端 | 语料积累 → YOLO fine-tune → 权重分发 |
| Playbook 服务 | 云端 Web | Playbook CRUD、结果收集、模型权重托管 |

### 2.2 当前实现范围

本仓库仅包含 **ESP32S3 固件**。PC 端和云端模块在后续阶段实现。

## 3. ODA Engine 设计（架构上下文）

### 3.1 ODA Loop

```
IDLE → OBSERVE(480p) → DECIDE(Playbook匹配) → ACT(HID指令) → VERIFY(waypoint对比)
```

- 频率: ~1-2 Hz（受 VLM API 延迟约束）
- 480p 帧用于全局空间索引和 waypoint 匹配
- 960p 帧用于窗口内 UI 元素定位
- 1920p 帧仅用于裁剪区域精细文本提取
- 3840p 不纳入常规管线（分辨率过剩）

### 3.2 图像金字塔数据结构

**480p 层 — 空间索引（Qwen 输出）**
```json
{
  "layer": 480,
  "windows": [{
    "id": "w1",
    "bbox": [0, 0, 120, 270],
    "label": "desktop",
    "exe": "explorer.exe"
  }, {
    "id": "w2",
    "bbox": [120, 0, 480, 270],
    "label": "browser_window",
    "exe": "chrome.exe",
    "features": ["tab_bar", "address_bar", "content_area"]
  }]
}
```

**960p 层 — 窗口内 UI Tree + Waypoint**
```json
{
  "layer": 960,
  "parent_window": "w2",
  "ui_tree": {
    "id": "chrome_content",
    "type": "web_page",
    "bbox": [0, 80, 960, 540],
    "children": [
      { "id": "e1", "type": "input_box", "bbox": [300, 20, 700, 60], "state": "focused" },
      { "id": "e2", "type": "result_list", "bbox": [200, 100, 800, 500], "children": [] }
    ]
  },
  "waypoint": { "node_id": "e1", "description": "search box, cursor blinking" }
}
```

**1920p 层 — 精细文本提取（Seed 2.0 Lite）**
```json
{
  "layer": 1920,
  "crop_source": "r1",
  "content": [{
    "type": "text",
    "fields": { "title": "...", "url": "...", "snippet": "..." }
  }]
}
```

### 3.3 Playbook 协议

```json
{
  "playbook_id": "chrome_google_search_v1",
  "app": "chrome.exe",
  "waypoints": {
    "google_home": {
      "480_hint": "white page with centered search box",
      "ui_signature": ["input_box:search", "button:Google Search"],
      "yolo_model": "chrome/google_search.yolo"
    },
    "results_page": {
      "480_hint": "search results below text box",
      "ui_signature": ["input_box:search", "result_list"],
      "parent": "google_home"
    }
  },
  "steps": [{
    "id": "search",
    "from_waypoint": "google_home",
    "to_waypoint": "results_page",
    "trigger_element": { "type": "input_box", "label": "search" },
    "extract": {
      "target": "result_list",
      "fields": ["title", "url", "snippet"],
      "pagination": { "element": "next_button", "max_pages": 5 }
    }
  }]
}
```

### 3.4 VLM 选择（通过 OpenRouter）

| 模型 | 用途 | 输入分辨率 |
|------|------|-----------|
| Qwen 2.X 8B | 低分辨率空间推理、窗口定位、UI Tree 生成 | 480p, 960p |
| Seed 2.0 Lite | 高分辨率文本提取 | 1920p 裁剪区域 |

### 3.5 YOLO 训练旁路

- 粒度: 每个站点/应用一个 YOLO 模型（如 `google.com.yolo`）
- VLM 标注产出自动入语料库（截图 + bbox + 标签）
- 每个 waypoint 累计 N 条语料后触发 YOLO fine-tune
- 训练在云端执行，权重文件 < 10MB，通过 Playbook 服务分发
- 推理在 PC CPU 上运行（YOLOv8n, < 10ms per 480p frame）
- 成熟页面的 VLM 调用率可降至 5% 以下

## 4. ESP32S3 固件设计

### 4.1 USB 复合设备

使用 TinyUSB 实现，一个 USB 口同时暴露三个功能：

| 接口 | 协议 | 说明 |
|------|------|------|
| HID Keyboard | Boot Protocol | 键盘输入，带 Report ID |
| HID Mouse | Report Protocol | 相对/绝对移动 + 5 键 + 滚轮 |
| CDC ACM | 虚拟串口 | PC ↔ ESP32S3 指令通道 |

### 4.2 固件模块

```
main
  ├── cdc_cmd_task    CDC 指令接收 + 解析 + Ring Buffer
  ├── hid_exec_task    HID 指令出队 + TinyUSB API 调用
  └── lcd_render_task  LVGL 或直接 framebuffer 渲染 (~10fps)
```

### 4.3 通信协议（CDC 下行: PC → ESP32S3）

Little Endian 字节序。1 字节指令码 + 变长 payload。

```
0x01  MOUSE_MOVE       int16 dx, int16 dy            (5 bytes)
0x02  MOUSE_MOVE_TO    uint16 x, uint16 y            (5 bytes, HID绝对坐标 0-32767)
0x03  MOUSE_CLICK      uint8 button                   (2 bytes, 1=左 2=右 4=中)
0x04  MOUSE_PRESS      uint8 button                   (2 bytes)
0x05  MOUSE_RELEASE    uint8 button                   (2 bytes)
0x06  MOUSE_SCROLL     int8 delta                     (2 bytes)
0x07  KEY_PRESS        uint8 hid_code                  (2 bytes)
0x08  KEY_RELEASE      uint8 hid_code                  (2 bytes)
0x09  KEY_TAP          uint8 hid_code, uint8 count    (3 bytes)
0x0A  KEY_TYPE         uint8 len, char[len]           (2+N bytes, ASCII→HID转换在ESP32端)
0x0B  SYNC             uint16 seq_id                  (3 bytes, 同步帧)
0x0C  NOOP             uint16 delay_ms                (3 bytes)
0xFD  LED_SET          uint8 r, uint8 g, uint8 b      (4 bytes, LCD背光色)
0xFE  ACK_REQUEST                                      (1 byte, 请求ESP32回传状态)
0xFF  RESET                                            (1 byte, 软复位)
```

### 4.4 通信协议（CDC 上行: ESP32S3 → PC）

```
0x01  ACK              uint16 seq_id, uint8 status    (4 bytes)
0x03  STATUS           struct status_t (20 bytes)     (21 bytes)
0xFF  ERROR            uint8 code                     (2 bytes)
```

### 4.5 LCD 状态显示布局

172×320 竖屏，深色主题 + 高饱和度色彩：

```
  ┌───────────────────┐
  │   O D A  LIVE     │  状态栏 (绿=运行 蓝=等待 黄=错误)
  │                   │
  │   CONNECTED       │
  │   step: search    │  任务信息区
  │   target: google  │  当前 step / waypoint / VLM
  │   vlm: qwen_480   │  调用次数 / 耗时 / 进度条
  │   [████░░] 78%    │
  │                   │
  │  ┌───────────┐    │
  │  │  chrome    │    │  App Card (当前应用图标+名称)
  │  └───────────┘    │
  │                   │
  │  yolo:● seq:0042  │  底部状态条
  └───────────────────┘
```

### 4.6 技术选型

| 组件 | 选择 |
|------|------|
| 框架 | ESP-IDF (v5.1+) |
| USB 协议栈 | TinyUSB (ESP-IDF 内置) |
| LCD 驱动 | TFT_eSPI 或 LovyanGFX (ST7789 支持) |
| 显示框架 | LVGL (轻量 UI) 或裸 framebuffer |
| RTOS | FreeRTOS (ESP-IDF 内置) |
| 编译工具 | idf.py / CMake |

## 5. 后续扩展方向

- ESP32S3 主导模式: 设备自主触发 RPA 任务（游戏挂机、多机管理）
- 物理按键交互: GPIO 按钮触发预设 Playbook
- Wi-Fi 直连云端: 设备不依赖 PC，直接联网获取 Playbook
- 多机管理: 一个 Cloud Panel 控制多个 ESP32S3 节点

## 6. PC 端地址映射约定

- VLM 返回像素坐标 (0-1920, 0-1080)
- PC 端负责缩放为 HID 绝对坐标 (0-32767)
- ESP32S3 固件只处理 HID 坐标，不做屏幕尺寸计算
- 约束: 强制全屏 + 主流分辨率 (1920×1080 / 1920×1200)
