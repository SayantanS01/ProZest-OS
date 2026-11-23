#ifndef PROZEST_LOGGER_H
#define PROZEST_LOGGER_H

typedef enum {
    LOG_DEBUG = 0,
    LOG_INFO,
    LOG_OK,
    LOG_WARN,
    LOG_ERROR
} log_level_t;

/*
 * Write a severity-tagged message.
 *
 * The graphical console must be initialized
 * before logging begins.
 */
void log_write(log_level_t level, const char *message);

#endif
