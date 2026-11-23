#include <stddef.h>

#include "logger.h"
#include "console.h"

/*
 * Translate a severity level into
 * a human-readable text prefix.
 */
static const char *log_prefix(log_level_t level)
{
    switch (level) {
        case LOG_DEBUG:
            return "[DEBUG] ";

        case LOG_INFO:
            return "[INFO]  ";

        case LOG_OK:
            return "[ OK ]  ";

        case LOG_WARN:
            return "[WARN]  ";

        case LOG_ERROR:
            return "[ERROR] ";

        default:
            return "[????]  ";
    }
}

/*
 * Write a structured kernel log entry.
 *
 * Version 1 supports plain strings only.
 * Formatting, timestamps, and buffering
 * will be introduced in later stages.
 */
void log_write(log_level_t level, const char *message)
{
    if (message == NULL) {
        return;
    }

    console_write(log_prefix(level));
    console_write(message);
    console_newline();
}
