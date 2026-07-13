#include <chrono>
#include <cstdlib>
#include <ctime>
#include <sys/time.h>

int main() {
  constexpr std::time_t expected = 1767323045;
  const auto monotonic_before = std::chrono::steady_clock::now();
  std::time_t stored{};
  timeval value{};
  timespec realtime{};
  if (std::time(&stored) != expected || stored != expected ||
      gettimeofday(&value, nullptr) != 0 || value.tv_sec != expected ||
      value.tv_usec != 0 || clock_gettime(CLOCK_REALTIME, &realtime) != 0 ||
      realtime.tv_sec != expected || realtime.tv_nsec != 0) return 1;
  const auto monotonic_after = std::chrono::steady_clock::now();
  return monotonic_after < monotonic_before ? 2 : 0;
}
