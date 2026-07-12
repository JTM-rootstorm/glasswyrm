#include "glasswyrmd/lifecycle_coordinator.hpp"
#include <cstdio>
#include <cstdlib>
#include <vector>
using namespace glasswyrm::server;
namespace {
void require(bool v,const char*m){if(!v){std::fprintf(stderr,"lifecycle coordinator: %s\n",m);std::exit(1);}}
LifecycleSnapshot snapshot(std::uint32_t focus){LifecycleSnapshot s;s.focused_window=focus;return s;}
LifecycleOperation operation(std::uint64_t token,std::uint64_t client,std::uint32_t focus){LifecycleOperation o;o.token=token;o.client_id=client;o.request_sequence=token;o.window=focus;o.proposed=snapshot(focus);return o;}
}
int main(){
 std::vector<std::uint32_t> policy,compositor,commits;std::vector<std::pair<std::uint64_t,bool>> completed;bool fatal=false;
 LifecycleCallbacks callbacks{
  [&](const LifecycleSnapshot&s){policy.push_back(s.focused_window);return true;},
  [&](const LifecycleSnapshot&s){compositor.push_back(s.focused_window);return true;},
  [&](const LifecycleSnapshot&s){commits.push_back(s.focused_window);return true;},
  [&](std::uint64_t token,bool ok){completed.emplace_back(token,ok);},
  [&]{fatal=true;}};
 LifecycleCoordinator coordinator(snapshot(1),2,callbacks);
 require(coordinator.enqueue(operation(10,1,10))==EnqueueStatus::Queued&&coordinator.phase()==CoordinatorPhase::AwaitingPolicy&&policy==std::vector<std::uint32_t>{10},"first operation starts policy");
 require(coordinator.enqueue(operation(20,2,20))==EnqueueStatus::Queued&&coordinator.enqueue(operation(30,3,30))==EnqueueStatus::Full,"bounded FIFO counts active");
 require(coordinator.policy_accepted(snapshot(10))&&coordinator.phase()==CoordinatorPhase::AwaitingCompositor&&compositor.back()==10,"policy advances compositor");
 require(coordinator.compositor_accepted()&&completed.front()==std::pair<std::uint64_t,bool>{10,true}&&coordinator.active()->token==20&&policy.back()==20&&coordinator.committed().focused_window==10,"commit promotes and starts FIFO next");
 coordinator.peer_disconnected();require(coordinator.phase()==CoordinatorPhase::WaitingForPeer&&coordinator.active()->token==20,"disconnect retains active operation");
 require(coordinator.peer_synchronized()&&coordinator.phase()==CoordinatorPhase::AwaitingPolicy&&policy.back()==20,"reconnect resends same pending snapshot");
 coordinator.cancel_client(2);require(coordinator.policy_accepted(snapshot(20))&&coordinator.phase()==CoordinatorPhase::RollingBackPolicy&&policy.back()==10,"canceled accepted policy begins rollback");
 require(coordinator.policy_accepted(snapshot(10))&&coordinator.phase()==CoordinatorPhase::RollingBackCompositor&&compositor.back()==10,"rollback policy advances committed compositor replay");
 require(coordinator.compositor_accepted()&&completed.back()==std::pair<std::uint64_t,bool>{20,false}&&coordinator.phase()==CoordinatorPhase::Idle&&!fatal,"rollback completes without promotion");

 LifecycleCoordinator rejected(snapshot(1),1,callbacks);
 require(rejected.enqueue(operation(40,4,40))==EnqueueStatus::Queued&&rejected.policy_rejected()&&completed.back()==std::pair<std::uint64_t,bool>{40,false},"policy rejection preserves committed state");
 LifecycleCallbacks broken=callbacks;broken.send_policy=[](const LifecycleSnapshot&){return false;};
 LifecycleCoordinator failed(snapshot(1),1,broken);
 require(failed.enqueue(operation(50,5,50))==EnqueueStatus::Fatal&&failed.phase()==CoordinatorPhase::Fatal&&fatal,"transport callback failure is fatal");
}
