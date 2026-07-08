#include "HalClock.h"

#include <Logging.h>
#include <WiFi.h>
#include <esp_sntp.h>
#include <time.h>

#include <cassert>

HalClock halClock;  // Singleton instance

// DS3231 register layout (BCD encoded):
//   0x00: Seconds
//   0x01: Minutes
//   0x02: Hours    (bit 6 = 0 for 24h mode)
//   0x03: Day of week (1-7)
//   0x04: Date      (1-31)
//   0x05: Month     (bit 7 = century, bits 3-0 = month)
//   0x06: Year      (00-99, offset from century bit)

namespace {
uint8_t bcdToDec(uint8_t bcd) { return ((bcd >> 4) * 10) + (bcd & 0x0F); }
uint8_t decToBcd(uint8_t dec) { return ((dec / 10) << 4) | (dec % 10); }

time_t utcTmToEpoch(struct tm& utc) {
  utc.tm_isdst = 0;
  setenv("TZ", "UTC0", 1);
  tzset();
  return mktime(&utc);
}

bool isValidUtcTm(const struct tm& utc) {
  if (utc.tm_year < 124) {
    return false;
  }
  if (utc.tm_mon < 0 || utc.tm_mon > 11) {
    return false;
  }
  if (utc.tm_mday < 1 || utc.tm_mday > 31) {
    return false;
  }
  if (utc.tm_hour < 0 || utc.tm_hour > 23) {
    return false;
  }
  if (utc.tm_min < 0 || utc.tm_min > 59) {
    return false;
  }
  if (utc.tm_sec < 0 || utc.tm_sec > 59) {
    return false;
  }
  const time_t epoch = utcTmToEpoch(const_cast<struct tm&>(utc));
  return epoch >= static_cast<time_t>(1704067200UL);
}
}  // namespace

void HalClock::begin() {
  if (!gpio.deviceIsX3()) {
    _available = false;
    return;
  }

  Wire.beginTransmission(I2C_ADDR_DS3231);
  Wire.write(DS3231_SEC_REG);
  if (Wire.endTransmission(false) != 0) {
    LOG_INF("CLK", "DS3231 RTC not found");
    _available = false;
    return;
  }
  Wire.requestFrom(I2C_ADDR_DS3231, (uint8_t)1);
  if (Wire.available() < 1) {
    _available = false;
    return;
  }
  Wire.read();

  _available = true;
  LOG_INF("CLK", "DS3231 RTC found");

  struct tm utc{};
  readUtcTm(utc, true);
}

bool HalClock::readUtcTmFromChip(struct tm& outUtc, const bool forceRefresh) const {
  if (!_available) {
    return false;
  }

  const unsigned long now = millis();
  if (!forceRefresh && _lastPollMs != 0 && (now - _lastPollMs) < CLOCK_POLL_MS && _hasCachedTime) {
    outUtc = _cachedUtcTm;
    return true;
  }

  Wire.beginTransmission(I2C_ADDR_DS3231);
  Wire.write(DS3231_SEC_REG);
  if (Wire.endTransmission(false) != 0) {
    if (!_hasCachedTime) {
      return false;
    }
    outUtc = _cachedUtcTm;
    return true;
  }
  Wire.requestFrom(I2C_ADDR_DS3231, (uint8_t)7);
  if (Wire.available() < 7) {
    if (!_hasCachedTime) {
      return false;
    }
    outUtc = _cachedUtcTm;
    return true;
  }

  const uint8_t rawSec = Wire.read();
  const uint8_t rawMin = Wire.read();
  const uint8_t rawHour = Wire.read();
  Wire.read();
  const uint8_t rawDate = Wire.read();
  const uint8_t rawMonth = Wire.read();
  const uint8_t rawYear = Wire.read();

  struct tm utc{};
  utc.tm_sec = bcdToDec(rawSec & 0x7F);
  utc.tm_min = bcdToDec(rawMin & 0x7F);

  if (rawHour & 0x40) {
    uint8_t h12 = bcdToDec(rawHour & 0x1F);
    const bool pm = rawHour & 0x20;
    if (h12 == 12) {
      h12 = 0;
    }
    utc.tm_hour = pm ? static_cast<int>(h12 + 12) : h12;
  } else {
    utc.tm_hour = bcdToDec(rawHour & 0x3F);
  }

  utc.tm_mday = bcdToDec(rawDate & 0x3F);
  utc.tm_mon = bcdToDec(rawMonth & 0x7F) - 1;
  const bool century19 = (rawMonth & 0x80) != 0;
  const int year = bcdToDec(rawYear);
  utc.tm_year = (century19 ? 1900 + year : 2000 + year) - 1900;
  utc.tm_isdst = 0;

  if (!isValidUtcTm(utc)) {
    if (!_hasCachedTime) {
      return false;
    }
    outUtc = _cachedUtcTm;
    return true;
  }

  _cachedUtcTm = utc;
  _lastPollMs = now;
  _hasCachedTime = true;
  outUtc = utc;
  return true;
}

bool HalClock::readUtcTm(struct tm& outUtc, const bool forceRefresh) const {
  return readUtcTmFromChip(outUtc, forceRefresh);
}

bool HalClock::readUtcEpoch(uint32_t& epochSeconds, const bool forceRefresh) const {
  struct tm utc{};
  if (!readUtcTm(utc, forceRefresh)) {
    return false;
  }
  const time_t epoch = utcTmToEpoch(utc);
  if (epoch < 0) {
    return false;
  }
  epochSeconds = static_cast<uint32_t>(epoch);
  return true;
}

bool HalClock::writeUtcTmToChip(const struct tm& utc) const {
  assert(utc.tm_hour >= 0 && utc.tm_hour < 24);
  assert(utc.tm_min >= 0 && utc.tm_min < 60);
  assert(utc.tm_sec >= 0 && utc.tm_sec < 60);

  const int year = utc.tm_year + 1900;
  const bool century19 = year < 2000;
  const uint8_t yearBcd = static_cast<uint8_t>((century19 ? year - 1900 : year - 2000) % 100);
  const int wday = utc.tm_wday <= 0 ? 1 : utc.tm_wday;

  Wire.beginTransmission(I2C_ADDR_DS3231);
  Wire.write(DS3231_SEC_REG);
  Wire.write(decToBcd(static_cast<uint8_t>(utc.tm_sec)));
  Wire.write(decToBcd(static_cast<uint8_t>(utc.tm_min)));
  Wire.write(decToBcd(static_cast<uint8_t>(utc.tm_hour)));
  Wire.write(decToBcd(static_cast<uint8_t>(wday)));
  Wire.write(decToBcd(static_cast<uint8_t>(utc.tm_mday)));
  Wire.write(decToBcd(static_cast<uint8_t>(utc.tm_mon + 1)) | (century19 ? 0x80U : 0U));
  Wire.write(decToBcd(yearBcd));
  if (Wire.endTransmission() != 0) {
    LOG_ERR("CLK", "Failed to write datetime to DS3231");
    return false;
  }

  _cachedUtcTm = utc;
  _lastPollMs = 0;
  _hasCachedTime = true;
  return true;
}

bool HalClock::writeUtcTm(const struct tm& utc) {
  if (!_available) {
    return false;
  }
  return writeUtcTmToChip(utc);
}

bool HalClock::getUtcTime(uint8_t& hour, uint8_t& minute, const bool forceRefresh) const {
  struct tm utc{};
  if (!readUtcTm(utc, forceRefresh)) {
    return false;
  }
  hour = static_cast<uint8_t>(utc.tm_hour);
  minute = static_cast<uint8_t>(utc.tm_min);
  return true;
}

bool HalClock::syncFromNTP() {
  if (!_available) {
    return false;
  }

  if (WiFi.status() != WL_CONNECTED) {
    LOG_ERR("CLK", "WiFi not connected, cannot sync NTP");
    return false;
  }

  LOG_INF("CLK", "Starting NTP sync...");
  configTzTime("UTC0", "pool.ntp.org", "time.nist.gov");

  constexpr int maxAttempts = 50;
  for (int i = 0; i < maxAttempts; i++) {
    if (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
      const time_t now = time(nullptr);
      struct tm utc{};
      gmtime_r(&now, &utc);

      if (writeUtcTm(utc)) {
        LOG_INF("CLK", "RTC set to %04d-%02d-%02d %02d:%02d:%02d UTC", utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday,
                utc.tm_hour, utc.tm_min, utc.tm_sec);
        return true;
      }
      return false;
    }
    delay(100);
  }

  LOG_ERR("CLK", "NTP sync timed out");
  return false;
}
