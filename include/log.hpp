#pragma once

#include <Arduino.h>
#include "app_config.hpp"

enum LogLevel : uint8_t {
  LOG_LEVEL_ERROR = 0,
  LOG_LEVEL_WARN = 1,
  LOG_LEVEL_INFO = 2,
  LOG_LEVEL_DEBUG = 3,
};

#ifndef LOG_LEVEL
#define LOG_LEVEL LOG_LEVEL_DEBUG
#endif

#if ENABLE_DEBUG_LOG
  #define LOG_ENABLED(level) ((level) <= LOG_LEVEL)
  #define LOG_PRINT(level, fmt, ...)            \
    do {                                        \
      if (LOG_ENABLED(level)) {                 \
        static const char *kTags[] = {"E", "W", "I", "D"}; \
        Serial.printf("[%s] " fmt, kTags[level], ##__VA_ARGS__); \
      }                                         \
    } while (0)
#else
  #define LOG_ENABLED(level) (false)
  #define LOG_PRINT(level, fmt, ...) do {} while (0)
#endif

#define LOGE(fmt, ...) LOG_PRINT(LOG_LEVEL_ERROR, fmt, ##__VA_ARGS__)
#define LOGW(fmt, ...) LOG_PRINT(LOG_LEVEL_WARN, fmt, ##__VA_ARGS__)
#define LOGI(fmt, ...) LOG_PRINT(LOG_LEVEL_INFO, fmt, ##__VA_ARGS__)
#define LOGD(fmt, ...) LOG_PRINT(LOG_LEVEL_DEBUG, fmt, ##__VA_ARGS__)
