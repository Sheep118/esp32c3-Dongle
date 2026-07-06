"""
BLE3.txt 崩溃日志分析脚本
分析崩溃模式：abort() PC地址、Guru Meditation Error、崩溃间周期、bt_gfk_key频率
"""
import re
import sys
from collections import defaultdict, Counter

filepath = r"d:\stu.mxxiao.OneDrive\OneDrive - 汕头大学\桌面\串口助手\BLE3.txt"

with open(filepath, 'r', encoding='utf-8', errors='replace') as f:
    lines = f.readlines()

print(f"总行数: {len(lines)}")
print()

# ============ 1. abort() 调用 ============
abort_pcs = set()
abort_pcs_list = []
for i, line in enumerate(lines):
    m = re.search(r'abort\(\) was called at PC (0x[0-9a-fA-F]+)', line)
    if m:
        abort_pcs.add(m.group(1))
        abort_pcs_list.append(m.group(1))

print("=" * 60)
print("1. abort() 调用分析")
print("=" * 60)
print(f"总计 abort() 次数: {len(abort_pcs_list)}")
print(f"唯一的 PC 地址: {len(abort_pcs)}")
print()
for pc in set(abort_pcs_list):
    count = abort_pcs_list.count(pc)
    print(f"  PC {pc}: 出现 {count} 次")
print()

# ============ 2. Guru Meditation Error ============
guru_entries = []
in_guru = False
guru_info = {}
for i, line in enumerate(lines):
    if 'Guru Meditation Error' in line:
        guru_info = {"line": i+1, "desc": line.strip(), "mepc": None, "ra": None, "mtval": None}
        in_guru = True
    if in_guru:
        m = re.search(r'MEPC\s*:\s*(0x[0-9a-fA-F]+)', line)
        if m: guru_info["mepc"] = m.group(1)
        m = re.search(r'RA\s*:\s*(0x[0-9a-fA-F]+)', line)
        if m: guru_info["ra"] = m.group(1)
        m = re.search(r'MTVAL\s*:\s*(0x[0-9a-fA-F]+)', line)
        if m: guru_info["mtval"] = m.group(1)
    if in_guru and line.strip() == '' and guru_info.get("mepc"):
        guru_entries.append(guru_info)
        guru_info = {}
        in_guru = False

print("=" * 60)
print("2. Guru Meditation Error 分析")
print("=" * 60)
print(f"总计 Guru Meditation Errors: {len(guru_entries)}")
print()
for g in guru_entries:
    print(f"  Line {g['line']}: {g['desc']}")
    print(f"    MEPC={g['mepc']}, RA={g['ra']}, MTVAL={g['mtval']}")
print()

# ============ 2b. 所有崩溃的 RA 值（寄存器dump中的Return Address） ============
all_ras = []
in_crash = False
for i, line in enumerate(lines):
    if 'abort() was called' in line or 'Guru Meditation Error' in line:
        in_crash = True
    if in_crash and re.match(r'^\s*RA\s*:', line):
        m = re.search(r'RA\s*:\s*(0x[0-9a-fA-F]+)', line)
        if m:
            all_ras.append(m.group(1))
        in_crash = False

print("2b. 所有崩溃的 RA (Return Address) 值：")
for ra in all_ras:
    print(f"  {ra}")
print()

# ============ 3. 崩溃之间的扫描周期数 ============
crash_line_nums = []
for i, line in enumerate(lines):
    if 'abort() was called' in line or 'Guru Meditation Error' in line:
        crash_line_nums.append(i + 1)

print("=" * 60)
print("3. 崩溃之间的扫描周期数统计")
print("=" * 60)
print(f"崩溃/异常总数: {len(crash_line_nums)}")
print(f"崩溃发生行号: {crash_line_nums}")
print()

# Count $SCAN_START between crashes
scan_start_lines = set()
for i, line in enumerate(lines):
    if '$SCAN_START' in line:
        scan_start_lines.add(i + 1)

cycles_between = []
prev_crash = 0
for cl in crash_line_nums:
    count = 0
    for sl in scan_start_lines:
        if prev_crash < sl < cl:
            count += 1
    if prev_crash > 0:
        cycles_between.append(count)
    prev_crash = cl

# After last crash
remaining_starts = 0
if crash_line_nums:
    last = crash_line_nums[-1]
    for sl in scan_start_lines:
        if sl > last:
            remaining_starts += 1

print(f"崩溃间扫描周期数 (SCAN_START 计数): {cycles_between}")
print(f"最后崩溃后剩余的扫描周期数 (日志结束前): {remaining_starts}")
print()
if cycles_between:
    print(f"  统计:")
    print(f"    最小: {min(cycles_between)}")
    print(f"    最大: {max(cycles_between)}")
    print(f"    平均: {sum(cycles_between)/len(cycles_between):.1f}")
    print(f"    中位数: {sorted(cycles_between)[len(cycles_between)//2]}")
    print()
    print(f"  分布:")
    c_dist = Counter(cycles_between)
    for c in sorted(c_dist):
        bar = "#" * c_dist[c]
        print(f"    {c:3d} 周期: {c_dist[c]:2d} 次  {bar}")

print()

# ============ 4. bt_gfk_key 频率 ============
bt_gfk_lines_list = []
for i, line in enumerate(lines):
    # bt_gfk_key in ASCII: b=62 t=74 _=5f g=67 f=66 k=6b _=5f k=6b e=65 y=79 \0=00
    # As 32-bit LE words: 0x675f7462 0x6b5f6b66 0x00007965
    if 'bt_gfk_key' in line:
        bt_gfk_lines_list.append((i+1, line.strip()))
    elif '675f7462' in line and '6b5f6b66' in line:
        bt_gfk_lines_list.append((i+1, line.strip()))

print("=" * 60)
print("4. 堆栈中 bt_gfk_key 出现频率")
print("=" * 60)
print(f"总计出现次数: {len(bt_gfk_lines_list)}")
print()

# Group occurrences by crash event
crash_sections = []
current_section = []
for i, line in enumerate(lines):
    if 'abort() was called' in line or 'Guru Meditation Error' in line:
        if current_section:
            crash_sections.append(current_section)
        current_section = [(i+1, line.strip())]
    elif current_section and ('Stack memory:' in line or 'Core' in line or 'register dump' in line or
          re.match(r'^[0-9a-f]+:', line) or line.strip() == '' or
          line.startswith('MEPC') or line.startswith('RA') or line.startswith('SP') or
          'bt_gfk' in line):
        current_section.append((i+1, line.strip()))
    elif current_section and 'ELF file' in line:
        current_section.append((i+1, line.strip()))
        crash_sections.append(current_section)
        current_section = []

# Count bt_gfk_key per crash
for idx, section in enumerate(crash_sections):
    bt_count = sum(1 for _, content in section if 'bt_gfk_key' in content or ('675f7462' in content and '6b5f6b66' in content))
    start_line = section[0][0]
    print(f"  崩溃 #{idx+1} (行 {start_line}): bt_gfk_key 出现 {bt_count} 次")

print()

# Show the actual stack lines containing bt_gfk
print("bt_gfk_key 在堆栈中的具体行：")
for lineno, content in bt_gfk_lines_list:
    print(f"  行 {lineno}: ...{content}...")
print()

# ============ 5. 扫描周期统计 ============
scan_counts = []
for i, line in enumerate(lines):
    m = re.search(r'\$SCAN_END\|COUNT:(\d+)', line)
    if m:
        scan_counts.append(int(m.group(1)))

print("=" * 60)
print("5. 扫描周期统计")
print("=" * 60)
total_scans = len(scan_counts)
total_devices = sum(scan_counts)
print(f"总扫描周期数: {total_scans}")
print(f"发现设备总数: {total_devices}")
if total_scans > 0:
    print(f"平均每周期设备数: {total_devices/total_scans:.1f}")
    print(f"  最小: {min(scan_counts)}")
    print(f"  最大: {max(scan_counts)}")
    print()
    print(f"  设备数分布:")
    c_dist = Counter(scan_counts)
    for c in sorted(c_dist):
        pct = c_dist[c] / total_scans * 100
        bar = "#" * c_dist[c]
        print(f"    {c:2d} 设备: {c_dist[c]:2d} 次 ({pct:5.1f}%)  {bar}")

print()

# ============ 6. 总体概要 ============
total_crashes = len(abort_pcs_list) + len(guru_entries)
print("=" * 60)
print("=== 崩溃分析总结 ===")
print("=" * 60)
print(f"运行时间: ~{total_scans * 5 / 60:.1f} 分钟 ({total_scans} 个扫描周期 × 5秒)")
print(f"总崩溃次数: {total_crashes}")
print(f"  abort() 调用: {len(abort_pcs_list)}")
print(f"  Guru Meditation Error: {len(guru_entries)}")
print(f"bt_gfk_key 在堆栈中出现: {len(bt_gfk_lines_list)} 次")
print()
if len(abort_pcs_list) > 0:
    print(f"崩溃频率: 每 {total_scans / total_crashes:.1f} 个扫描周期崩溃一次")
    print(f"         ~每 {(total_scans * 5) / total_crashes:.0f} 秒崩溃一次")
if cycles_between:
    print(f"崩溃间隔周期分布: {sorted(cycles_between)}")
print()

# ============ 额外: 每行输出崩溃类型标记 ============
print("=" * 60)
print("崩溃时间线")
print("=" * 60)
for c in abort_pcs_list:
    print(f"  [abort] PC={c}")
for g in guru_entries:
    print(f"  [Guru]  {g['desc']} | MEPC={g['mepc']} | MTVA={g['mtval']}")

print()
print("分析完成")
