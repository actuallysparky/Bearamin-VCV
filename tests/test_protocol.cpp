#include "../src/lite/protocol.hpp"
#include <cassert>
#include <string>

int main() {
    using namespace bearamin_virtual_protocol;
    std::string prefix="{\"v\":1,\"type\":\"frame\",\"boot\":7,\"seq\":4294967295,\"capture_us\":900,\"mm\":[";
    for (int i=0;i<64;++i) { if (i) prefix+=","; prefix += i==1 ? "null" : "400"; }
    prefix+="]}";
    Message m{}; bool unsupported=false;
    assert(parseLine(prefix,m,unsupported));
    assert(m.frame && m.boot==7 && m.seq==UINT32_MAX && m.depth.validMask==(UINT64_MAX ^ 2u));
    assert(m.depth.distanceMm[0]==400 && m.depth.distanceMm[1]==0);
    auto checkBad=[&](std::string text){Message x{};bool u=false;assert(!parseLine(text,x,u));};
    checkBad(prefix.substr(0,prefix.size()-1));
    checkBad(prefix+"garbage");
    checkBad("{\"v\":1,\"v\":1,\"type\":\"status\",\"boot\":1,\"seq\":1,\"capture_us\":1,\"state\":\"sensor_fault\"}");
    checkBad("{\"v\":1,\"type\":\"status\",\"boot\":1,\"seq\":1,\"capture_us\":1,\"state\":\"sensor_fault\",\"mm\":[]}");
    checkBad(std::string(1024,'x'));
    std::string bad=prefix; auto pos=bad.find("400"); bad.replace(pos,3,"2001"); checkBad(bad);
    Message status{}; assert(parseLine("{\"v\":1,\"type\":\"status\",\"boot\":1,\"seq\":2,\"capture_us\":3,\"state\":\"sensor_fault\"}",status,unsupported));
    assert(!status.frame && status.state==SENSOR_FAULT);
    Message v{}; assert(!parseLine("{\"v\":2,\"type\":\"status\"}",v,unsupported) && unsupported);
}
