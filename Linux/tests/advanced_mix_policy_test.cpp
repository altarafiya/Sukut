#include "live/OptimizedMix.h"
#include "window/AdvancedMixAccess.h"
#include <cassert>
#include <iostream>
#include <cstdlib>
int main(){using namespace cleanwave;using Gate=sukut::window::AdvancedMixAccess;
 // The real code is not in the source; the policy is tested with a stand-in.
 auto matches=[](std::string_view c){return c=="test-code";};
 assert(!Gate::IsAccessCode("wrong"));if(const char*real=std::getenv("SUKUT_ADVANCED_CODE"))assert(Gate::IsAccessCode(real));
 assert(sukut::window::Sha256Hex("abc")=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
 for(int mode=0;mode<=3;++mode)for(int bg=-30;bg<=20;++bg){auto a=ModeGains(mode,bg),b=ModeGains(mode,bg,{false,10,10,10});assert(a.voice_db==b.voice_db&&a.music_db==b.music_db&&a.background_db==b.background_db);if(mode!=2){auto c=ModeGains(mode,bg,{true,10,10,10});assert(a.voice_db==c.voice_db&&a.music_db==c.music_db&&a.background_db==c.background_db);}}
 auto mix=ModeGains(2,0,{true,-200,0,200});assert(mix.voice_db==-20&&mix.music_db==0&&mix.background_db==10);
 Gate gate;assert(gate.Attempt("wrong",1000)==Gate::Result::Incorrect);assert(gate.Attempt("wrong",1000)==Gate::Result::Incorrect);assert(gate.Attempt("wrong",1000)==Gate::Result::Locked);assert(gate.locked_until==601000);assert(gate.Attempt("test-code",600999,matches)==Gate::Result::Locked);assert(gate.Attempt("test-code",601000,matches)==Gate::Result::Enabled);
 std::cout<<"PASS: default parity, other-mode isolation, gain bounds, and lockout expiry\n";}
