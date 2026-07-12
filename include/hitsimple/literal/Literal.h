#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace hitsimple::literal {

struct DecodeResult {
  std::string bytes;
  std::optional<std::string> error;

  explicit operator bool() const { return !error.has_value(); }
};

DecodeResult decodeCharLiteral(std::string_view literal);
DecodeResult decodeStringLiteral(std::string_view literal);
std::optional<std::uint64_t> bytesToUnsignedInteger(std::string_view bytes);
std::optional<std::uint64_t> parseUnsignedIntegerLiteral(std::string_view text);

} // namespace hitsimple::literal
