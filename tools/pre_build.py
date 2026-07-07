"""
PlatformIO 预编译脚本：自动将 HTML 嵌入为 C 头文件
"""
import sys
import os

Import("env")

# 获取项目根目录
project_dir = env.subst("${PROJECT_DIR}")
tools_dir = os.path.join(project_dir, "tools")
sys.path.insert(0, tools_dir)

from embed_html import html_to_c_array

html_path = os.path.join(tools_dir, "web_config.html")
output_path = os.path.join(project_dir, "src", "webpage.h")

html_to_c_array(html_path, output_path)
