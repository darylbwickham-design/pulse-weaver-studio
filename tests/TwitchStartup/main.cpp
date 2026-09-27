#include "../../engine/obs-studio/shared/qt/PulseTwitchStart.hpp"
#include <cstdio>
#include <cstdlib>
using namespace PulseTwitch;
void check(bool ok,const char *message){if(!ok){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}}
int main(){
 check(normalizeMode("vertical")=="dual","Legacy portrait-only choice migrates to supported Dual");
 check(normalizeMode("off")=="off" && normalizeMode("horizontal")=="horizontal","Saved selections retained");
 auto plan=route("dual",false,"portrait-a");
 check(plan.enhanced && plan.extraCanvas=="portrait-a","Dual enables enhanced and portrait");
 plan=route("horizontal",plan.enhanced,"portrait-a");
 check(plan.enhanced && plan.extraCanvas.empty(),"Dual to landscape keeps quality levels but clears portrait");
 plan=route("dual",plan.enhanced,"portrait-b");
 check(plan.extraCanvas=="portrait-b","Next Dual start uses current canvas not cached one");
 check(!route("horizontal",false,"stale").enhanced,"Standard landscape stays standard");
 check(matches("horizontal",true,false) && !matches("horizontal",true,true),"Landscape rejects accidental dual output");
 check(matches("dual",true,true) && !matches("dual",true,false),"Dual refuses landscape-only fallback");
 check(!matches("dual",false,true) && !matches("horizontal",false,false),"Reject missing landscape encoders");
 StartState state;
 auto attempt=state.begin();
 check(state.pending(),"Preparation is pending");
 check(state.prepared(attempt,true),"Preparation accepted");
 // Model the reported gap: obs_output_start returns true but obs_output_active is false.
 bool outputActive=false;
 check(state.pending() || outputActive,"Accepted but not active must not report failure");
 check(state.connecting(),"Remain connecting through arbitrarily delayed success");
 state.started(); outputActive=true;
 check(!state.pending() && outputActive,"Live event ends pending state");
 state.stop();
 attempt=state.begin(); state.prepared(attempt,true); state.failed();
 check(!state.pending(),"Connection failure ends pending state");
 attempt=state.begin(); check(!state.prepared(attempt,false) && !state.pending(),"Preparation rejection is failure");
 attempt=state.begin(); state.stop(); const auto newer=state.begin();
 check(!state.prepared(attempt,true) && state.pending(),"Cancelled old callback cannot overwrite next attempt");
 check(state.prepared(newer,true),"New attempt can proceed");
 state.stop(); check(!state.pending() && state.attempt()!=newer,"Stop cancels connecting and deferred destination starts");
 std::puts("PASS: format changes, stale portrait clearing, negotiated format checks, delayed startup, failure and cancellation generations");
}