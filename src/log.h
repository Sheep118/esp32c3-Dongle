#ifndef LOG_H
#define LOG_H

#include <Arduino.h>

/**
 * ========================================================
 *  统一日志宏模块
 * ========================================================
 *
 * 使用方法：
 *   1. 在 .cpp 文件顶部包含此头文件
 *   2. 定义 LOG_TAG（例如 #define LOG_TAG "Main"）
 *   3. 使用 LOG_INFO/adgm/ERROR(...) 替代 Serial.printf(...)
 *
 * 原理：
 *   - 没有定义 LOG_TAG 的文件中，所有 LOG_* 宏被编译为空（零开销）
 *   - 有 LOG_TAG 的文件，自动在每行前附加 [TAG] 前缀
 *
 * 示例：
 *   #define LOG_TAG "BLE"
 *   LOG_INFO("scan started, duration=%u\n", 5);
 *   // → 输出: [BLE] scan started, duration=5
 *
 * 控制全局开关：
 *   在 platformio.ini 中 -DLOG_DISABLE 可全局关闭所有日志输出
 */

// ── 全局关闭开关 ──
// platformio.ini: -DLOG_DISABLE → 所有模块日志编译为空
#ifdef LOG_DISABLE
    #undef  LOG_TAG
#endif

// ── TAG 检测 ──
#ifdef LOG_TAG
    #define LOG_HAS_TAG 1
#else
    #define LOG_HAS_TAG 0
#endif

// ── 日志宏 ──
#if LOG_HAS_TAG
    #define LOG_INFO(fmt, ...)  Serial.printf("[%s] " fmt "\r\n", LOG_TAG, ##__VA_ARGS__)
    #define LOG_WARN(fmt, ...)  Serial.printf("[%s] WARN: " fmt "\r\n", LOG_TAG, ##__VA_ARGS__)
    #define LOG_ERROR(fmt, ...) Serial.printf("[%s] ERROR: " fmt "\r\n", LOG_TAG, ##__VA_ARGS__)
#else
    #define LOG_INFO(fmt, ...)  ((void)0)
    #define LOG_WARN(fmt, ...)  ((void)0)
    #define LOG_ERROR(fmt, ...) ((void)0)
#endif

#endif // LOG_H
