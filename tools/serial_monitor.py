"""
串口监视器 - 用于读取 BLE Dongle 的 USB-CDC 输出
用法: python serial_monitor.py [COM端口]

解析 $ 前缀的自定义 BLE 扫描数据格式：
  $BLE|MAC:XX:XX:XX:XX:XX:XX|RSSI:-42|ADDR:0|NAME:xxx|MANUF:AABBCC|UUID:xxx
  $SCAN_START|DURATION:5
  $SCAN_END|COUNT:12
"""

import sys
import time
import serial
import serial.tools.list_ports
from collections import OrderedDict


DEFAULT_PORT = "COM5"
BAUDRATE = 115200


def list_ports():
    ports = serial.tools.list_ports.comports()
    if not ports:
        print("没有发现可用串口")
        return []
    print("可用串口:")
    for p in ports:
        desc = p.description if p.description else "未知设备"
        print(f"  {p.device} - {desc}")
    return ports


def find_esp32c3_port():
    ports = serial.tools.list_ports.comports()
    for p in ports:
        if "303A" in p.hwid.upper() or "ESP" in p.description.upper():
            return p.device
    return None


def parse_ble_line(text):
    """解析 $ 前缀的自定义格式行，返回 (类型, 字段字典) 或 None"""
    if not text.startswith('$'):
        return None
    
    # 去除开头的 $
    rest = text[1:]
    
    # 根据第一个 | 前的关键词判断类型
    first_pipe = rest.find('|')
    if first_pipe < 0:
        prefix = rest
        fields_str = ""
    else:
        prefix = rest[:first_pipe]
        fields_str = rest[first_pipe + 1:]
    
    fields = OrderedDict()
    
    if prefix == "BLE":
        # $BLE|MAC:...|RSSI:...|ADDR:...|NAME:...|MANUF:...|UUID:...
        for part in fields_str.split('|'):
            if ':' in part:
                key, _, value = part.partition(':')
                fields[key.upper()] = value
        return ("BLE", fields)
    
    elif prefix == "SCAN_START":
        # $SCAN_START|DURATION:5
        for part in fields_str.split('|'):
            if ':' in part:
                key, _, value = part.partition(':')
                fields[key.upper()] = value
        return ("SCAN_START", fields)
    
    elif prefix == "SCAN_END":
        # $SCAN_END|COUNT:12
        for part in fields_str.split('|'):
            if ':' in part:
                key, _, value = part.partition(':')
                fields[key.upper()] = value
        return ("SCAN_END", fields)
    
    return None


def format_ble(fields):
    """格式化 BLE 扫描结果，返回带颜色的字符串"""
    mac = fields.get("MAC", "??")
    rssi = fields.get("RSSI", "?")
    name = fields.get("NAME", "")
    manuf = fields.get("MANUF", "")
    uuid = fields.get("UUID", "")

    # RSSI 强度指示
    try:
        rssi_val = int(rssi)
        if rssi_val >= -50:
            rssi_display = f"\033[91m{rssi:>4}\033[0m"        # 强信号-红色
        elif rssi_val >= -70:
            rssi_display = f"\033[93m{rssi:>4}\033[0m"        # 中信号-黄色
        else:
            rssi_display = f"\033[92m{rssi:>4}\033[0m"        # 弱信号-绿色
    except:
        rssi_display = f"{rssi:>4}"

    # 名字截断
    name_part = f" \033[94m{name}\033[0m" if name else ""
    
    # 厂商数据预览（取前 8 字符缩略）
    manuf_part = ""
    if manuf:
        short_mf = manuf[:12]
        if len(manuf) > 12:
            short_mf += ".."
        manuf_part = f" \033[90mMFG:{short_mf}\033[0m"

    # UUID
    uuid_part = ""
    if uuid:
        uuid_part = f" \033[90mUUID:{uuid[:8]}..\033[0m"

    return f"\033[92mBLE\033[0m {rssi_display} {mac}{name_part}{manuf_part}{uuid_part}"


def main():
    port = DEFAULT_PORT
    if len(sys.argv) > 1:
        port = sys.argv[1]
    
    available = [p.device for p in serial.tools.list_ports.comports()]
    if port not in available:
        print(f"端口 {port} 不可用，正在搜索 ESP32-C3...")
        esp_port = find_esp32c3_port()
        if esp_port:
            port = esp_port
            print(f"发现 ESP32-C3: {port}")
        else:
            list_ports()
            print(f"\n请指定正确的端口: python {sys.argv[0]} COMx")
            sys.exit(1)
    
    print(f"连接 {port} @ {BAUDRATE} bps...")
    print("按 Ctrl+C 退出\n")
    print("=" * 60)
    
    try:
        ser = serial.Serial(port, BAUDRATE, timeout=1)
        time.sleep(2)
        ser.reset_input_buffer()
        
        while True:
            if ser.in_waiting > 0:
                line = ser.readline()
                try:
                    text = line.decode('utf-8', errors='replace').strip()
                    if not text:
                        continue
                    
                    parsed = parse_ble_line(text)
                    if parsed:
                        ptype, fields = parsed
                        if ptype == "BLE":
                            print(format_ble(fields))
                        elif ptype == "SCAN_START":
                            dur = fields.get("DURATION", "?")
                            print(f"\033[90m── 扫描开始 ({dur}s) ──\033[0m")
                        elif ptype == "SCAN_END":
                            cnt = fields.get("COUNT", "?")
                            print(f"\033[90m── 扫描结束, 发现 {cnt} 个设备 ──\033[0m")
                    else:
                        # 不是 $ 格式，普通日志
                        # 过滤掉 BLE 栈的 ESP 日志
                        if text.startswith("E (") or text.startswith("I (") or text.startswith("D ("):
                            continue
                        if text.startswith("ESP-ROM:") or text.startswith("Build:") or \
                           text.startswith("rst:") or text.startswith("SPIWP:") or \
                           text.startswith("mode:") or text.startswith("load:") or \
                           text.startswith("entry"):
                            continue
                        if text.startswith("Core") or text.startswith("MEPC") or \
                           text.startswith("TP") or text.startswith("S0") or \
                           text.startswith("A0") or text.startswith("A2") or \
                           text.startswith("A6") or text.startswith("S2") or \
                           text.startswith("S4") or text.startswith("S8") or \
                           text.startswith("T3") or text.startswith("MSTATUS") or \
                           text.startswith("MHARTID") or text.startswith("Stack") or \
                           text.startswith("ELF") or text.startswith("Rebooting"):
                            continue
                        if text.startswith("Guru Meditation"):
                            print(f"\033[91m{text}\033[0m")  # 红色显示崩溃
                            continue
                        print(text)
                except:
                    pass
            else:
                time.sleep(0.01)
                
    except serial.SerialException as e:
        print(f"串口错误: {e}")
        sys.exit(1)
    except KeyboardInterrupt:
        print("\n\n用户中断，退出。")
    finally:
        if 'ser' in locals() and ser.is_open:
            ser.close()
            print("串口已关闭。")

if __name__ == "__main__":
    main()


if __name__ == "__main__":
    main()
