#include "hitsimple/literal/Literal.h"

#include <cstddef>
#include <limits>

namespace hitsimple::literal {
namespace {

bool isHexDigit(char ch) {
  return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') ||
         (ch >= 'A' && ch <= 'F');
}

bool isOctalDigit(char ch) { return ch >= '0' && ch <= '7'; }

std::uint32_t hexValue(char ch) {
  if (ch >= '0' && ch <= '9') {
    return static_cast<std::uint32_t>(ch - '0');
  }
  if (ch >= 'a' && ch <= 'f') {
    return static_cast<std::uint32_t>(ch - 'a' + 10);
  }
  return static_cast<std::uint32_t>(ch - 'A' + 10);
}

void appendUtf8(std::string& out, std::uint32_t codePoint) {
  if (codePoint <= 0x7fU) {
    out.push_back(static_cast<char>(codePoint));
    return;
  }
  if (codePoint <= 0x7ffU) {
    out.push_back(static_cast<char>(0xc0U | (codePoint >> 6U)));
    out.push_back(static_cast<char>(0x80U | (codePoint & 0x3fU)));
    return;
  }
  out.push_back(static_cast<char>(0xe0U | (codePoint >> 12U)));
  out.push_back(static_cast<char>(0x80U | ((codePoint >> 6U) & 0x3fU)));
  out.push_back(static_cast<char>(0x80U | (codePoint & 0x3fU)));
}

DecodeResult decodeQuotedLiteral(std::string_view literal, char quote) {
  DecodeResult result;
  std::size_t begin = 0;
  std::size_t end = literal.size();
  if (literal.size() >= 2 && literal.front() == quote &&
      literal.back() == quote) {
    begin = 1;
    end = literal.size() - 1;
  }

  for (std::size_t i = begin; i < end; ++i) {
    if (literal[i] != '\\') {
      result.bytes.push_back(literal[i]);
      continue;
    }

    ++i;
    if (i >= end) {
      result.error = "unterminated escape sequence";
      return result;
    }

    switch (literal[i]) {
    case 'n':
      result.bytes.push_back('\n');
      continue;
    case 't':
      result.bytes.push_back('\t');
      continue;
    case 'r':
      result.bytes.push_back('\r');
      continue;
    case '\\':
      result.bytes.push_back('\\');
      continue;
    case '\'':
      result.bytes.push_back('\'');
      continue;
    case '"':
      result.bytes.push_back('"');
      continue;
    case 'x': {
      if (i + 2 >= end || !isHexDigit(literal[i + 1]) ||
          !isHexDigit(literal[i + 2])) {
        result.error = "invalid hexadecimal escape";
        return result;
      }
      const auto value = (hexValue(literal[i + 1]) << 4U) |
                         hexValue(literal[i + 2]);
      result.bytes.push_back(static_cast<char>(value));
      i += 2;
      continue;
    }
    case 'u': {
      if (i + 4 >= end || !isHexDigit(literal[i + 1]) ||
          !isHexDigit(literal[i + 2]) || !isHexDigit(literal[i + 3]) ||
          !isHexDigit(literal[i + 4])) {
        result.error = "invalid unicode escape";
        return result;
      }
      std::uint32_t codePoint = 0;
      for (std::size_t offset = 1; offset <= 4; ++offset) {
        codePoint = (codePoint << 4U) | hexValue(literal[i + offset]);
      }
      if (codePoint >= 0xd800U && codePoint <= 0xdfffU) {
        result.error = "unicode surrogate code point is not valid";
        return result;
      }
      appendUtf8(result.bytes, codePoint);
      i += 4;
      continue;
    }
    default:
      break;
    }

    if (isOctalDigit(literal[i])) {
      std::uint32_t value = 0;
      std::size_t count = 0;
      while (i < end && count < 3 && isOctalDigit(literal[i])) {
        value = (value * 8U) + static_cast<std::uint32_t>(literal[i] - '0');
        ++i;
        ++count;
      }
      --i;
      if (value > 0377U) {
        result.error = "octal escape is out of range";
        return result;
      }
      result.bytes.push_back(static_cast<char>(value));
      continue;
    }

    result.error = "invalid escape sequence";
    return result;
  }

  return result;
}

} // namespace

DecodeResult decodeCharLiteral(std::string_view literal) {
  return decodeQuotedLiteral(literal, '\'');
}

DecodeResult decodeStringLiteral(std::string_view literal) {
  return decodeQuotedLiteral(literal, '"');
}

std::optional<std::uint64_t> bytesToUnsignedInteger(std::string_view bytes) {
  if (bytes.empty() || bytes.size() > sizeof(std::uint64_t)) {
    return std::nullopt;
  }

  std::uint64_t value = 0;
  for (std::size_t index = 0; index < bytes.size(); ++index) {
    value |= static_cast<std::uint64_t>(
                 static_cast<unsigned char>(bytes[index]))
             << (index * 8U);
  }
  return value;
}

std::optional<std::uint64_t> parseUnsignedIntegerLiteral(std::string_view text) {
  if (text.empty()) {
    return std::nullopt;
  }

  int base = 10;
  std::size_t index = 0;
  if (text.size() >= 2 && text[0] == '0' &&
      (text[1] == 'x' || text[1] == 'X')) {
    base = 16;
    index = 2;
  } else if (text.size() >= 2 && text[0] == '0' &&
             (text[1] == 'b' || text[1] == 'B')) {
    base = 2;
    index = 2;
  } else if (text.size() >= 2 && text[0] == '0' &&
             (text[1] == 'o' || text[1] == 'O')) {
    base = 8;
    index = 2;
  } else if (text.size() > 1 && text[0] == '0') {
    base = 8;
    index = 1;
  }

  std::uint64_t value = 0;
  bool hasDigit = false;
  for (; index < text.size(); ++index) {
    const char ch = text[index];
    if (ch == '_') {
      continue;
    }

    std::uint32_t digit = 0;
    if (ch >= '0' && ch <= '9') {
      digit = static_cast<std::uint32_t>(ch - '0');
    } else if (ch >= 'a' && ch <= 'f') {
      digit = static_cast<std::uint32_t>(ch - 'a' + 10);
    } else if (ch >= 'A' && ch <= 'F') {
      digit = static_cast<std::uint32_t>(ch - 'A' + 10);
    } else {
      return std::nullopt;
    }

    if (digit >= static_cast<std::uint32_t>(base)) {
      return std::nullopt;
    }

    const auto max = std::numeric_limits<std::uint64_t>::max();
    if (value > (max - digit) / static_cast<std::uint32_t>(base)) {
      return std::nullopt;
    }
    value = value * static_cast<std::uint32_t>(base) + digit;
    hasDigit = true;
  }

  if (!hasDigit) {
    return std::nullopt;
  }
  return value;
}

} // namespace hitsimple::literal
