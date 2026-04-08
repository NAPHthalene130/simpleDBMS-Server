#pragma once

#include <cstdint>

/**
 * @class DateTime
 * @brief 日期时间数据类
 * @details 提供统一的日期时间数据容器，包含年、月、周内日、日、时、分、秒、毫秒八个字段。
 * 该类被数据库、表和字段等元数据对象复用，用于记录创建时间与修改时间。
 * @author NAPH130
 */
class DateTime
{
public:
    DateTime();

    std::uint16_t getYear() const;
    void setYear(std::uint16_t year);

    std::uint16_t getMonth() const;
    void setMonth(std::uint16_t month);

    std::uint16_t getDayOfWeek() const;
    void setDayOfWeek(std::uint16_t dayOfWeek);

    std::uint16_t getDay() const;
    void setDay(std::uint16_t day);

    std::uint16_t getHour() const;
    void setHour(std::uint16_t hour);

    std::uint16_t getMinute() const;
    void setMinute(std::uint16_t minute);

    std::uint16_t getSecond() const;
    void setSecond(std::uint16_t second);

    std::uint16_t getMilliseconds() const;
    void setMilliseconds(std::uint16_t milliseconds);

private:
    std::uint16_t year;         ///< 年份
    std::uint16_t month;        ///< 月份（1-12）
    std::uint16_t dayOfWeek;    ///< 周内日
    std::uint16_t day;          ///< 月内日（1-31）
    std::uint16_t hour;         ///< 小时（0-23）
    std::uint16_t minute;       ///< 分钟（0-59）
    std::uint16_t second;       ///< 秒（0-59）
    std::uint16_t milliseconds; ///< 毫秒（0-999）
};
