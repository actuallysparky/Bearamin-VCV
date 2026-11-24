#include "../src/lite/output.hpp"
#include "../src/lite/field_preview.hpp"
#include <cassert>
#include <cmath>

int main() {
    using bearamin_virtual_preview::strength;
    assert(strength(0,100,300)<0 && strength(301,100,300)<0);
    assert(strength(50,100,300)==1 && strength(100,100,300)==1);
    assert(strength(200,100,300)==0.5f && strength(300,100,300)==0);
    assert(strength(300,300,300)<0);
    assert(strength(500,100,300)<0 && strength(500,100,900)==0.5f);
    bearamin_virtual_output::OutputState out;
    bearfield::ProcessedFieldState field{};
    field.present=true; field.spankHeld=true;
    field.xNorm12=4095; field.yNorm12=0; field.zNorm12=2048;
    out.step(field,false,1.f,0.f,0.01f);
    assert(out.cv[0]==5.f && out.cv[1]==0.f && out.cv[3]==5.f && out.cv[4]==-5.f);
    assert(out.presence==5.f && out.spank==5.f);
    out.step(field,false,0.f,0.f,0.01f);
    for (float v:out.cv) assert(v==0.f);
    out.step(field,false,1.f,0.f,0.01f);
    field.present=false; field.spankHeld=false;
    out.step(field,false,1.f,1.f,0.1f);
    assert(out.cv[0]>4.f && out.cv[0]<5.f && out.presence==0.f && out.spank==0.f);
    out.step(field,true,1.f,1.f,0.1f);
    for (float v:out.cv) assert(v==0.f);
}
