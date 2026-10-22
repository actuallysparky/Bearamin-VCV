// SPDX-License-Identifier: GPL-3.0-or-later
#include "plugin.hpp"
Plugin* pluginInstance;
void init(Plugin* p) {
    pluginInstance = p;
    p->addModel(modelSlowLFO);
    p->addModel(modelBearaminVirtual);
}
