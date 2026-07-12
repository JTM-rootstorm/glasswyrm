#include <glasswyrm/ipc.h>
#include "ipc/wire/input_contract.hpp"
#include <cassert>
#include <cstdint>
#include <vector>
using namespace gw::ipc::wire;
int main() {
  const SyntheticMotion motion{7,11,-2,300,0};
  const std::vector<std::uint8_t> golden{7,0,0,0,0,0,0,0,11,0,0,0,254,255,255,255,44,1,0,0,0,0,0,0};
  assert(encode(motion)==golden);
  SyntheticMotion decoded_motion;
  assert(decode(golden,decoded_motion)==CodecStatus::Ok && decoded_motion.root_x==-2);
  auto malformed=golden; malformed.push_back(0);
  assert(decode(malformed,decoded_motion)==CodecStatus::TrailingData);
  malformed=golden; malformed[0]=0;
  assert(decode(malformed,decoded_motion)==CodecStatus::InvalidValue);

  SyntheticButton button{9,12,5,1,0,0}; SyntheticButton decoded_button;
  assert(decode(encode(button),decoded_button)==CodecStatus::Ok);
  button.button=6; assert(decode(encode(button),decoded_button)==CodecStatus::InvalidValue);
  button.button=1; button.pressed=2; assert(decode(encode(button),decoded_button)==CodecStatus::InvalidValue);
  SyntheticKey key{10,13,255,0,0,0}; SyntheticKey decoded_key;
  assert(decode(encode(key),decoded_key)==CodecStatus::Ok);
  key.keycode=7; assert(decode(encode(key),decoded_key)==CodecStatus::InvalidValue);
  SyntheticBarrier barrier{11,0}; SyntheticBarrier decoded_barrier;
  assert(decode(encode(barrier),decoded_barrier)==CodecStatus::Ok);
  SyntheticInputAcknowledged ack{11,13,SyntheticInputResult::Clamped,-1,20,4,5,0x101,0,3,0};
  SyntheticInputAcknowledged decoded_ack;
  assert(decode(encode(ack),decoded_ack)==CodecStatus::Ok);
  ack.result=static_cast<SyntheticInputResult>(7);
  assert(decode(encode(ack),decoded_ack)==CodecStatus::InvalidValue);

  gwipc_synthetic_motion public_motion{sizeof(public_motion),1,1,2,3,0,{}};
  gwipc_contract_payload* payload=nullptr;
  assert(gwipc_contract_encode_synthetic_motion(&public_motion,&payload)==GWIPC_STATUS_OK);
  size_t size=0; assert(gwipc_contract_payload_data(payload,&size)!=nullptr && size==24);
  gwipc_contract_payload_destroy(payload);
  public_motion.flags=1;
  assert(gwipc_contract_encode_synthetic_motion(&public_motion,&payload)==GWIPC_STATUS_INVALID_ARGUMENT);
  static_assert(GWIPC_CAP_SYNTHETIC_INPUT==(UINT64_C(1)<<12));
  static_assert(GWIPC_MESSAGE_SYNTHETIC_INPUT_ACKNOWLEDGED==0x0310);
}
