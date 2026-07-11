"""Remove trailing \n from all LOG_INFO/WARN/ERROR() calls since the macro now appends \r\n automatically."""
import re, glob, os

src_dir = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "src")
count = 0

for fp in glob.glob(os.path.join(src_dir, "*.cpp")):
    with open(fp, "r", encoding="utf-8") as f:
        text = f.read()
    
    # Match LOG_*(...)\n" → LOG_*(...)"  (remove the trailing \n inside the format string)
    new_text = re.sub(r'(LOG_(?:INFO|WARN|ERROR)\([^"]*"[^"]*)\\n"', r'\1"', text)
    
    if new_text != text:
        with open(fp, "w", encoding="utf-8") as f:
            f.write(new_text)
        print(f"  Fixed: {os.path.basename(fp)}")
        count += 1

print(f"Done. {count} files updated.")
