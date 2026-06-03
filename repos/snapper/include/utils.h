#pragma once

#include <base/log.h>
#include <os/path.h>
#include <os/vfs.h>
#include <rtc_session/connection.h>

/* Decor */
#define RED "\033[31m"
#define GREEN "\033[32m"
#define YELLOW "\033[33m"
#define CYAN "\033[96m"

#define BG_RED "\033[41m"

#define UNDERLINE "\033[4m"
#define BOLD "\033[1m"

#define RESET "\033[0m"

/* Logging */
#define TODO(message) Genode::log (CYAN "[TODO] ", message)


/* Timestamps */
/**
 * @brief Convert RTC timestamp to formatted string.
 */
Genode::String<Vfs::Directory_service::Dirent::Name::MAX_LEN>
timestamp_to_str (const Rtc::Timestamp &ts);

/**
 * @brief Convert formatted string to an RTC timestamp.
 */
Rtc::Timestamp str_to_timestamp (char *str);

/**
 * @brief Checks if given year is a leap year or not.
 */
bool leap_year (unsigned year);

/**
 * @brief Returns the number of days in a given month in a given year.
 */
unsigned days_in_month (unsigned month, unsigned year);

/**
 * @brief Coverts an RTC timestamp to seconds since UNIX time.
 */
Genode::uint64_t timestamp_to_seconds (const Rtc::Timestamp &ts);

/**
 * @brief Removes the basename from the file path.
 */
void remove_basename (char *path);
