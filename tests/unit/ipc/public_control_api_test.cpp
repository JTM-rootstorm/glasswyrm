#include <glasswyrm/ipc.h>
#include <array>
#include <cstdint>
#include <cstdio>
int main(){
  gwipc_snapshot_begin begin{sizeof(begin),0x0102030405060708ULL,GWIPC_SNAPSHOT_WINDOW_POLICY,0,9,2,{}};
  gwipc_control_payload*p=nullptr;
  if(gwipc_control_encode_snapshot_begin(&begin,&p)!=GWIPC_STATUS_OK)return 1;
  size_t n=0;const auto*d=gwipc_control_payload_data(p,&n);
  constexpr std::array<std::uint8_t,28> golden{8,7,6,5,4,3,2,1,3,0,0,0,9,0,0,0,0,0,0,0,2,0,0,0,0,0,0,0};
  bool ok=n==golden.size();for(size_t i=0;ok&&i<n;++i)ok=d[i]==golden[i];gwipc_control_payload_destroy(p);
  begin.reserved[0]=1;p=reinterpret_cast<gwipc_control_payload*>(1);
  ok=ok&&gwipc_control_encode_snapshot_begin(&begin,&p)==GWIPC_STATUS_INVALID_ARGUMENT;
  gwipc_snapshot_abort abort{sizeof(abort),1,1,nullptr,1,{}};
  ok=ok&&gwipc_control_encode_snapshot_abort(&abort,&p)==GWIPC_STATUS_INVALID_ARGUMENT;
  const auto version=gwipc_get_api_version();ok=ok&&version.major==0&&version.minor==3&&version.patch==0;
  if(!ok)std::fputs("public control API test failed\n",stderr);
  return ok?0:1;
}
