// SPDX-License-Identifier: GPL-3.0-or-later
// Test-only Rack host support, compiled separately from the plugin API client.
// Rack's JSON parameter loader needs a host engine. The pinned SDK's internal
// constructor is intentionally unavailable to plugins, so keep it in this host
// translation unit. This file is never included in the distributable plugin.
#include <engine/Engine.hpp>

rack::engine::Engine* createTestEngine() {
    return new rack::engine::Engine;
}
