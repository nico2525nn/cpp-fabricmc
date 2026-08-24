#include "TestClient.hpp"
using namespace cpptest;
int main(){
    TestClient c;
    if(!c.connect("127.0.0.1", 25999)){ printf("connect fail\n"); return 1;}
    auto js = c.queryStatusJson();
    printf("status: %s\n", js.c_str());
    return js.empty()?1:0;
}
