#include "ipc/wire/lifecycle_contract.hpp"
#include "tests/helpers/test_support.hpp"

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace {
using namespace gw::ipc::wire;
using gw::test::require;

std::vector<std::uint8_t> hex(std::string_view text) {
  auto digit=[](char c){return static_cast<std::uint8_t>(c<='9'?c-'0':c-'a'+10);};
  std::vector<std::uint8_t> out;
  for(std::size_t i=0;i<text.size();i+=2) out.push_back((digit(text[i])<<4U)|digit(text[i+1]));
  return out;
}

PolicyWindowUpsert base() {
  return {10,1,0,2,-3,4,100,80,1,PolicyWindowType::Dialog,
          PolicyMapIntent::WantsMap,false,2,true,false,false,true,11,12,13,0};
}

template<class T> void malformed(const T& value) {
  const auto bytes=encode(value); T decoded;
  for(std::size_t n=0;n<bytes.size();++n)
    require(decode(std::span(bytes).first(n),decoded)==CodecStatus::Truncated,
            "lifecycle truncated prefix rejected");
  auto trailing=bytes; trailing.push_back(0);
  require(decode(trailing,decoded)==CodecStatus::TrailingData,
          "lifecycle trailing byte rejected");
}
}

int main() {
  const auto window=base();
  PolicyLifecycleWindowUpsert lifecycle{window,14,15,16,PolicyStackMode::Above,0};
  const auto lifecycle_golden=hex("0a000000010000000000000002000000fdffffff040000006400000050000000010000000200010000020100000100000b000000000000000c000000000000000d0000000000000000000000000000000e000000000000000f0000000000000010000000010000000000000000000000");
  require(encode(lifecycle)==lifecycle_golden,"lifecycle exact golden");
  const auto base_bytes=encode(window);
  require(std::equal(base_bytes.begin(),base_bytes.end(),lifecycle_golden.begin()),
          "lifecycle prefix preserves exact M5 window bytes");
  PolicyLifecycleWindowUpsert decoded_lifecycle;
  require(decode(lifecycle_golden,decoded_lifecycle)==CodecStatus::Ok &&
              decoded_lifecycle.geometry_serial==14 &&
              decoded_lifecycle.stack_sibling==16,
          "lifecycle golden round trips");
  malformed(lifecycle);
  lifecycle.stack_serial=0;
  require(decode(encode(lifecycle),decoded_lifecycle)==CodecStatus::InvalidValue,
          "zero stack serial requires empty stack intent");
  lifecycle={window,0,1,10,PolicyStackMode::Above,0};
  require(decode(encode(lifecycle),decoded_lifecycle)==CodecStatus::InvalidValue,
          "self stack sibling rejected");

  SurfacePolicyUpsert surface{17,10,2,PolicyWindowType::Dialog,
      PolicyAppliedState::Normal,true,true,true,false,true,2,0,0};
  const auto surface_golden=hex("11000000000000000a000000020000000200010001010100010200000000000000000000");
  require(encode(surface)==surface_golden,"surface policy exact golden");
  SurfacePolicyUpsert decoded_surface;
  require(decode(surface_golden,decoded_surface)==CodecStatus::Ok &&
              decoded_surface.surface_id==17 && decoded_surface.focused,
          "surface policy golden round trips");
  malformed(surface);
  auto bad=surface_golden; bad[20]=2;
  require(decode(bad,decoded_surface)==CodecStatus::InvalidValue,
          "surface policy noncanonical boolean rejected");
  bad=surface_golden; bad[16]=0xff;
  require(decode(bad,decoded_surface)==CodecStatus::InvalidValue,
          "surface policy enum rejected");
  return 0;
}
