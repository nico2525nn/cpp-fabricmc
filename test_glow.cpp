#include "tests/TestClient.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
using namespace cpptest;
int main(int argc, char** argv){
    const char* bin = argc>1?argv[1]:"build/cppfm";
    // start server
    pid_t pid = fork();
    if(pid==0){
        execl(bin, bin, "--port=25599", "--view-distance=6", "--world-dir=/tmp/glowtest", "--online-mode=false", (char*)nullptr);
        _exit(1);
    }
    std::this_thread::sleep_for(std::chrono::seconds(2));
    TestClient c;
    if(!c.connect("127.0.0.1",25599)) { std::cerr<<"connect fail\n"; return 1; }
    if(!c.join("GlowTester")) { std::cerr<<"join fail "<<c.lastError<<"\n"; return 1; }
    std::this_thread::sleep_for(std::chrono::milliseconds(800));
    std::cerr<<"joined, sending setblock glowstone\n";
    c.sendChatCommand("setblock 2 -60 0 minecraft:glowstone");
    auto dl = std::chrono::steady_clock::now() + std::chrono::milliseconds(3000);
    while(std::chrono::steady_clock::now()<dl){
        c.pump(40);
        std::cerr<<" counts: BlockUpdate="<<c.blockUpdates.size()<<" UpdateLight="<<c.count(cppfm::proto::pl::sc::UpdateLight)<<" Inits="<<c.count(cppfm::proto::pl::sc::InitializeWorldBorder)<<std::endl;
        if(c.count(cppfm::proto::pl::sc::UpdateLight)>0) break;
    }
    bool got = c.count(cppfm::proto::pl::sc::UpdateLight)>0;
    std::cerr<<"RESULT UpdateLight got="<<got<<"\n";
    for(auto &u: c.blockUpdates) std::cerr<<" blockUpdate "<<u.x<<" "<<u.y<<" "<<u.z<<" state "<<u.state<<"\n";
    c.close();
    kill(pid, SIGTERM);
    int st; waitpid(pid,&st,0);
    return got?0:1;
}
