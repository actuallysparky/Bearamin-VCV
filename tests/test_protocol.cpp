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
    // The optional processed gesture extension is strict while legacy v1 stays valid.
    std::string extended=prefix.substr(0,prefix.size()-1)+R"(,"gesture":{"v":1,"x":1024,"y":2048,"z":4095,"cx":250,"cy":500,"present":true,"presence":false,"spank":true,"rotation":1,"mirror":true}})";
    Message processed{};assert(parseLine(extended,processed,unsupported));
    assert(processed.gesture.available && processed.gesture.x==1024 && !processed.gesture.presence);
    for(const auto& change: {std::pair<std::string,std::string>{"1024","4096"},{"\"rotation\":1","\"rotation\":4"},{"\"present\":true","\"present\":false"},{"\"mirror\":true","\"mirror\":1"}}) {
        std::string bad=extended;auto at=bad.find(change.first);assert(at!=std::string::npos);
        bad.replace(at,change.first.size(),change.second);checkBad(bad);
    }

}
