#include "DateTime.h"

DateTime::DateTime()
    : year(0),
      month(0),
      dayOfWeek(0),
      day(0),
      hour(0),
      minute(0),
      second(0),
      milliseconds(0)
{
}

std::uint16_t DateTime::getYear() const
{
    return year;
}

void DateTime::setYear(std::uint16_t year)
{
    this->year = year;
}

std::uint16_t DateTime::getMonth() const
{
    return month;
}

void DateTime::setMonth(std::uint16_t month)
{
    this->month = month;
}

std::uint16_t DateTime::getDayOfWeek() const
{
    return dayOfWeek;
}

void DateTime::setDayOfWeek(std::uint16_t dayOfWeek)
{
    this->dayOfWeek = dayOfWeek;
}

std::uint16_t DateTime::getDay() const
{
    return day;
}

void DateTime::setDay(std::uint16_t day)
{
    this->day = day;
}

std::uint16_t DateTime::getHour() const
{
    return hour;
}

void DateTime::setHour(std::uint16_t hour)
{
    this->hour = hour;
}

std::uint16_t DateTime::getMinute() const
{
    return minute;
}

void DateTime::setMinute(std::uint16_t minute)
{
    this->minute = minute;
}

std::uint16_t DateTime::getSecond() const
{
    return second;
}

void DateTime::setSecond(std::uint16_t second)
{
    this->second = second;
}

std::uint16_t DateTime::getMilliseconds() const
{
    return milliseconds;
}

void DateTime::setMilliseconds(std::uint16_t milliseconds)
{
    this->milliseconds = milliseconds;
}
