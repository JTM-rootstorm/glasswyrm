#include "backends/drm/dumb_buffer.hpp"
#include "backends/drm/resources.hpp"
#include "backends/output/software_frame.hpp"

#include "tests/helpers/test_support.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace {

enum class Failure { None, Create, AddFramebuffer, MapDumb, MapMemory };

class FakeDumbBufferApi final : public glasswyrm::drm::DumbBufferApi {
 public:
  glasswyrm::drm::DumbAllocation allocation{7, 12, 24};
  Failure failure{Failure::None};
  std::size_t fail_create_call{};
  std::size_t create_calls{};
  bool zero_framebuffer_id{false};
  std::uint32_t next_handle{7};
  std::uint32_t next_framebuffer{70};
  std::vector<std::string> calls;
  std::vector<std::byte> mapping;
  std::vector<std::vector<std::byte>> additional_mappings;

  FakeDumbBufferApi() { calls.reserve(64); }

  bool create_dumb(const std::uint32_t width, const std::uint32_t height,
                   const std::uint32_t bits_per_pixel,
                   glasswyrm::drm::DumbAllocation& output,
                   std::string& error) override {
    calls.push_back("create");
    ++create_calls;
    gw::test::require(width != 0 && height != 0 && bits_per_pixel == 32,
                      "CREATE_DUMB arguments");
    if (failure == Failure::Create || create_calls == fail_create_call) {
      error = "injected CREATE_DUMB failure";
      return false;
    }
    output = allocation;
    output.handle = allocation.handle == 0 ? 0 : next_handle++;
    return true;
  }

  bool add_framebuffer2(const std::uint32_t handle,
                        const std::uint32_t width,
                        const std::uint32_t height,
                        const std::uint32_t pitch,
                        const std::uint32_t format,
                        std::uint32_t& framebuffer_id,
                        std::string& error) override {
    calls.push_back("addfb2");
    gw::test::require(handle != 0 && width != 0 && height != 0 &&
                          pitch == allocation.pitch &&
                          format == glasswyrm::drm::kFormatXrgb8888,
                      "AddFB2 XRGB8888 arguments");
    if (failure == Failure::AddFramebuffer) {
      error = "injected AddFB2 failure";
      return false;
    }
    framebuffer_id = zero_framebuffer_id ? 0 : next_framebuffer++;
    return true;
  }

  bool map_dumb(const std::uint32_t handle, std::uint64_t& offset,
                std::string& error) override {
    calls.push_back("map_dumb");
    gw::test::require(handle != 0, "MAP_DUMB handle");
    if (failure == Failure::MapDumb) {
      error = "injected MAP_DUMB failure";
      return false;
    }
    offset = 4096;
    return true;
  }

  std::byte* map_memory(const std::uint64_t offset, const std::size_t size,
                        std::string& error) override {
    calls.push_back("mmap");
    gw::test::require(offset == 4096 && size == allocation.size,
                      "mmap arguments");
    if (failure == Failure::MapMemory) {
      error = "injected mmap failure";
      return nullptr;
    }
    if (mapping.empty()) {
      mapping.assign(size, std::byte{0xa5});
      return mapping.data();
    }
    additional_mappings.emplace_back(size, std::byte{0xa5});
    return additional_mappings.back().data();
  }

  void remove_framebuffer(const std::uint32_t framebuffer_id) noexcept override {
    calls.push_back("rmfb:" + std::to_string(framebuffer_id));
  }
  void unmap_memory(std::byte*, const std::size_t size) noexcept override {
    calls.push_back("unmap:" + std::to_string(size));
  }
  void destroy_dumb(const std::uint32_t handle) noexcept override {
    calls.push_back("destroy:" + std::to_string(handle));
  }
};

bool ends_with(const std::vector<std::string>& values,
               const std::span<const std::string> suffix) {
  return values.size() >= suffix.size() &&
         std::equal(suffix.begin(), suffix.end(),
                    values.end() - static_cast<std::ptrdiff_t>(suffix.size()));
}

}  // namespace

int main() {
  using namespace glasswyrm::drm;
  std::string error;

  FakeDumbBufferApi api;
  DumbBuffer buffer;
  gw::test::require(DumbBuffer::create(api, 2, 2, buffer, error),
                    "CREATE/MAP/ADDFB2 lifecycle succeeds");
  gw::test::require(buffer.valid() && buffer.handle() == 7 &&
                        buffer.framebuffer_id() == 70 && buffer.pitch() == 12 &&
                        buffer.size() == 24,
                    "validated dumb-buffer metadata retained");
  gw::test::require(std::all_of(api.mapping.begin(), api.mapping.end(),
                               [](const std::byte byte) {
                                 return byte == std::byte{0};
                               }),
                    "complete mapping deterministically cleared");

  const std::vector<std::uint32_t> pixels{
      0xff112233U, 0x00445566U, 0xff778899U, 0xffaabbccU};
  std::fill(api.mapping.begin(), api.mapping.end(), std::byte{0xa5});
  gw::test::require(buffer.copy_from(pixels, error), "full frame copy succeeds");
  for (std::size_t row = 0; row < 2; ++row) {
    const auto visible_begin = row * 12;
    gw::test::require(std::equal(
                          api.mapping.begin() + visible_begin,
                          api.mapping.begin() + visible_begin + 8,
                          reinterpret_cast<const std::byte*>(pixels.data()) +
                              row * 8),
                      "visible row copied completely");
    gw::test::require(std::all_of(api.mapping.begin() + visible_begin + 8,
                                 api.mapping.begin() + visible_begin + 12,
                                 [](const std::byte byte) {
                                   return byte == std::byte{0};
                                 }),
                      "pitch padding remains zero");
  }
  gw::test::require(
      buffer.visible_hash() == glasswyrm::output::hash_visible_xrgb8888(pixels) &&
          buffer.visible_hash() == 0x4d1416c2755838b5ULL,
      "scanout visible RGB hash equals canonical software-frame hash");
  const std::vector<std::uint32_t> wrong_pixels{0xff000000U};
  gw::test::require(!buffer.copy_from(wrong_pixels, error),
                    "mismatched canonical frame rejected");
  buffer.reset();
  const std::vector<std::string> cleanup{"rmfb:70", "unmap:24", "destroy:7"};
  gw::test::require(ends_with(api.calls, cleanup),
                    "RMFB precedes unmap and dumb-handle destruction");

  FakeDumbBufferApi invalid;
  DumbBuffer rejected;
  invalid.allocation.handle = 0;
  gw::test::require(!DumbBuffer::create(invalid, 2, 2, rejected, error) &&
                        invalid.calls.back() == "create",
                    "zero returned handle rejected");
  invalid = FakeDumbBufferApi{};
  invalid.allocation.pitch = 7;
  invalid.allocation.size = 14;
  gw::test::require(!DumbBuffer::create(invalid, 2, 2, rejected, error) &&
                        ends_with(invalid.calls,
                                  std::array<std::string, 2>{"create", "destroy:7"}),
                    "undersized pitch rejected and handle destroyed");
  invalid = FakeDumbBufferApi{};
  invalid.zero_framebuffer_id = true;
  gw::test::require(!DumbBuffer::create(invalid, 2, 2, rejected, error) &&
                        ends_with(invalid.calls,
                                  std::array<std::string, 2>{"addfb2", "destroy:7"}),
                    "zero AddFB2 framebuffer ID rejected");
  invalid = FakeDumbBufferApi{};
  invalid.allocation.size = 23;
  gw::test::require(!DumbBuffer::create(invalid, 2, 2, rejected, error),
                    "size below pitch times height rejected");
  invalid = FakeDumbBufferApi{};
  invalid.allocation.size = DumbBuffer::kMaximumBytes + 1;
  gw::test::require(!DumbBuffer::create(invalid, 2, 2, rejected, error),
                    "64 MiB mapping ceiling enforced");
  invalid = FakeDumbBufferApi{};
  gw::test::require(!DumbBuffer::create(invalid, 0, 2, rejected, error) &&
                        invalid.calls.empty(),
                    "zero dimensions rejected before CREATE_DUMB");

  for (const auto failure : {Failure::Create, Failure::AddFramebuffer,
                             Failure::MapDumb, Failure::MapMemory}) {
    FakeDumbBufferApi failing;
    failing.failure = failure;
    DumbBuffer partial;
    gw::test::require(!DumbBuffer::create(failing, 2, 2, partial, error),
                      "injected construction failure propagates");
    if (failure == Failure::AddFramebuffer)
      gw::test::require(ends_with(
                            failing.calls,
                            std::array<std::string, 2>{"addfb2", "destroy:7"}),
                        "AddFB2 failure destroys created handle");
    if (failure == Failure::MapDumb)
      gw::test::require(ends_with(
                            failing.calls,
                            std::array<std::string, 3>{"map_dumb", "rmfb:70",
                                                       "destroy:7"}),
                        "MAP_DUMB failure removes FB before handle");
    if (failure == Failure::MapMemory)
      gw::test::require(ends_with(
                            failing.calls,
                            std::array<std::string, 3>{"mmap", "rmfb:70",
                                                       "destroy:7"}),
                        "mmap failure removes FB before handle");
  }

  FakeDumbBufferApi pair_api;
  DumbBufferPair pair;
  gw::test::require(DumbBufferPair::create(pair_api, 2, 2, pair, error) &&
                        pair_api.create_calls == 2 &&
                        pair.front().handle() != pair.back().handle(),
                    "presenter buffer pair owns exactly two buffers");
  const auto old_back = pair.back().handle();
  pair.promote_back();
  gw::test::require(pair.front().handle() == old_back,
                    "completed back buffer becomes front buffer");

  FakeDumbBufferApi partial_pair_api;
  partial_pair_api.fail_create_call = 2;
  DumbBufferPair partial_pair;
  gw::test::require(
      !DumbBufferPair::create(partial_pair_api, 2, 2, partial_pair, error) &&
          ends_with(partial_pair_api.calls,
                    std::array<std::string, 4>{"create", "rmfb:70", "unmap:24",
                                               "destroy:7"}),
      "second-buffer failure fully unwinds the first buffer");
  return 0;
}
