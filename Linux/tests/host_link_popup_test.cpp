#include "window/HostLink.h"
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

void Check(bool popup,bool cached=false) {
  const auto path=sukut::window::HostLink::SocketPath();std::filesystem::create_directories(path.parent_path());
  const int server=socket(AF_UNIX,SOCK_SEQPACKET|SOCK_CLOEXEC,0);assert(server>=0);
  sockaddr_un address{};address.sun_family=AF_UNIX;std::strcpy(address.sun_path,path.c_str());
  assert(bind(server,reinterpret_cast<sockaddr*>(&address),sizeof(address))==0);assert(listen(server,1)==0);
  std::string first;
  std::thread peer([&]{const int client=accept(server,nullptr,nullptr);assert(client>=0);char b[128];const auto n=recv(client,b,sizeof(b),0);if(n<=0){close(client);return;}first.assign(b,n);const std::string answer="1{\"mode\":0}";assert(send(client,answer.data(),answer.size(),0)==static_cast<ssize_t>(answer.size()));while(recv(client,b,sizeof(b),0)>0){}close(client);});
  {sukut::window::HostLink link;std::string error;const auto started=std::chrono::steady_clock::now();assert(link.Connect("",error,popup,cached?"{\"mode\":2}":""));
   if(cached){assert(link.InitialState()=="{\"mode\":2}");assert(std::chrono::steady_clock::now()-started<std::chrono::milliseconds(200));}
   else assert(link.InitialState()=="{\"mode\":0}");}
  peer.join();close(server);unlink(path.c_str());assert(first==(cached?"":popup?"read":"hello"));
}
int main(){char folder[]="/tmp/sukut-link-XXXXXX";assert(mkdtemp(folder));setenv("XDG_RUNTIME_DIR",folder,1);Check(false);Check(true);Check(true,true);std::filesystem::remove_all(folder);std::cout<<"PASS: popup reads state without registering a main window; normal window keeps hello handshake\n";}
