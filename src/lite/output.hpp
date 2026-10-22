// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Bearamin contributors

#pragma once
#include <bearfield/bearfield.h>
#include <algorithm>
#include <cmath>

namespace bearamin_virtual_output {
struct OutputState {
    float cv[6]{};
    float presence = 0.f;
    float spank = 0.f;
    void step(const bearfield::ProcessedFieldState& field, bool fault,
              float range, float slewSeconds, float sampleSeconds) {
        if (fault) {
            for (float& value : cv) value = 0.f;
            presence = spank = 0.f;
            return;
        }
        range=std::max(0.f,std::min(1.f,range));
        float norm[3]={field.xNorm12/4095.f,field.yNorm12/4095.f,field.zNorm12/4095.f};
        float target[6]{};
        if (field.present) for (int i=0;i<3;++i) {
            target[i]=5.f*norm[i]*range;
            target[i+3]=5.f*(2.f*norm[i]-1.f)*range;
        }
        float alpha=slewSeconds<=0.f ? 1.f : 1.f-std::exp(-sampleSeconds/slewSeconds);
        for (int i=0;i<6;++i) cv[i] += alpha*(target[i]-cv[i]);
        presence=field.present ? 5.f : 0.f;
        spank=field.spankHeld ? 5.f : 0.f;
    }
};
}
