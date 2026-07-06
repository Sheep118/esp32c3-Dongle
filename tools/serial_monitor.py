"""
串口监视器 - 用于读取 BLE Dongle 的 USB-CDC 输出
用法: python serial_monitor.py [COM端口]

如果不指定端口，默认使用 COM5
"""

import sys
import time
import serial
import serial.tools.list_ports

DEFAULT_PORT = "COM5"
BAUDRATE = 115200


def list_ports():
    """列出所有可用串口"""
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
    """自动寻找 ESP32-C3 串口"""
    ports = serial.tools.list_ports.comports()
    for p in ports:
        # ESP32-C3 USB CDC 的 VID 是 0x303A
        if "303A" in p.hwid.upper() or "ESP" in p.description.upper():
            return p.device
    return None


def main():
    # 确定端口
    port = DEFAULT_PORT
    if len(sys.argv) > 1:
        port = sys.argv[1]
    
    # 如果默认端口不可用，自动查找
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
        # 给设备一点时间复位
        time.sleep(2)
        
        # 清空缓冲区
        ser.reset_input_buffer()
        
        while True:
            if ser.in_waiting > 0:
                # 读取一行
                line = ser.readline()
                try:
                    text = line.decode('utf-8', errors='replace').strip()
                    if text:
                        # 尝试解析 JSON（BLE 扫描结果是 JSON）
                        if text.startswith('{') and ('"type":"ble_scan"' in text or '"ble_scan"' in text):
                            # 彩色打印 BLE 扫描结果
                            print(f"\033[92m[BLE] {text}\033[0m")
                        elif text.startswith('['):
                            print(f"\033[93m{text}\033[0m")
                        else:
                            print(text)
                except:
                    print(f"[RAW] {line.hex()}")
            else:
                # 没有数据时短暂休眠，降低 CPU 占用
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
