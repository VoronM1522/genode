#include <utils.h>

Genode::String<
    Vfs::Directory_service::Dirent::Name::MAX_LEN>
timestamp_to_str (const Rtc::Timestamp &ts)
{
  Genode::String<4+1> year (ts.year);
  Genode::String<2+1> month (ts.month);
  Genode::String<2+1> day (ts.day);
  Genode::String<2+1> hour (ts.hour);
  Genode::String<2+1> minute (ts.minute);
  Genode::String<2+1> second (ts.second);

  month = (ts.month < 10) ? Genode::String<2+1>("0", month) : month;
  day = (ts.day < 10) ? Genode::String<2+1>("0", day) : day;
  hour = (ts.hour < 10) ? Genode::String<2+1>("0", hour) : hour;
  minute = (ts.minute < 10) ? Genode::String<2+1>("0", minute) : minute;
  second = (ts.second < 10) ? Genode::String<2+1>("0", second) : second;
  
  Genode::String<Vfs::Directory_service::Dirent::Name::MAX_LEN> str (
      year, "-", month, "-", day, " ", hour, ":", minute, ":",
      second);

  return str;
}

// TODO better error handling
Rtc::Timestamp
str_to_timestamp (char *str)
{
  if (Genode::strlen (str) > Vfs::Directory_service::Dirent::Name::MAX_LEN)
    {
      throw -1;
    }

  char *ptr = str;
  const unsigned base = 10;

  Rtc::Timestamp ts;

  // year
  Genode::size_t size
      = Genode::ascii_to_unsigned<decltype (ts.year)> (ptr, ts.year, base);

  if (size < 4)
    throw -1;

  ptr += size + 1;

  // month
  size = Genode::ascii_to_unsigned<decltype (ts.month)> (ptr, ts.month, base);
  if (size != 1 && size != 2)
    throw -1;

  ptr += size + 1;

  // day
  size = Genode::ascii_to_unsigned<decltype (ts.day)> (ptr, ts.day, base);
  if (size != 1 && size != 2)
    throw -1;

  ptr += size + 1;

  // hour
  size = Genode::ascii_to_unsigned<decltype (ts.hour)> (ptr, ts.hour, base);
  if (size != 1 && size != 2)
    throw -1;

  ptr += size + 1;

  // minute
  size
      = Genode::ascii_to_unsigned<decltype (ts.minute)> (ptr, ts.minute, base);
  if (size != 1 && size != 2)
    throw -1;

  ptr += size + 1;

  // second
  size
      = Genode::ascii_to_unsigned<decltype (ts.second)> (ptr, ts.second, base);
  if (size != 1 && size != 2)
    throw -1;

  return ts;
}

bool
leap_year (unsigned year)
{
  return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

unsigned
days_in_month (unsigned month, unsigned year)
{
  static const unsigned days[]
      = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

  if (month == 2 && leap_year (year))
    return 29;

  return days[month - 1];
}

Genode::uint64_t
timestamp_to_seconds (const Rtc::Timestamp &ts)
{
  Genode::uint64_t seconds = 0;

  // add seconds for complete years since epoch (1970)
  for (unsigned y = 1970; y < ts.year; y++)
    {
      seconds += leap_year (y) ? 366ULL * 24 * 60 * 60 : 365ULL * 24 * 60 * 60;
    }

  // add seconds for complete months in current year
  for (unsigned m = 1; m < ts.month; m++)
    {
      seconds += days_in_month (m, ts.year) * 24ULL * 60 * 60;
    }

  // Add remaining days, hours, minutes and seconds
  seconds += (ts.day - 1) * 24ULL * 60 * 60;
  seconds += ts.hour * 60ULL * 60;
  seconds += ts.minute * 60ULL;
  seconds += ts.second;

  return seconds;
}

void
remove_basename (char *path)
{
  char *last_slash = path;
  for (char *ptr = path; *ptr != 0; ptr++)
    {
      if (*ptr == '/' && *(ptr + 1) != 0)
        last_slash = ptr;
    }

  *last_slash = 0;
}
