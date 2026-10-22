// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Bearamin contributors

#pragma once
#include <bearfield/bearfield.h>
#include <jansson.h>
#include <cstdint>
#include <cstring>
#include <string>
namespace bearamin_virtual_protocol {
enum State { DISCONNECTED, CONNECTING, STREAMING, STALE, SENSOR_INIT, SENSOR_FAULT, UNSUPPORTED, BUSY };
inline bool uintField(json_t* root, const char* key, uint64_t max, uint64_t& value) {
    json_t* j = json_object_get(root, key);
    if (!json_is_integer(j) || json_integer_value(j) < 0 || static_cast<uint64_t>(json_integer_value(j)) > max) return false;
    value = static_cast<uint64_t>(json_integer_value(j)); return true;
}
struct Message {
    bool frame = false;
    State state = SENSOR_INIT;
    uint32_t boot = 0, seq = 0;
    uint64_t capture = 0;
    bearfield::DepthFrame8x8 depth{};
};

inline bool parseLine(const std::string& line, Message& out, bool& unsupported) {
    if (line.size() > 1023) return false;
    json_error_t error{};
    json_t* root = json_loadb(line.data(), line.size(), JSON_REJECT_DUPLICATES, &error);
    if (!root) return false;
    uint64_t v = 0, boot = 0, seq = 0, capture = 0;
    bool valid = json_is_object(root) && uintField(root,"v",UINT32_MAX,v);
    if (valid && v != 1) { unsupported = true; valid = false; }
    valid = valid && uintField(root,"boot",UINT32_MAX,boot) &&
        uintField(root,"seq",UINT32_MAX,seq) && uintField(root,"capture_us",INT64_MAX,capture);
    json_t* type = json_object_get(root,"type");
    valid = valid && json_is_string(type);
    if (valid) {
        out.boot = static_cast<uint32_t>(boot); out.seq = static_cast<uint32_t>(seq); out.capture = capture;
        const char* kind = json_string_value(type);
        if (std::strcmp(kind,"frame") == 0) {
            json_t* cells = json_object_get(root,"mm");
            valid = json_is_array(cells) && json_array_size(cells) == 64;
            if (valid) {
                out.frame = true; out.depth.sequence = out.seq; out.depth.capturedUs = capture;
                for (size_t i = 0; i < 64; ++i) {
                    json_t* cell = json_array_get(cells,i);
                    if (json_is_null(cell)) continue;
                    if (!json_is_integer(cell) || json_integer_value(cell) < 1 || json_integer_value(cell) > 2000) { valid = false; break; }
                    out.depth.distanceMm[i] = static_cast<uint16_t>(json_integer_value(cell));
                    out.depth.validMask |= UINT64_C(1) << i;
                }
            }
        } else if (std::strcmp(kind,"status") == 0) {
            json_t* state = json_object_get(root,"state");
            const char* s = json_is_string(state) ? json_string_value(state) : "";
            valid = !json_object_get(root,"mm");
            if (std::strcmp(s,"sensor_init") == 0) out.state = SENSOR_INIT;
            else if (std::strcmp(s,"sensor_fault") == 0) out.state = SENSOR_FAULT;
            else valid = false;
        } else valid = false;
    }
    json_decref(root);
    return valid;
}

}
