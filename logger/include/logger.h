#pragma once

/**
 * @file logger.h
 * @brief CamperNode OS logging framework.
 */

#include "esp_err.h"
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LOG_LEVEL_DEBUG = 0,
    LOG_LEVEL_INFO,
    LOG_LEVEL_WARNING,
    LOG_LEVEL_ERROR,
    LOG_LEVEL_MAX
} camper_log_level_t;

typedef struct logger logger_t;

logger_t *logger_create(void);
void logger_destroy(logger_t *log);

esp_err_t logger_set_level(logger_t *log, camper_log_level_t level);
camper_log_level_t logger_get_level(const logger_t *log);

void logger_debug(logger_t *log, const char *tag, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
void logger_info(logger_t *log, const char *tag, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
void logger_warning(logger_t *log, const char *tag, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
void logger_error(logger_t *log, const char *tag, const char *fmt, ...) __attribute__((format(printf, 3, 4)));

#ifdef __cplusplus
}
#endif
