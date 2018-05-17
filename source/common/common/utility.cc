#include "common/common/utility.h"

#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <regex>
#include <string>

#include "envoy/common/exception.h"

#include "common/common/assert.h"
#include "common/common/empty_string.h"
#include "common/common/fmt.h"
#include "common/common/hash.h"
#include "common/singleton/const_singleton.h"

#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "spdlog/spdlog.h"

namespace Envoy {
namespace {

class CustomFieldValues {
public:
  const std::string SUBSECOND{"f"};
  const std::regex SUBSECOND_PATTERN{"%([1-9])?f"};
};

typedef ConstSingleton<CustomFieldValues> CustomFieldNames;

} // namespace

std::string DateFormatter::fromTime(const SystemTime& time, const bool use_regex) const {
  const std::string subsecond_str =
      fmt::FormatInt(
          std::chrono::duration_cast<std::chrono::nanoseconds>(time.time_since_epoch()).count())
          .str()
          .substr(10);
  absl::string_view new_format_string =
      use_regex ? setCustomFieldRegex(CustomFieldNames::get().SUBSECOND_PATTERN, subsecond_str)
                : setCustomField(CustomFieldNames::get().SUBSECOND, subsecond_str);

  const time_t current_time = std::chrono::system_clock::to_time_t(time);
  if (new_format_string.empty()) {
    return fromTime(current_time);
  }

  // Send the newly formatted format string to stftime.
  return fromTime(current_time, new_format_string);
}

std::string DateFormatter::fromTime(time_t time, absl::string_view new_format_string) const {
  tm current_tm;
  gmtime_r(&time, &current_tm);

  std::array<char, 1024> buf;
  strftime(&buf[0], buf.size(), new_format_string.data(), &current_tm);
  return std::string(&buf[0]);
}

std::string DateFormatter::setCustomField(const std::string& field,
                                          const std::string& value) const {
  size_t found = format_string_.find(field);
  if (found == std::string::npos) {
    // If no field specifier inside the current format string, return an empty string and
    // subsequently use the default formatter.
    return EMPTY_STRING;
  }

  size_t index = found;
  std::string new_format_string = format_string_;

  while (found != std::string::npos) {
    index = found;

    size_t start = found - 1;
    if (start < new_format_string.size()) {
      absl::string_view sub = new_format_string.substr(start, 2);
      if (sub.front() == '%' && sub.size() == 2) {
        // This field definitely has no width specifier, i.e. %f.
        new_format_string.replace(start, 2, value);
      } else {
        start = found - 2;

        // This field potentially has a valid width specifier, e.g. %3f.
        if (start < new_format_string.size()) {
          absl::string_view sub = new_format_string.substr(start, 2);

          if (sub.front() == '%' && sub.size() == 2) {
            uint64_t width;
            if (StringUtil::atoul(sub.substr(1, 1).data(), width)) {
              new_format_string.replace(start, 3, value.substr(0, width));
            }
          }
        }
      }
    }

    found = new_format_string.find(field, ++index);
  }

  return new_format_string;
}

std::string DateFormatter::setCustomFieldRegex(const std::regex& field,
                                               const std::string& value) const {
  std::string new_format_string = format_string_;
  std::smatch result;
  while (regex_search(new_format_string, result, field)) {
    ASSERT(result.size() == 2);
    const std::string& width_specifier = result[1];
    const uint32_t width = width_specifier.empty() ? value.size() : std::stoul(result[1]);
    new_format_string.replace(result.position(), result.length(), value.substr(0, width));
  }
  return new_format_string;
}

std::string DateFormatter::fromTime(time_t time) const {
  tm current_tm;
  gmtime_r(&time, &current_tm);

  std::array<char, 1024> buf;
  strftime(&buf[0], buf.size(), format_string_.c_str(), &current_tm);
  return std::string(&buf[0]);
}

std::string DateFormatter::now() {
  time_t current_time_t;
  time(&current_time_t);
  return fromTime(current_time_t);
}

ProdSystemTimeSource ProdSystemTimeSource::instance_;
ProdMonotonicTimeSource ProdMonotonicTimeSource::instance_;

ConstMemoryStreamBuffer::ConstMemoryStreamBuffer(const char* data, size_t size) {
  // std::streambuf won't modify `data`, but the interface still requires a char* for convenience,
  // so we need to const_cast.
  char* ptr = const_cast<char*>(data);

  this->setg(ptr, ptr, ptr + size);
}

InputConstMemoryStream::InputConstMemoryStream(const char* data, size_t size)
    : ConstMemoryStreamBuffer{data, size}, std::istream{static_cast<std::streambuf*>(this)} {}

bool DateUtil::timePointValid(SystemTime time_point) {
  return std::chrono::duration_cast<std::chrono::milliseconds>(time_point.time_since_epoch())
             .count() != 0;
}

bool DateUtil::timePointValid(MonotonicTime time_point) {
  return std::chrono::duration_cast<std::chrono::milliseconds>(time_point.time_since_epoch())
             .count() != 0;
}

const char StringUtil::WhitespaceChars[] = " \t\f\v\n\r";

bool StringUtil::atoul(const char* str, uint64_t& out, int base) {
  if (strlen(str) == 0) {
    return false;
  }

  char* end_ptr;
  errno = 0;
  out = strtoul(str, &end_ptr, base);
  if (*end_ptr != '\0' || (out == ULONG_MAX && errno == ERANGE)) {
    return false;
  } else {
    return true;
  }
}

bool StringUtil::atol(const char* str, int64_t& out, int base) {
  if (strlen(str) == 0) {
    return false;
  }

  char* end_ptr;
  errno = 0;
  out = strtol(str, &end_ptr, base);
  if (*end_ptr != '\0' || ((out == LONG_MAX || out == LONG_MIN) && errno == ERANGE)) {
    return false;
  } else {
    return true;
  }
}

absl::string_view StringUtil::ltrim(absl::string_view source) {
  const absl::string_view::size_type pos = source.find_first_not_of(WhitespaceChars);
  if (pos != absl::string_view::npos) {
    source.remove_prefix(pos);
  } else {
    source.remove_prefix(source.size());
  }
  return source;
}

absl::string_view StringUtil::rtrim(absl::string_view source) {
  const absl::string_view::size_type pos = source.find_last_not_of(WhitespaceChars);
  if (pos != absl::string_view::npos) {
    source.remove_suffix(source.size() - pos - 1);
  } else {
    source.remove_suffix(source.size());
  }
  return source;
}

absl::string_view StringUtil::trim(absl::string_view source) { return ltrim(rtrim(source)); }

bool StringUtil::findToken(absl::string_view source, absl::string_view delimiters,
                           absl::string_view key_token, bool trim_whitespace) {
  const auto tokens = splitToken(source, delimiters, trim_whitespace);
  if (trim_whitespace) {
    for (const auto token : tokens) {
      if (key_token == trim(token)) {
        return true;
      }
    }
    return false;
  }

  return std::find(tokens.begin(), tokens.end(), key_token) != tokens.end();
}

bool StringUtil::caseFindToken(absl::string_view source, absl::string_view delimiters,
                               absl::string_view key_token, bool trim_whitespace) {
  const auto tokens = splitToken(source, delimiters, trim_whitespace);
  std::function<bool(absl::string_view)> predicate;

  if (trim_whitespace) {
    predicate = [&](absl::string_view token) { return caseCompare(key_token, trim(token)); };
  } else {
    predicate = [&](absl::string_view token) { return caseCompare(key_token, token); };
  }

  return std::find_if(tokens.begin(), tokens.end(), predicate) != tokens.end();
}

bool StringUtil::caseCompare(absl::string_view lhs, absl::string_view rhs) {
  if (rhs.size() != lhs.size()) {
    return false;
  }
  return absl::StartsWithIgnoreCase(rhs, lhs);
}

absl::string_view StringUtil::cropRight(absl::string_view source, absl::string_view delimiter) {
  const absl::string_view::size_type pos = source.find(delimiter);
  if (pos != absl::string_view::npos) {
    source.remove_suffix(source.size() - pos);
  }
  return source;
}

absl::string_view StringUtil::cropLeft(absl::string_view source, absl::string_view delimiter) {
  const absl::string_view::size_type pos = source.find(delimiter);
  if (pos != absl::string_view::npos) {
    source.remove_prefix(pos + delimiter.size());
  }
  return source;
}

std::vector<absl::string_view> StringUtil::splitToken(absl::string_view source,
                                                      absl::string_view delimiters,
                                                      bool keep_empty_string) {
  if (keep_empty_string) {
    return absl::StrSplit(source, absl::ByAnyChar(delimiters));
  }
  return absl::StrSplit(source, absl::ByAnyChar(delimiters), absl::SkipEmpty());
}

uint32_t StringUtil::itoa(char* out, size_t buffer_size, uint64_t i) {
  // The maximum size required for an unsigned 64-bit integer is 21 chars (including null).
  if (buffer_size < 21) {
    throw std::invalid_argument("itoa buffer too small");
  }

  char* current = out;
  do {
    *current++ = "0123456789"[i % 10];
    i /= 10;
  } while (i > 0);

  for (uint64_t i = 0, j = current - out - 1; i < j; i++, j--) {
    char c = out[i];
    out[i] = out[j];
    out[j] = c;
  }

  *current = 0;
  return current - out;
}

size_t StringUtil::strlcpy(char* dst, const char* src, size_t size) {
  strncpy(dst, src, size - 1);
  dst[size - 1] = '\0';
  return strlen(src);
}

std::string StringUtil::join(const std::vector<std::string>& source, const std::string& delimiter) {
  std::ostringstream buf;
  std::copy(source.begin(), source.end(),
            std::ostream_iterator<std::string>(buf, delimiter.c_str()));
  std::string ret = buf.str();
  // copy will always end with an extra delimiter, we remove it here.
  return ret.substr(0, ret.length() - delimiter.length());
}

std::string StringUtil::subspan(absl::string_view source, size_t start, size_t end) {
  return std::string(source.data() + start, end - start);
}

std::string StringUtil::escape(const std::string& source) {
  std::string ret;

  // Prevent unnecessary allocation by allocating 2x original size.
  ret.reserve(source.length() * 2);
  for (char c : source) {
    switch (c) {
    case '\r':
      ret += "\\r";
      break;
    case '\n':
      ret += "\\n";
      break;
    case '\t':
      ret += "\\t";
      break;
    case '"':
      ret += "\\\"";
      break;
    default:
      ret += c;
      break;
    }
  }

  return ret;
}

std::string AccessLogDateTimeFormatter::fromTime(const SystemTime& time) {
  static const char DefaultDateFormat[] = "%Y-%m-%dT%H:%M:%S.000Z";

  struct CachedTime {
    std::chrono::seconds epoch_time_seconds;
    size_t formatted_time_length{0};
    char formatted_time[32];
  };
  static thread_local CachedTime cached_time;

  const std::chrono::milliseconds epoch_time_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(time.time_since_epoch());

  const std::chrono::seconds epoch_time_seconds =
      std::chrono::duration_cast<std::chrono::seconds>(epoch_time_ms);

  if (cached_time.formatted_time_length == 0 ||
      cached_time.epoch_time_seconds != epoch_time_seconds) {
    time_t time = static_cast<time_t>(epoch_time_seconds.count());
    tm date_time;
    gmtime_r(&time, &date_time);
    cached_time.formatted_time_length =
        strftime(cached_time.formatted_time, sizeof(cached_time.formatted_time), DefaultDateFormat,
                 &date_time);
    cached_time.epoch_time_seconds = epoch_time_seconds;
  }

  ASSERT(cached_time.formatted_time_length == 24 &&
         cached_time.formatted_time_length < sizeof(cached_time.formatted_time));

  // Overwrite the digits in the ".000Z" at the end of the string with the
  // millisecond count from the input time.
  size_t offset = cached_time.formatted_time_length - 4;
  uint32_t msec = epoch_time_ms.count() % 1000;
  cached_time.formatted_time[offset++] = ('0' + (msec / 100));
  msec %= 100;
  cached_time.formatted_time[offset++] = ('0' + (msec / 10));
  msec %= 10;
  cached_time.formatted_time[offset++] = ('0' + msec);

  return cached_time.formatted_time;
}

bool StringUtil::endsWith(const std::string& source, const std::string& end) {
  if (source.length() < end.length()) {
    return false;
  }

  size_t start_position = source.length() - end.length();
  return std::equal(source.begin() + start_position, source.end(), end.begin());
}

bool StringUtil::startsWith(const char* source, const std::string& start, bool case_sensitive) {
  if (case_sensitive) {
    return strncmp(source, start.c_str(), start.size()) == 0;
  } else {
    return strncasecmp(source, start.c_str(), start.size()) == 0;
  }
}

const std::string& StringUtil::nonEmptyStringOrDefault(const std::string& s,
                                                       const std::string& default_value) {
  return s.empty() ? default_value : s;
}

std::string StringUtil::toUpper(absl::string_view s) {
  std::string upper_s;
  upper_s.reserve(s.size());
  std::transform(s.cbegin(), s.cend(), std::back_inserter(upper_s), absl::ascii_toupper);
  return upper_s;
}

bool StringUtil::CaseInsensitiveCompare::operator()(absl::string_view lhs,
                                                    absl::string_view rhs) const {
  return StringUtil::caseCompare(lhs, rhs);
}

uint64_t StringUtil::CaseInsensitiveHash::operator()(absl::string_view key) const {
  return HashUtil::djb2CaseInsensitiveHash(key);
}

std::string StringUtil::removeCharacters(const absl::string_view& str,
                                         const IntervalSet<size_t>& remove_characters) {
  std::string ret;
  size_t pos = 0;
  const auto intervals = remove_characters.toVector();
  std::vector<absl::string_view> pieces;
  pieces.reserve(intervals.size());
  for (const auto& interval : intervals) {
    if (interval.first != pos) {
      ASSERT(interval.second <= str.size());
      pieces.push_back(str.substr(pos, interval.first - pos));
    }
    pos = interval.second;
  }
  if (pos != str.size()) {
    pieces.push_back(str.substr(pos));
  }
  return absl::StrJoin(pieces, "");
}

bool Primes::isPrime(uint32_t x) {
  if (x < 4) {
    return true; // eliminates special-casing 2.
  } else if ((x & 1) == 0) {
    return false; // eliminates even numbers >2.
  }

  uint32_t limit = sqrt(x);
  for (uint32_t factor = 3; factor <= limit; factor += 2) {
    if ((x % factor) == 0) {
      return false;
    }
  }
  return true;
}

uint32_t Primes::findPrimeLargerThan(uint32_t x) {
  x += (x % 2) + 1;
  while (!isPrime(x)) {
    x += 2;
  }
  return x;
}

std::regex RegexUtil::parseRegex(const std::string& regex, std::regex::flag_type flags) {
  // TODO(zuercher): In the future, PGV (https://github.com/lyft/protoc-gen-validate) annotations
  // may allow us to remove this in favor of direct validation of regular expressions.
  try {
    return std::regex(regex, flags);
  } catch (const std::regex_error& e) {
    throw EnvoyException(fmt::format("Invalid regex '{}': {}", regex, e.what()));
  }
}

// https://en.wikipedia.org/wiki/Algorithms_for_calculating_variance#Online_algorithm
void WelfordStandardDeviation::update(double newValue) {
  ++count_;
  const double delta = newValue - mean_;
  mean_ += delta / count_;
  const double delta2 = newValue - mean_;
  m2_ += delta * delta2;
}

double WelfordStandardDeviation::computeVariance() const {
  if (count_ < 2) {
    return std::nan("");
  }
  return m2_ / (count_ - 1);
}

double WelfordStandardDeviation::computeStandardDeviation() const {
  const double variance = computeVariance();
  // It seems very difficult for variance to go negative, but from the calculation in update()
  // above, I can't quite convince myself it's impossible, so put in a guard to be sure.
  return (std::isnan(variance) || variance < 0) ? std::nan("") : sqrt(variance);
}

} // namespace Envoy
