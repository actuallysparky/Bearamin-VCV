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
struct DeviceGesture {
    bool available = false, present = false, presence = false, spank = false;
    uint16_t x = 0, y = 0, z = 0, centroidX = 0, centroidY = 0;
    unsigned rotation = 0;
    bool mirror = false;
};
inline bool parseGesture(json_t* object, DeviceGesture& out) {
    if (!object) return true; // Legacy v1 firmware has raw frames only.
    uint64_t version = 0, x = 0, y = 0, z = 0, rotation = 0, cx = 0, cy = 0;
    if (!json_is_object(object) || !uintField(object,"v",1,version) || version != 1 ||
        !uintField(object,"x",4095,x) || !uintField(object,"y",4095,y) ||
        !uintField(object,"z",4095,z) || !uintField(object,"rotation",3,rotation) ||
        !uintField(object,"cx",1000,cx) || !uintField(object,"cy",1000,cy)) return false;
    for (const char* key : {"present", "presence", "spank", "mirror"})
        if (!json_is_boolean(json_object_get(object,key))) return false;
    out.present = json_is_true(json_object_get(object,"present"));
    out.presence = json_is_true(json_object_get(object,"presence"));
    out.spank = json_is_true(json_object_get(object,"spank"));
    if ((out.presence || out.spank) && !out.present) return false;
    out.x = x; out.y = y; out.z = z; out.rotation = rotation;
    out.centroidX = cx; out.centroidY = cy;
    out.mirror = json_is_true(json_object_get(object,"mirror"));
    out.available = true;
    return true;
}
struct Message {
    bool frame = false;
    State state = SENSOR_INIT;
    uint32_t boot = 0, seq = 0;
    uint64_t capture = 0;
    bearfield::DepthFrame8x8 depth{};
    DeviceGesture gesture{};
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
            valid = json_is_array(cells) && json_array_size(cells) == 64 &&
                parseGesture(json_object_get(root,"gesture"),out.gesture);
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
