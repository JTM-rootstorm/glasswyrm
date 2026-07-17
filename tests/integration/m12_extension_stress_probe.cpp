#include "glasswyrmd/published_buffer.hpp"
#include "glasswyrmd/resource_table.hpp"

#include <dirent.h>
#include <fcntl.h>
#include <sys/shm.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <ostream>
#include <span>
#include <string_view>
#include <vector>

namespace {

using namespace glasswyrm::server;
using glasswyrm::geometry::Rectangle;

constexpr ClientId kOwner = 41;
constexpr ClientId kReuseOwner = 42;
constexpr std::uint32_t kResourceBase = 0x400000U;
constexpr std::uint32_t kResourceMask = 0x1FFFFFU;
constexpr std::size_t kIterations = 64;

[[noreturn]] void fail(const std::string_view message) {
  std::fprintf(stderr, "m12 extension stress probe: %.*s\n",
               static_cast<int>(message.size()), message.data());
  std::exit(1);
}

void require(const bool condition, const std::string_view message) {
  if (!condition) fail(message);
}

class SysvSegment {
 public:
  explicit SysvSegment(const std::size_t size)
      : id_(::shmget(IPC_PRIVATE, size, IPC_CREAT | 0600)), size_(size) {
    require(id_ >= 0, "create private SysV SHM segment");
  }

  ~SysvSegment() {
    if (id_ >= 0) (void)::shmctl(id_, IPC_RMID, nullptr);
  }

  SysvSegment(const SysvSegment&) = delete;
  SysvSegment& operator=(const SysvSegment&) = delete;

  [[nodiscard]] int id() const noexcept { return id_; }
  [[nodiscard]] std::size_t size() const noexcept { return size_; }

  [[nodiscard]] std::size_t attachment_count() const {
    struct shmid_ds status {};
    require(::shmctl(id_, IPC_STAT, &status) == 0,
            "inspect private SysV SHM segment");
    return static_cast<std::size_t>(status.shm_nattch);
  }

 private:
  int id_{-1};
  std::size_t size_{};
};

std::size_t open_descriptor_count() {
  DIR* const directory = ::opendir("/proc/self/fd");
  require(directory != nullptr, "open /proc/self/fd");
  std::size_t count = 0;
  errno = 0;
  while (const auto* entry = ::readdir(directory)) {
    const std::string_view name{entry->d_name};
    if (name != "." && name != "..") ++count;
  }
  const int read_error = errno;
  require(::closedir(directory) == 0, "close /proc/self/fd");
  require(read_error == 0, "enumerate /proc/self/fd");
  return count;
}

ResourceLimits stress_limits(const std::size_t segment_size) {
  ResourceLimits limits;
  limits.maximum_shm_segments_per_client = 1;
  limits.maximum_shm_bytes_per_client = segment_size;
  limits.maximum_xfixes_regions_per_client = 1;
  limits.maximum_xfixes_region_rectangles = 1;
  limits.maximum_damage_resources_per_client = 1;
  limits.maximum_pictures_per_client = 1;
  return limits;
}

void exercise_repeated_shm(const SysvSegment& segment) {
  ResourceTable resources(kScreenModel, stress_limits(segment.size()));
  constexpr std::uint32_t xid = kResourceBase | 1U;
  const auto peer_uid = static_cast<std::uint32_t>(::getuid());
  const auto baseline = segment.attachment_count();

  for (std::size_t iteration = 0; iteration < kIterations; ++iteration) {
    require(resources.attach_shm_segment(
                kOwner, kResourceBase, kResourceMask, xid,
                static_cast<std::uint32_t>(segment.id()), true, peer_uid) ==
                AttachShmStatus::Success,
            "repeated SHM attach succeeds");
    require(segment.attachment_count() == baseline + 1,
            "repeated SHM attach creates exactly one mapping");
    require(resources.detach_shm_segment(xid) == DetachShmStatus::Success,
            "repeated SHM detach succeeds");
    require(segment.attachment_count() == baseline,
            "repeated SHM detach releases exactly one mapping");
  }
  require(resources.invariants_hold(),
          "repeated SHM operations preserve resource invariants");
}

void exercise_eventfd_synchronization() {
  const auto descriptor_baseline = open_descriptor_count();
  auto pixels = PixelStorage::create(8, 8);
  require(pixels.has_value(), "create eventfd synchronization pixels");

  for (std::size_t iteration = 0; iteration < kIterations; ++iteration) {
    auto published = PublishedWindowBuffer::create(
        static_cast<std::uint64_t>(iteration + 1), 0x200001U, *pixels,
        GWIPC_SYNCHRONIZATION_EVENTFD);
    require(published != nullptr && published->synchronization_fd() >= 0,
            "create eventfd-synchronized published buffer");
    const int descriptor = published->synchronization_fd();
    require((::fcntl(descriptor, F_GETFD) & FD_CLOEXEC) != 0 &&
                (::fcntl(descriptor, F_GETFL) & O_NONBLOCK) != 0,
            "published eventfd is nonblocking and close-on-exec");
    require(published->signal_ready() && published->retract_ready(),
            "eventfd token completes one producer-consumer handoff");
    require(!published->retract_ready(),
            "eventfd handoff consumes exactly one readiness token");
  }

  {
    PublishedBufferStore store(512);
    auto published = PublishedWindowBuffer::create(
        1000, 0x200002U, *pixels, GWIPC_SYNCHRONIZATION_EVENTFD);
    require(published != nullptr && published->signal_ready() &&
                store.install(0x200002U, std::move(published)) &&
                store.retire(0x200002U,
                             PublishedBufferRetirement::ConsumerDone),
            "install and retire synchronized buffer before disconnect");
    store.peer_disconnected();
    require(store.accounted_bytes() == 0,
            "peer disconnect releases retired synchronized buffers");
  }

  require(open_descriptor_count() == descriptor_baseline,
          "eventfd and memfd descriptors return to baseline");
}

void exercise_limits_cleanup_and_reuse(const SysvSegment& segment) {
  ResourceTable resources(kScreenModel, stress_limits(segment.size()));
  constexpr std::uint32_t pixmap = kResourceBase | 10U;
  constexpr std::uint32_t shm = kResourceBase | 11U;
  constexpr std::uint32_t rejected_shm = kResourceBase | 12U;
  constexpr std::uint32_t region = kResourceBase | 13U;
  constexpr std::uint32_t rejected_region = kResourceBase | 14U;
  constexpr std::uint32_t damage = kResourceBase | 15U;
  constexpr std::uint32_t rejected_damage = kResourceBase | 16U;
  constexpr std::uint32_t picture = kResourceBase | 17U;
  constexpr std::uint32_t rejected_picture = kResourceBase | 18U;
  constexpr std::uint32_t reusable = kResourceBase | 19U;
  const auto peer_uid = static_cast<std::uint32_t>(::getuid());
  const auto shm_baseline = segment.attachment_count();
  const std::array one_rectangle{Rectangle{0, 0, 1, 1}};
  const std::array complex_damage{Rectangle{0, 0, 1, 1},
                                  Rectangle{6, 6, 1, 1}};

  require(resources.create_pixmap(kOwner, kResourceBase, kResourceMask,
                                  pixmap, resources.screen().root_window, 24,
                                  8, 8) == CreatePixmapStatus::Success,
          "create stress drawable");
  require(resources.attach_shm_segment(
              kOwner, kResourceBase, kResourceMask, shm,
              static_cast<std::uint32_t>(segment.id()), true, peer_uid) ==
              AttachShmStatus::Success,
          "extension SHM limit accepts first mapping");
  require(resources.attach_shm_segment(
              kOwner, kResourceBase, kResourceMask, rejected_shm,
              static_cast<std::uint32_t>(segment.id()), true, peer_uid) ==
                  AttachShmStatus::BadAlloc &&
              !resources.find(rejected_shm) &&
              segment.attachment_count() == shm_baseline + 1,
          "extension SHM limit rejects without mapping or XID leak");

  require(resources.create_xfixes_region(kOwner, kResourceBase, kResourceMask,
                                         rejected_region, complex_damage) ==
                  RegionStatus::BadAlloc &&
              !resources.find(rejected_region),
          "region rectangle limit rejects atomically");
  require(resources.create_xfixes_region(kOwner, kResourceBase, kResourceMask,
                                         region, one_rectangle) ==
                  RegionStatus::Success &&
              resources.create_xfixes_region(
                  kOwner, kResourceBase, kResourceMask, rejected_region,
                  one_rectangle) == RegionStatus::BadAlloc &&
              !resources.find(rejected_region),
          "region count limit rejects without XID leak");

  require(resources.create_damage(kOwner, kResourceBase, kResourceMask,
                                  damage, pixmap,
                                  DamageReportLevel::NonEmpty) ==
                  DamageStatus::Success &&
              resources.create_damage(kOwner, kResourceBase, kResourceMask,
                                      rejected_damage, pixmap,
                                      DamageReportLevel::NonEmpty) ==
                  DamageStatus::BadAlloc &&
              !resources.find(rejected_damage),
          "damage count limit rejects without XID leak");
  const auto mutation = resources.add_damage(pixmap, complex_damage);
  require(mutation.status == DamageStatus::Success &&
              mutation.notifications.size() == 1 &&
              resources.find_damage(damage)->accumulated ==
                  std::vector<Rectangle>{{0, 0, 8, 8}},
          "complex damage falls back to full drawable bounds");

  auto first_picture = Picture::create_drawable(
      pixmap, PictureFormatId::Xrgb32, 24, 32);
  auto second_picture = Picture::create_drawable(
      pixmap, PictureFormatId::Xrgb32, 24, 32);
  require(first_picture.has_value() && second_picture.has_value() &&
              resources.create_picture(kOwner, kResourceBase, kResourceMask,
                                       picture, std::move(*first_picture)) ==
                  PictureResourceStatus::Success &&
              resources.create_picture(kOwner, kResourceBase, kResourceMask,
                                       rejected_picture,
                                       std::move(*second_picture)) ==
                  PictureResourceStatus::BadAlloc &&
              !resources.find(rejected_picture),
          "picture count limit rejects without XID leak");

  const auto cleanup = resources.cleanup_client(kOwner);
  require(cleanup.resources_destroyed >= 5 &&
              resources.resource_count_by_owner(kOwner) == 0 &&
              segment.attachment_count() == shm_baseline &&
              resources.invariants_hold(),
          "client disconnect releases all extension resources and SHM maps");

  require(resources.create_pixmap(
              kReuseOwner, kResourceBase, kResourceMask, pixmap,
              resources.screen().root_window, 24, 8, 8) ==
              CreatePixmapStatus::Success,
          "reuse client creates drawable with released XID");
  for (std::size_t iteration = 0; iteration < kIterations; ++iteration) {
    require(resources.create_xfixes_region(
                kReuseOwner, kResourceBase, kResourceMask, reusable,
                one_rectangle) == RegionStatus::Success &&
                resources.destroy_xfixes_region(reusable) ==
                    RegionStatus::Success &&
                resources.create_damage(kReuseOwner, kResourceBase,
                                        kResourceMask, reusable, pixmap,
                                        DamageReportLevel::BoundingBox) ==
                    DamageStatus::Success &&
                resources.destroy_damage(reusable) == DamageStatus::Success,
            "extension resource XID is reusable across resource types");
    auto reused_picture = Picture::create_drawable(
        pixmap, PictureFormatId::Xrgb32, 24, 32);
    require(reused_picture.has_value() &&
                resources.create_picture(
                    kReuseOwner, kResourceBase, kResourceMask, reusable,
                    std::move(*reused_picture)) ==
                    PictureResourceStatus::Success &&
                resources.free_picture(reusable) ==
                    PictureResourceStatus::Success,
            "extension resource XID is reusable for RENDER pictures");
  }
  (void)resources.cleanup_client(kReuseOwner);
  require(resources.resource_count_by_owner(kReuseOwner) == 0 &&
              resources.invariants_hold(),
          "reuse-client disconnect leaves no extension resources");
}

void write_result(std::ostream& output) {
  output << "{\n"
            "  \"schema\": 1,\n"
            "  \"probe\": \"m12_extension_stress_probe\",\n"
            "  \"iterations\": {\"shm\": 64, \"eventfd\": 64, \"xid\": 64},\n"
            "  \"checks\": {\n"
            "    \"extension_limits\": true,\n"
            "    \"shm_attach_detach\": true,\n"
            "    \"eventfd_synchronization\": true,\n"
            "    \"damage_complexity_fallback\": true,\n"
            "    \"disconnect_cleanup\": true,\n"
            "    \"extension_xid_reuse\": true,\n"
            "    \"descriptor_leak_free\": true,\n"
            "    \"shm_mapping_leak_free\": true\n"
            "  },\n"
            "  \"deltas\": {\"descriptors\": 0, \"shm_mappings\": 0},\n"
            "  \"passed\": true\n"
            "}\n";
}

std::optional<std::filesystem::path> output_path(const int argc, char** argv) {
  if (argc == 1) return std::nullopt;
  if (argc == 2 && std::string_view{argv[1]} == "--help") {
    std::printf("usage: m12_extension_stress_probe [--output PATH]\n");
    std::exit(0);
  }
  if (argc == 3 && std::string_view{argv[1]} == "--output")
    return std::filesystem::path{argv[2]};
  fail("usage: m12_extension_stress_probe [--output PATH]");
}

}  // namespace

int main(const int argc, char** argv) {
  const auto destination = output_path(argc, argv);
  const auto descriptor_baseline = open_descriptor_count();
  SysvSegment segment(4096);
  const auto shm_baseline = segment.attachment_count();

  exercise_repeated_shm(segment);
  exercise_eventfd_synchronization();
  exercise_limits_cleanup_and_reuse(segment);

  require(segment.attachment_count() == shm_baseline,
          "SysV SHM mapping count returns to baseline");
  require(open_descriptor_count() == descriptor_baseline,
          "descriptor count returns to process baseline");

  if (destination) {
    std::ofstream output(*destination, std::ios::binary | std::ios::trunc);
    require(output.good(), "open result output");
    write_result(output);
    require(output.good(), "write result output");
  } else {
    write_result(std::cout);
  }
  return 0;
}
