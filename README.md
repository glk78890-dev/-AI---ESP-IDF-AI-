# ESP32 小智 AI 对话助手

> 基于 ESP32 + DeepSeek API 的串口终端 AI 聊天机器人 —— 仅需一块 ESP32 核心板即可实现。

---

## 目录

- [1. 项目简介](#1-项目简介)
- [2. 硬件准备](#2-硬件准备)
- [3. 软件环境](#3-软件环境)
- [4. 项目结构](#4-项目结构)
- [5. 从零开始搭建](#5-从零开始搭建)
  - [5.1 创建 ESP-IDF 项目](#51-创建-esp-idf-项目)
  - [5.2 配置 WiFi](#52-配置-wifi)
  - [5.3 配置 DeepSeek API](#53-配置-deepseek-api)
  - [5.4 添加组件依赖](#54-添加组件依赖)
  - [5.5 编写代码](#55-编写代码)
- [6. 核心原理讲解](#6-核心原理讲解)
- [7. 编译与烧录](#7-编译与烧录)
- [8. 使用说明](#8-使用说明)
- [9. 常见问题与排查](#9-常见问题与排查)
- [10. 进阶优化](#10-进阶优化)

---

## 1. 项目简介

**小智（XiaoZhi）** 是一个运行在 ESP32 上的中文 AI 对话助手。它通过串口终端与用户交互，将用户输入的文字发送到 DeepSeek API（大语言模型），并将 AI 回复显示在终端上。

### 核心功能

| 功能 | 状态 |
|------|------|
| WiFi 连接 | ✅ 自动连接，支持断线重连 |
| 串口终端交互 | ✅ 支持中英文输入，退格删除 |
| DeepSeek API 调用 | ✅ HTTPS 安全连接 |
| 多轮对话记忆 | ✅ 环形缓冲区，最多 10 轮 |
| UART 噪声过滤 | ✅ 自动过滤控制字符和 UTF-8 非法字节 |
| 错误恢复 | ✅ 连续 3 次报错后提示检查配置 |

### 技术架构

```
┌──────────────────┐      HTTP POST (HTTPS)      ┌──────────────────┐
│   ESP32 开发板    │ ──────────────────────────→ │  DeepSeek API    │
│                  │ ←────────────────────────── │  api.deepseek.com│
│  ┌────────────┐  │      JSON Response          └──────────────────┘
│  │ chat_task  │  │
│  │ (FreeRTOS) │  │
│  └─────┬──────┘  │
│        │         │
│   getchar()      │
│   printf()       │
│        │         │
│  ┌─────┴──────┐  │
│  │  UART 串口  │  │
│  └─────┬──────┘  │
└────────┼─────────┘
         │ USB 串口线
    ┌────┴────┐
    │  电脑终端 │
    │ (VSCode) │
    └─────────┘
```

---

## 2. 硬件准备

| 物料 | 说明 | 数量 |
|------|------|------|
| ESP32 开发板 | 本项目使用 ESP32-WROOM-32 核心板 | 1 |
| Micro USB 数据线 | 用于烧录和串口通信 | 1 |
| 2.4G WiFi 网络 | ESP32 仅支持 2.4GHz WiFi | - |

> **无需任何音频模块、传感器或额外硬件**，纯文字对话只需 ESP32 核心板即可。

---

## 3. 软件环境

### 3.1 安装 ESP-IDF

本项目基于 **ESP-IDF v5.1.2**。推荐方式：

1. 下载 [ESP-IDF 离线安装器](https://dl.espressif.com/dl/esp-idf/)（Windows 推荐 v5.1.2）
2. 运行安装器，选择完整安装（包含工具链和 Python 环境）
3. 安装 VSCode 及 ESP-IDF 扩展

安装后验证：
```bash
# 在 ESP-IDF Command Prompt 中运行
idf.py --version
# 应输出: ESP-IDF v5.1.2
```

### 3.2 注册 DeepSeek API

1. 访问 [platform.deepseek.com](https://platform.deepseek.com) 注册账号
2. 创建 API Key（在控制台 → API Keys）
3. 免费额度：**100 万 tokens/月**（足够日常使用）

---

## 4. 项目结构

```
xiaozhi/
├── CMakeLists.txt              # 根 CMake，定义项目名
├── sdkconfig                   # Kconfig 配置（WiFi、DeepSeek 参数等）
├── main/
│   ├── CMakeLists.txt          # 组件注册 + 依赖声明
│   ├── Kconfig.projbuild       # menuconfig 菜单定义
│   └── station_example_main.c  # 全部业务代码（~500 行）
├── build/                      # 构建产物（自动生成）
└── .vscode/                    # VSCode 配置
```

**核心代码都在 `main/station_example_main.c` 这一个文件中**，按功能分为：
- WiFi 连接（事件驱动 + FreeRTOS 事件组）
- DeepSeek API 调用（HTTP POST + JSON 解析）
- 对话记忆管理（环形缓冲区）
- 串口终端交互（逐字符读取 + 噪声过滤）

---

## 5. 从零开始搭建

### 5.1 创建 ESP-IDF 项目

在 **ESP-IDF Command Prompt** 中：

```bash
# 拷贝 WiFi Station 示例作为起点
xcopy /E /I %IDF_PATH%\examples\wifi\getting_started\station xiaozhi
cd xiaozhi

# 设置目标芯片
idf.py set-target esp32

# 第一次构建（验证环境）
idf.py build
```

### 5.2 配置 WiFi

**方法一：通过 menuconfig（推荐）**
```bash
idf.py menuconfig
# 进入 Example Configuration → 设置 SSID 和 Password
```

**方法二：直接修改 sdkconfig**

搜索并修改以下两行：
```
CONFIG_ESP_WIFI_SSID="你的WiFi名"
CONFIG_ESP_WIFI_PASSWORD="你的WiFi密码"
```

> ⚠️ ESP32 只支持 **2.4GHz** WiFi，不支持 5GHz。

### 5.3 配置 DeepSeek API

**方法一：通过 menuconfig**
```bash
idf.py menuconfig
# Example Configuration → DeepSeek API Key → 填入你的 Key
```

**方法二：直接硬编码**

在 `main/station_example_main.c` 中（第 63 行）：
```c
#define DEEPSEEK_API_KEY  "sk-你的API密钥"
```

> ⚠️ **安全提示**：API Key 是敏感信息。如果代码要公开，请用 menuconfig 配置而不是硬编码。

**可选配置项**：

| 配置项 | 默认值 | 说明 |
|--------|--------|------|
| `DEEPSEEK_MODEL` | `deepseek-v4-flash` | 模型：v4-flash(快速) / v4-pro(推理) |
| `DEEPSEEK_MAX_HISTORY` | 10 | 对话记忆轮数 |
| `DEEPSEEK_MAX_TOKENS` | 512 | AI 回复最大 token 数 |

### 5.4 添加组件依赖

编辑 `main/CMakeLists.txt`：

```cmake
idf_component_register(SRCS "station_example_main.c"
                       INCLUDE_DIRS "."
                       REQUIRES esp_http_client json nvs_flash esp_wifi mbedtls)
```

**依赖说明**：

| 组件 | 用途 |
|------|------|
| `esp_wifi` | WiFi 连接 |
| `nvs_flash` | 非易失存储（WiFi 参数） |
| `esp_http_client` | HTTP/HTTPS 客户端 |
| `json` | cJSON 库，构建和解析 API 请求/响应 |
| `mbedtls` | TLS 加密 + 证书验证（`esp_crt_bundle.h` 所在组件） |

### 5.5 编写代码

完整代码见 `main/station_example_main.c`。以下是各部分的关键实现说明：

---

#### 5.5.1 WiFi 连接（事件驱动模式）

```c
// 创建事件组用于同步
static EventGroupHandle_t s_wifi_event_group;

// 事件回调：处理 WiFi 启动、断连、获取 IP
static void event_handler(void* arg, esp_event_base_t event_base,
                          int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();                                       // 开始连接
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT); // 连接成功
    }
}

// 初始化并等待连接完成
void wifi_init_sta(void) {
    // ... 配置 WiFi 参数 ...
    xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT,
                        pdFALSE, pdFALSE, portMAX_DELAY);  // 阻塞等待
}
```

#### 5.5.2 DeepSeek API 调用

DeepSeek API 兼容 OpenAI 格式，端点为：
```
POST https://api.deepseek.com/chat/completions
```

**请求 JSON 构建**（`build_request_json`）：
```c
cJSON *root = cJSON_CreateObject();
cJSON_AddStringToObject(root, "model", "deepseek-v4-flash");
cJSON_AddBoolToObject(root, "stream", false);   // 非流式，简化解析
cJSON_AddNumberToObject(root, "max_tokens", 512);

// 构建 messages 数组：system + 历史对话 + 当前用户输入
cJSON *messages = cJSON_AddArrayToObject(root, "messages");
// ... 添加 system message, history, user message ...
```

**HTTPS 请求发送**（`call_deepseek_api`）：
```c
esp_http_client_config_t config = {
    .url = "https://api.deepseek.com/chat/completions",
    .method = HTTP_METHOD_POST,
    .timeout_ms = 30000,
    .crt_bundle_attach = esp_crt_bundle_attach,  // 使用内置证书包
};
// 设置 Authorization 头
esp_http_client_set_header(client, "Authorization", "Bearer <API_KEY>");
esp_http_client_perform(client);  // 阻塞执行
```

**响应 JSON 解析**：
```c
cJSON *root = cJSON_Parse(response_buffer);
cJSON *content = cJSON_GetObjectItem(
    cJSON_GetObjectItem(
        cJSON_GetArrayItem(cJSON_GetObjectItem(root, "choices"), 0),
        "message"
    ), "content"
);
assistant_reply = strdup(content->valuestring);
```

#### 5.5.3 对话记忆（环形缓冲区）

```c
typedef struct {
    char role[16];       // "user" 或 "assistant"
    char *content;       // 堆分配的对话内容
} history_entry_t;

static history_entry_t s_history[MAX_HISTORY_ENTRIES];  // 20 条
static int s_history_count = 0;   // 已存储条数
static int s_history_start = 0;   // 环形缓冲区起始位置

// 添加消息（超过上限时覆盖最早的）
static void history_add(const char *role, const char *content) {
    // 缓冲区未满：追加到末尾
    // 缓冲区已满：覆盖最旧条目，移动起始指针
}
```

每次 API 调用时：
1. **调用前**：`history_add("user", user_input)` 保存用户输入
2. **请求中**：遍历环形缓冲区，将所有历史消息加入 `messages` 数组
3. **响应后**：`history_add("assistant", reply)` 保存 AI 回复

#### 5.5.4 串口终端交互

核心采用 **逐字符读取** 方式，解决 `fgets` 的 UART 噪声问题：

```c
while (1) {
    c = getchar();  // 阻塞等待一个字符

    // 1. 过滤控制字符（保留 Enter、Backspace）
    if (c < 32 && c != '\n' && c != '\r' && c != '\b') continue;

    // 2. 处理退格
    if (c == '\b' || c == 0x7f) { char_count--; continue; }

    // 3. 处理回车 → 执行对话
    if (c == '\n' || c == '\r') {
        // 过滤开头的 UTF-8 非法字节（UART 噪声）
        // 调用 DeepSeek API
        // 打印回复
    }

    // 4. 普通字符 → 追加到缓冲区 + 屏幕回显
    input_buffer[char_count++] = (char)c;
    putchar(c);
}
```

**关键设计决策**：

| 问题 | 解决方案 | 为何如此 |
|------|----------|----------|
| `fgets` 读取 UART 噪声 | 改用 `getchar()` 逐字符读 | `fgets` 读到空字符立即返回，导致疯狂循环 |
| 中文输入乱码 | 只过滤 ASCII 0-31，允许 128-255 通过 | UTF-8 中文字节的最高位为 1（128-255） |
| 输入前带 `�` | 过滤开头的非法 UTF-8 字节 | UART RX 引脚空闲时的噪声字节 |
| 屏幕回显乱码 | 逐字节 `putchar` | 终端按字节渲染，不完整 UTF-8 序列显示为 `�`，`[xxx]` 确认行会正确显示 |

---

## 6. 核心原理讲解

### 6.1 整体流程

```
app_main()
  ├─ nvs_flash_init()         // 初始化 NVS（存储 WiFi 参数）
  ├─ wifi_init_sta()          // 连接 WiFi（阻塞等待）
  │    ├─ esp_netif_init()    // TCP/IP 协议栈
  │    ├─ esp_wifi_init()     // WiFi 驱动
  │    ├─ 注册事件回调       // 监听连接状态
  │    └─ xEventGroupWaitBits // 等待连接成功信号
  └─ xTaskCreate(chat_task)   // 创建对话任务

chat_task()                   // FreeRTOS 任务（无限循环）
  └─ while(1):
       ├─ getchar()           // 等待串口输入
       ├─ 过滤噪声字节
       ├─ 收集完整输入行
       └─ call_deepseek_api() // 调用 API 并打印回复
            ├─ history_add("user", ...)     // 保存用户输入
            ├─ build_request_json()         // 构建请求 JSON
            ├─ esp_http_client_perform()    // HTTPS POST
            ├─ cJSON_Parse()                // 解析响应
            └─ history_add("assistant", ...)// 保存 AI 回复
```

### 6.2 FreeRTOS 任务模型

- **main_task**（优先级 1）：执行 `app_main()`，WiFi 连接后创建 `chat_task`，然后退出
- **chat_task**（优先级 5）：无限循环，负责串口读取和 API 调用
- **wifi 驱动任务**（优先级 23）：处理 WiFi 协议栈，由 ESP-IDF 自动创建

### 6.3 内存管理

ESP32 有 ~292KB 可用 DRAM（本项目配置下）。内存分配策略：

| 数据 | 存储位置 | 大小 |
|------|----------|------|
| `input_buffer` | chat_task 栈 | 512B |
| `response_buffer` | BSS（全局） | 4096B |
| `s_history[].content` | 堆（malloc） | 每个 ~512B |
| `request_body`（JSON） | 堆 | ~2-5KB |
| chat_task 栈 | FreeRTOS 栈 | 10,240B |
| TLS 握手缓冲 | 堆 | ~30-40KB |

---

## 7. 编译与烧录

### 7.1 在 VSCode 中操作（推荐）

| 操作 | 快捷键 | 说明 |
|------|--------|------|
| 配置项目 | `Ctrl+E` → SDK Configuration editor | 设置 WiFi、API Key 等 |
| 编译 | `Ctrl+E B` | 构建固件 |
| 烧录 | `Ctrl+E F` | 烧录到 ESP32 |
| 串口监控 | `Ctrl+E M` | 查看输出、输入对话 |
| 编译+烧录+监控 | `Ctrl+E F`（选 Flash） | 一键完成 |

### 7.2 命令行操作

```bash
# 在 ESP-IDF Command Prompt 中

# 配置（可选）
idf.py menuconfig

# 编译
idf.py build

# 烧录 + 监控（COM7 为串口号，根据实际情况修改）
idf.py -p COM7 flash monitor

# 退出监控：Ctrl + ]
```

### 7.3 首次运行

烧录后，串口终端会显示：

```
I (xxxx) xiaozhi: connected to ap SSID:你的WiFi名
I (xxxx) xiaozhi: WiFi connected, starting chat task...

========================================
       小智 AI 助手 (XiaoZhi)
  基于 DeepSeek deepseek-v4-flash
========================================
输入文字开始对话，输入 'quit' 退出
----------------------------------------

你:
```

此时在终端输入文字，按回车即可与 AI 对话。

---

## 8. 使用说明

### 8.1 基本操作

| 操作 | 方式 |
|------|------|
| 输入文字 | 直接打字，按回车发送 |
| 删除字符 | 按 Backspace |
| 退出对话 | 输入 `quit` 并回车 |

### 8.2 输出说明

```
你: 你好[你好]                        ← [xxx] 为确认行，显示实际捕获的内容
小智: 你好！有什么可以帮你的吗？      ← AI 回复
```

### 8.3 对话示例

```
你: 我叫小明[我叫小明]
小智: 你好小明！很高兴认识你！

你: 我叫什么名字[我叫什么名字]
小智: 你刚才告诉我你叫小明呀！            ← 能记住上下文！
```

---

## 9. 常见问题与排查

### 9.1 编译错误

#### `Failed to resolve component 'esp_tls'`

**原因**：ESP-IDF v5.1 中组件名为 `esp-tls`（连字符），且被 `esp_http_client` 自动依赖。

**解决**：从 `main/CMakeLists.txt` 的 `REQUIRES` 中删除 `esp_tls`，添加 `mbedtls`。

```cmake
# 错误
REQUIRES esp_http_client json nvs_flash esp_wifi esp_tls)

# 正确
REQUIRES esp_http_client json nvs_flash esp_wifi mbedtls)
```

#### `fatal error: esp_crt_bundle.h: No such file or directory`

**原因**：`esp_crt_bundle.h` 在 `mbedtls` 组件中。

**解决**：在 `REQUIRES` 中添加 `mbedtls`。

#### `未定义标识符 "CONFIG_DEEPSEEK_API_KEY"`

**原因**：IntelliSense 没有读取到更新后的 `sdkconfig.h`。

**解决**：
1. 运行一次 `idf.py menuconfig`（或直接 build），让 CMake 重新生成 `sdkconfig.h`
2. 或重新加载 VSCode 窗口：`Ctrl+Shift+P` → `Reload Window`

### 9.2 运行问题

#### `Returned from app_main()` 后没有聊天界面

**原因**：烧录了旧固件。

**解决**：重新 `Ctrl+E B` 编译 + `Ctrl+E F` 烧录。确认编译时间是否正确。

#### 串口疯狂刷 `你: 你: 你: 你:`

**原因**：`fgets` 读到 UART 噪声/空字符后立即返回，循环空转。

**解决**：已改用 `getchar()` 逐字符读取 + 控制字符过滤。

#### 输入中文后显示乱码 `������`

**原因**：逐字节 `putchar` 回显时，UTF-8 多字节序列被终端逐个渲染为 `�`。

**说明**：这是纯显示问题，`[xxx]` 确认行和实际 API 请求中的中文是正确的。

#### 中文字符无法输入

**原因**：字符过滤规则太严格，过滤掉了 UTF-8 高位字节（128-255）。

**解决**：确认过滤规则只拒绝 0-31 范围的控制字符：
```c
if (c < 32 && c != '\n' && c != '\r' && c != '\b') continue;
```

#### `HTTP error 400: invalid unicode code point`

**原因**：输入开头带有非法 UTF-8 字节（UART 噪声）。

**解决**：已添加开头过滤逻辑，自动跳过 0x80-0xBF、0xC0、0xC1、0xF5+ 等非法起始字节。

#### `HTTP error 401/403`

**原因**：API Key 错误或过期。

**解决**：检查 `DEEPSEEK_API_KEY` 是否正确，去 [platform.deepseek.com](https://platform.deepseek.com) 重新生成。

#### `HTTP error 429`

**原因**：超出速率限制（免费版 5 QPS）。

**解决**：降低对话频率，或等待配额恢复。

#### AI 不记得之前的对话

**原因**：请求中没有携带对话历史。

**解决**：已实现环形缓冲区历史管理，每次 API 调用携带完整历史。

#### AI 编造日期/无法获取实时信息

**原因**：DeepSeek 是纯语言模型，**没有联网能力**，无法获取实时信息。

**解决**：已在 System Prompt 中明确告知模型诚实承认限制。如需实时信息，需要后续实现 Function Calling 调用外部 API。

### 9.3 Windows 特定问题

#### `idf.py menuconfig` 打不开

**原因**：在 Git Bash 中运行 ESP-IDF 命令会失败。

**解决**：
1. 在 VSCode 中：`Ctrl+E` → SDK Configuration editor
2. 或在 Windows 开始菜单搜 "ESP-IDF Command Prompt"

#### `MSys/Mingw is not supported`

**原因**：Git Bash 不被 ESP-IDF 的构建系统支持。

**解决**：使用 **ESP-IDF Command Prompt** 或 **VSCode 集成终端**（非 Git Bash）。

---

## 10. 进阶优化

当前实现是**最简可用版本**。以下是可选的优化方向：

### 10.1 功能增强

| 方向 | 实现思路 |
|------|----------|
| **语音输入** | 添加 I2S 麦克风（如 INMP441），接入百度/讯飞 ASR 语音识别 API |
| **语音输出** | 添加 I2S 功放（如 MAX98357A），接入百度/讯飞 TTS 或 Edge TTS |
| **唤醒词** | 本地 VAD（语音活动检测）+ 关键词唤醒 |
| **实时信息** | 通过 DeepSeek Function Calling 调用天气/新闻等外部 API |
| **OLED 显示** | SSD1306 I2C 屏幕显示对话内容或表情动画 |
| **按键触发** | GPIO 按键实现"按住说话"交互模式 |
| **LED 状态** | GPIO 控制 LED 指示连接/思考/回复等状态 |

### 10.2 性能优化

| 方向 | 思路 |
|------|------|
| **流式输出** | 设置 `"stream": true`，用 SSE 解析实现逐 token 打字效果 |
| **token 缓存** | 利用 DeepSeek 的 context caching 减少重复 system prompt 的 token 开销 |
| **异步 HTTP** | 使用非阻塞模式，避免 chat_task 长时间挂起 |

### 10.3 生产化

| 方向 | 思路 |
|------|------|
| **API Key 安全** | 使用 NVS 加密存储替代硬编码 |
| **OTA 升级** | 通过 WiFi 远程更新固件 |
| **Web 配网** | SmartConfig 或 Captive Portal 配网，替代硬编码 WiFi 密码 |

---

## 附录 A：完整文件清单

| 文件 | 行数 | 说明 |
|------|------|------|
| `main/station_example_main.c` | ~522 | 全部业务代码 |
| `main/CMakeLists.txt` | 3 | 组件注册 |
| `main/Kconfig.projbuild` | 106 | menuconfig 菜单 |
| `CMakeLists.txt` | 6 | 根项目定义 |
| `sdkconfig` | ~1850 | Kconfig 配置输出文件 |

## 附录 B：DeepSeek API 关键参数

| 参数 | 值 | 说明 |
|------|-----|------|
| 端点 | `https://api.deepseek.com/chat/completions` | |
| 认证 | `Authorization: Bearer <key>` | |
| 模型 | `deepseek-v4-flash` | 快速模型，响应延迟 2-8 秒 |
| 模型 | `deepseek-v4-pro` | 推理模型，更强的思考能力 |
| 免费额度 | 1,000,000 tokens/月 | 约 50 万次简单对话 |
| 速率限制 | 5 QPS | 每秒最多 5 次请求 |

---

*最后更新：2026 年 6 月*
