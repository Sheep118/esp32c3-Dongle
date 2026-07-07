# BLE 蓝牙广播知识手册

> 本文档根据 ESP-IDF (Bluedroid) 官方 API 头文件整理，结合蓝牙核心规范 5.x。

---

## 1. BLE 广播类型 (Advertising Types)

### 1.1 五种广播类型

基于 ESP-IDF `esp_gap_ble_api.h` 中的 `esp_ble_adv_type_t` 枚举定义：

| 枚举值 | 数值 | 简称 | 可连接 | 可扫描 |
|--------|------|------|--------|--------|
| `ADV_TYPE_IND` | `0x00` | 通用广播 (Connectable Undirected) | ✅ | ✅ |
| `ADV_TYPE_DIRECT_IND_HIGH` | `0x01` | 定向广播-高速 (Directed, High Duty Cycle) | ✅ | ❌ |
| `ADV_TYPE_SCAN_IND` | `0x02` | 可扫描广播 (Scannable Undirected) | ❌ | ✅ |
| `ADV_TYPE_NONCONN_IND` | `0x03` | 不可连接广播 (Non-connectable) | ❌ | ❌ |
| `ADV_TYPE_DIRECT_IND_LOW` | `0x04` | 定向广播-低速 (Directed, Low Duty Cycle) | ✅ | ❌ |

### 1.2 各类型详解

#### ① `ADV_TYPE_IND` — 通用广播（可连接，非定向）
- **最常见**的类型，也是最完整的广播模式
- 任何扫描设备都能收到，收到后可以发起连接请求
- 收到扫描请求后必须回复扫描响应包（Scan Response）
- 广播周期：通常 20ms ~ 10.24s（可配置）
- **典型应用**：手机、穿戴设备、PC 蓝牙外设、蓝牙键鼠

#### ② `ADV_TYPE_DIRECT_IND_HIGH` — 定向广播（高速）
- **面向特定目标设备**，广播包中含有目标 MAC 地址
- 只对目标设备有效，其他设备收到后忽略
- 高速模式：每 **3.75ms** 发一次，最长持续 **1.28 秒**
- 不支持扫描请求/响应
- **典型应用**：快速重连（设备绑定了），快速配对

#### ③ `ADV_TYPE_DIRECT_IND_LOW` — 定向广播（低速）
- 与上一类型相同，但广播间隔和普通广播类似
- **典型应用**：设备在后台维持连接状态，低功耗重连

#### ④ `ADV_TYPE_SCAN_IND` — 可扫描广播
- 不可连接，但可以被扫描
- 收到扫描请求后必须回复扫描响应包
- 可以用扫描响应包来传递额外数据
- **典型应用**：需要暴露额外信息但不希望被连接的设备

#### ⑤ `ADV_TYPE_NONCONN_IND` — 不可连接不可扫描广播
- 纯单向发射，不能连接、不能扫描
- 能量利用最高效，只发不收
- 广播数据全部在 31 字节 AdvData 中
- **典型应用**：Beacon（iBeacon/Eddystone）、传感器广播数据、位置标签

### 1.3 广播间隔与数据包关系

```
广播间隔 (Advertising Interval) = 0.625ms × N  (N = 20 ~ 4000)
即：12.5ms ~ 2.5s

每个广播间隔内，发送 3 个广播包（3 个 BLE 信道：37/38/39）
```

| 场景 | 典型间隔 | 每秒广播包数 |
|------|---------|:----------:|
| 快速连接 | 20ms | ~150 |
| 常规 | 100ms | ~30 |
| 低功耗 | 1s | ~3 |

---

## 2. BLE 广播数据包格式（AD Structure）

### 2.1 基本结构

每个 AD Structure 由三部分组成：

```
┌──────────┬──────────┬─────────────────────┐
│ Length   │ AD Type  │ AD Data             │
│ (1 字节) │ (1 字节) │ (Length-1 字节)     │
└──────────┴──────────┴─────────────────────┘
```

整个广播数据包（AdvData / ScanRspData）就是多个 AD Structure 拼接，**总长度不超过 31 字节**（经典 BLE）。

### 2.2 示例：解析一个广播包

```
Hex: 02 01 06  03 03 0A 18  09 FF 4C 00 01 02 03
     └─────┘  └──────────┘  └───────────────────┘
     AD #1     AD #2         AD #3

AD #1: Length=2, Type=0x01 (Flags), Data=0x06
       → 06 = LE General Discoverable + BR/EDR Not Supported
       
AD #2: Length=3, Type=0x03 (16-bit UUID Complete), Data=0x180A
       → 完整 16-bit UUID: 0x180A (Device Information Service)

AD #3: Length=9, Type=0xFF (Manufacturer Data), Data=0x4C00010203
       → Company ID: 0x004C (Apple), 后跟自定义数据
```

### 2.3 常见 AD Type 完整列表

基于 ESP-IDF `ESP_BLE_AD_TYPE_xxx` 枚举定义：

| 值 | 常量 | 描述 | 常见用途 |
|:--:|------|------|---------|
| `0x01` | `FLAG` | 设备标志 | 必须项，定义广播模式 |
| `0x02` | `16SRV_PART` | 16-bit UUID 部分 | 不完整 UUID 列表 |
| `0x03` | `16SRV_CMPL` | 16-bit UUID 完整 | 标准 BLE Service UUID |
| `0x04` | `32SRV_PART` | 32-bit UUID 部分 | 较少见 |
| `0x05` | `32SRV_CMPL` | 32-bit UUID 完整 | 较少见 |
| `0x06` | `128SRV_PART` | 128-bit UUID 部分 | 自定义 Service |
| `0x07` | `128SRV_CMPL` | 128-bit UUID 完整 | 自定义 Service |
| `0x08` | `NAME_SHORT` | 短名称 | 设备简称 |
| `0x09` | `NAME_CMPL` | 完整名称 | 完整设备名 |
| `0x0A` | `TX_PWR` | 发射功率 | dBm 值（1 字节有符号） |
| `0x0D` | `DEV_CLASS` | 设备类别 | 经典蓝牙设备类别 |
| `0x10` | `SM_TK` | 配对临时密钥 | OOB 配对 |
| `0x11` | `SM_OOB_FLAG` | OOB 标志 | 配对方式指示 |
| `0x12` | `INT_RANGE` | 连接间隔范围 | 推荐连接参数 |
| `0x14` | `SOL_SRV_UUID` | 请求 16-bit UUID | 请求指定 Service 的连接 |
| `0x15` | `128SOL_SRV_UUID` | 请求 128-bit UUID | 同上 |
| `0x16` | `SERVICE_DATA` | 服务数据 | 16-bit UUID + 数据 |
| `0x17` | `PUBLIC_TARGET` | 公共目标地址 | 目标 MAC 列表 |
| `0x18` | `RANDOM_TARGET` | 随机目标地址 | 目标 MAC 列表 |
| `0x19` | `APPEARANCE` | 外观 | 设备图标/外形枚举 |
| `0x1A` | `ADV_INT` | 广播间隔 | 广播间隔建议值 |
| `0x1B` | `LE_DEV_ADDR` | LE 设备地址 | 地址类型 + MAC |
| `0x1C` | `LE_ROLE` | LE 角色 | 设备角色 |
| `0x1D` | `SPAIR_C256` | C256 配对值 | 安全配对 |
| `0x1E` | `SPAIR_R256` | R256 配对值 | 安全配对 |
| `0x24` | `URI` | URI 地址 | Eddystone-URI |
| `0xFF` | `MANUFACTURER` | **厂商自定义数据** | iBeacon / 自定义协议 |

### 2.4 AD Type 的广播包 vs 扫描响应包分布

```
广播包（AdvData，共 31B）：
├── Flags                       (0x01)  3B   ← 必须
├── Complete Name               (0x09)  N B   ← 可选，放不下时用 SHORT
├── TX Power Level              (0x0A)  2B   ← 可选
└── Manufacturer Specific Data  (0xFF)  N B   ← 自定义

扫描响应包（ScanRspData，共 31B）：
├── Complete Name               (0x09)  N B   ← 广播包放不下时放这里
├── 128-bit Service UUIDs       (0x07)  16B  ← Service 信息
└── Service Data                (0x16)  N B   ← 附加数据
```

**关键规则**：
- 广播包 **必须** 包含 `Flags`（0x01），这是唯一的强制项
- 扫描响应包 **不能** 包含 `Flags`
- 两个包合计 62 字节（2 × 31B）

---

## 3. 扫描响应包 (Scan Response)

### 3.1 何时发送？

扫描响应包**不是自动发送**的。触发流程：

```
┌─────────────┐      ┌─────────────┐      ┌─────────────┐
│  广播设备    │      │   扫描设备    │      │  广播设备    │
│ (Advertiser)│      │  (Scanner)   │      │ (Advertiser)│
└──────┬──────┘      └──────┬──────┘      └──────┬──────┘
       │                    │                    │
       │  广播包 (AdvData)   │                    │
       │───────────────────→│                    │
       │                    │                    │
       │       主动扫描设备发送扫描请求           │
       │  (SCAN_REQ)       │                    │
       │←─────────────────│                    │
       │                    │                    │
       │  扫描响应包         │                    │
       │  (ScanRspData)     │                    │
       │───────────────────→│                    │
       │                    │                    │
```

**条件**：
1. **仅 `ADV_TYPE_IND` 和 `ADV_TYPE_SCAN_IND`** 两种广播类型支持扫描响应
2. 扫描设备必须使用**主动扫描**（Active Scan），被动扫描不会发 `SCAN_REQ`
3. 定向广播和不可连接广播**不支持**扫描响应

### 3.2 典型用法

- **名称放扫描响应**：广播包含 Flags + 部分信息，名称放扫描响应里 → 只有主动扫描才看得见名称
- **隐私保护**：敏感数据放扫描响应里 → 被动扫描器看不到

---

## 4. BLE MAC 地址

### 4.1 两种地址类型

| 类型 | 标识 | 说明 |
|------|------|------|
| **Public Address** | 最高 2 位 = `00` | IEEE 分配的全球唯一地址，类似 MAC 地址 |
| **Random Address** | 最高 2 位 ≠ `00` | 设备自己生成，又分 4 种子类型 |

### 4.2 Public Address 格式约束

```
Public Address (48-bit):

Bit 47  46  45  ...    8   7   6   5   4   3   2   1   0
┌────┬────┬──────────────────────────────────────────────┐
│ 0  │ 0  │         由 IEEE 分配的组织唯一标识符           │
└────┴────┴──────────────────────────────────────────────┘
      ↑ 最高 2 位必须为 00
```

你设的 `AA:BB:CC:DD:EE:FF`：
```
AA = 1010 1010 → 最高 2 位 = 10 ❌ 不满足！这是"随机静态地址"
```

所以 Bluedroid 自动将其修正为 `AABBCCDDEE01`——第 0 字节被改写了：
```
FF = 1111 1111 → 最后 2 位为 11 → 被改写为 01 以满足规则
```

### 4.3 Public Address 的具体规则

根据 IEEE 802-2014 和蓝牙核心规范 5.x Vol 6 Part B §1.3：

| 约束 | 说明 |
|------|------|
| **最高字节 bit 1 = 0** | Universal/Local bit（0=IEEE 分配，1=本地管理） |
| **最高字节 bit 0 = 0** | Individual/Group bit（0=单播，1=多播） |

所以 `esp_base_mac_addr_set()` 会检查并强制 Public Address 的最高 2 位为 `00`。

### 4.4 如何在代码中验证

你使用的 `esp_base_mac_addr_set(mac)`：
- 如果最高 2 位 ≠ `00`，会被**静默改写**为合法的 Public Address
- FF → 高低两位被清零，变为 01 或 00

### 4.5 合法 Public Address 示例

| MAC | 合法？ | 说明 |
|-----|:---:|------|
| `AA:BB:CC:DD:EE:00` | ✅ | 最高字节 AA=10101010, bit0=0, bit1=0 ✅ |
| `AA:BB:CC:DD:EE:01` | ✅ | 同上（你看到的修正结果） |
| `AA:BB:CC:DD:EE:FF` | ❌ | FF=11111111, bit0=1, bit1=1 → 被改写 |
| `00:11:22:33:44:55` | ✅ | 00=00000000 ✅ |

---

## 5. 参考来源

- ESP-IDF SDK: `esp_gap_ble_api.h` (v5.x, 2024-06-06)
- Bluetooth Core Specification 5.x, Vol 6, Part B
- IEEE 802-2014 Standard for Local and Metropolitan Area Networks
