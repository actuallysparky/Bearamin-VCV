// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cstdint>
namespace bearamin_virtual_preview {
// Near is full intensity; far is the dim edge. Outside far has no active cell.
inline float strength(uint16_t mm, uint16_t nearMm, uint16_t farMm) {
    if (!mm || nearMm >= farMm || mm > farMm) return -1.f;
    return std::max(0.f, std::min(1.f, (float(farMm) - mm) / (farMm - nearMm)));
}
}
