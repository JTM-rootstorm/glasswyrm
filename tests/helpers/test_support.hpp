#pragma once

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <span>
#include <string_view>

namespace gw::test {

inline void require(const bool condition, const std::string_view message) {
  if (!condition) {
    std::cerr << "test failure: " << message << '\n';
    std::exit(1);
  }
}

inline void require_bytes_equal(const std::span<const std::uint8_t> actual,
                                const std::span<const std::uint8_t> expected,
                                const std::string_view message) {
  if (actual.size() != expected.size()) {
    std::cerr << "test failure: " << message << ": size " << actual.size()
              << " != " << expected.size() << '\n';
    std::exit(1);
  }
  for (std::size_t index = 0; index < actual.size(); ++index) {
    if (actual[index] != expected[index]) {
      std::cerr << "test failure: " << message << ": byte " << index
                << " differs\n";
      std::exit(1);
    }
  }
}

} // namespace gw::test
