/**
 * @file logger.c
 * @brief CamperNode OS logging framework.
 */

#include "logger.h"
#include "esp_log.h"
#include <stdlib.h>
#include <stdio.h>

struct logger {
    camper_log_level_t level;
};

static esp_log_level_t to_esp_level(camper_log_level_t level)
{
    switch (level) {
    case LOG_LEVEL_DEBUG:
        return ESP_LOG_DEBUG;
    case LOG_LEVEL_INFO:
        return ESP_LOG_INFO;
    case LOG_LEVEL_WARNING:
        return ESP_LOG_WARN;
    case LOG_LEVEL_ERROR:
        return ESP_LOG_ERROR;
    default:
        return ESP_LOG_INFO;
    }
}

static bool should_log(const logger_t *log, camper_log_level_t level)
{
    return log != NULL && level >= log->level;
}

static void log_v(logger_t *log, camper_log_level_t level, esp_log_level_t esp_level,
                  const char *tag, const char *fmt, va_list args)
{
    if (!should_log(log, level)) {
        return;
    }
    esp_log_writev(esp_level, tag, fmt, args);
}

logger_t *logger_create(void)
{
    logger_t *log = calloc(1, sizeof(logger_t));
    if (log == NULL) {
        return NULL;
    }
    log->level = LOG_LEVEL_INFO;
    return log;
}

void logger_destroy(logger_t *log)
{
    free(log);
}

esp_err_t logger_set_level(logger_t *log, camper_log_level_t level)
{
    if (log == NULL || level >= LOG_LEVEL_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    log->level = level;
    esp_log_level_set("*", to_esp_level(level));
    return ESP_OK;
}

camper_log_level_t logger_get_level(const logger_t *log)
{
    if (log == NULL) {
        return LOG_LEVEL_INFO;
    }
    return log->level;
}

void logger_debug(logger_t *log, const char *tag, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    log_v(log, LOG_LEVEL_DEBUG, ESP_LOG_DEBUG, tag, fmt, args);
    va_end(args);
}

void logger_info(logger_t *log, const char *tag, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    log_v(log, LOG_LEVEL_INFO, ESP_LOG_INFO, tag, fmt, args);
    va_end(args);
}

void logger_warning(logger_t *log, const char *tag, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    log_v(log, LOG_LEVEL_WARNING, ESP_LOG_WARN, tag, fmt, args);
    va_end(args);
}

void logger_error(logger_t *log, const char *tag, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    log_v(log, LOG_LEVEL_ERROR, ESP_LOG_ERROR, tag, fmt, args);
    va_end(args);
}
