# BLE MAC 地址完整规范

> 基于 Bluetooth Core Specification 5.x Vol 6 Part B §1.3 和 IEEE 802-2014

---

## 1. MAC 地址的 48 位结构

BLE 设备地址 48 位（6 字节），在蓝牙广播包中按**小端序**传输：

```
发送顺序（空中）:   LSB ............................. MSB
HEX 表示:          AA:BB:CC:DD:EE:FF
                    ↑           ↑
                    bit0        bit47
                    先发字节    最后字节
```

### 1.1 关键位定义

| 位 | 位置 | 含义 |
|:--:|------|------|
| **bit0** | AA 的最低有效位 | 单播(0) / 多播(1) |
| **bit1** | AA 的次低位 | Universal(0) / Local(1) |
| **bit2-7** | AA 的高 6 位 | Company ID 的最高 6 位 |
| **bit8-47** | BB~FF | Company ID 低位 + Company Assigned |

### 1.2 单播 vs 多播

```
bit0 = 0: 单播地址 (Unicast)
  → 唯一标识一个设备，全球唯一
  → 正常蓝牙设备都用这个

bit0 = 1: 多播地址 (Multicast/Group)
  → 标识一组设备
  → 极少使用，一般只有 Mesh 网络用到
```

**这是 `03:bb:cc:dd:ee:03` 被拒绝的原因**——`03` 的 bit0=1，系统判定为多播地址，拒绝设置。

---

## 2. Public vs Random 地址

最高字节（FF 位）的 **bit7-6** 决定地址类型：

| bit7-6 | 类型 | 说明 |
|:------:|------|------|
| `00` | **Public Device Address** | IEEE 注册的全球唯一地址 |
| `01` | **Static Random Address** | 设备自行生成，上电期间不变 |
| `10` | **Private Resolvable Address** | 可解析隐私地址（需 IRK 绑定） |
| `11` | **Private Non-Resolvable Address** | 不可解析隐私地址（定期更换） |

### 2.1 Public Address

```
FF = 0x00 ~ 0x3F
```

- IEEE 向厂商分配 Organizationally Unique Identifier (OUI，前 3 字节)
- 后 3 字节由厂商自行分配
- ESP32-C3 的默认 MAC 是 Public Address
- `esp_base_mac_addr_set()` 只能设置 Public Address

### 2.2 Static Random Address

```
FF = 0x40 ~ 0x7F  (bit7-6 = 01)
```

- 设备随机生成，必须满足：
  - **bit1-0 ≠ 00**（至少有一位置 1）
  - 其余 46 位随机（不能全 0 或全 1）
- 上电期间保持不变，重启后可重新生成
- `esp_ble_gap_set_rand_addr()` 设置

### 2.3 Private Address

```
FF = 0x80 ~ 0xFF  (bit7-6 = 10 或 11)
```

- **Resolvable**：用 IRK (Identity Resolving Key) 和随机数生成，可由绑定过的设备解析
- **Non-Resolvable**：完全随机，无法追溯到具体设备
- 定期更换（通常每 15 分钟）实现隐私保护

---

## 3. 字节序

### 3.1 空中传输（小端）

BLE 广播包中各字段按**小端序**（Little-Endian）传输：

```
Company ID 0x04D9 = Apple (TI)
空中顺序: D9 04
  → 第一个字节 = 0xD9 (低位)
  → 第二个字节 = 0x04 (高位)
```

MAC 地址同理：
```
MAC = AA:BB:CC:DD:EE:FF
空中传输: AA BB CC DD EE FF  (AA 先发，FF 后发)
```

### 3.2 厂商数据中的 Company ID

广播包 `0xFF` (Manufacturer Data) 的前 2 字节是 Company ID：

```
广播数据: FF 06 00 01 09 20 02 ...
                   └─┘ Company ID 0x0006 (Microsoft)
```

**注意**：Company ID 也是小端序！

### 3.3 代码中的处理

```cpp
// 从广播包原始数据中提取 Company ID (小端序 → 主机序)
uint16_t devCompanyId = (uint8_t)manufRaw[0] | ((uint8_t)manufRaw[1] << 8);
// 如果 raw = [06, 00, ...]  → devCompanyId = 0x0006
```

---

## 4. 在代码中的实际影响

### 4.1 `esp_base_mac_addr_set()` 的限制

- 只能设置 **Public Address**
- 必须满足：
  - bit47-46 = `00` (最后一个字节最高 2 位)
  - bit0 = `0` (第一个字节最低位)
- 返回 `ESP_ERR_INVALID_ARG (0x102 = 258)` 表示不合法

### 4.2 `esp_ble_gap_set_rand_addr()` 的限制

- 设置 **Random Address**（Static / Private）
- 必须满足：
  - bit47-46 ≠ `00` (必须有一个为 1)
  - 不能全 0 或全 1

### 4.3 校验代码

```cpp
// 检查 public 还是 random
bool isPublic = ((mac[5] & 0xC0) == 0);  // mac[5] 是最高字节 FF

// 检查单播
bool isUnicast = ((mac[0] & 0x01) == 0); // mac[0] 是最低字节 AA
```

---

## 5. 合法地址示例

| 地址 | 最高字节 | bit7-6 | bit0 | 类型 | 合法性 |
|------|---------|:------:|:----:|------|:-----:|
| `02:00:00:00:00:00` | 00 | 00 | 0 | Public | ✅ |
| `04:00:00:00:00:40` | 40 | 01 | 0 | Static Random | ✅ |
| `06:00:00:00:00:80` | 80 | 10 | 0 | Private Resolvable | ✅ |
| `03:00:00:00:00:00` | 00 | 00 | 1 | Public | ❌ bit0=1 (multicast) |
| `01:00:00:00:00:C0` | C0 | 11 | 1 | Private Non-Resolvable | ✅ |

---

## 6. 参考来源

- Bluetooth Core Specification 5.x, Vol 6, Part B, §1.3 "Device Address"
- IEEE 802-2014 Standard for Local and Metropolitan Area Networks
- ESP-IDF API Reference: `esp_ble_gap_set_rand_addr()`, `esp_base_mac_addr_set()`
