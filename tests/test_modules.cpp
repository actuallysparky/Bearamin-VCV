// SPDX-License-Identifier: GPL-3.0-or-later
// Exercise the actual Rack modules against the official SDK, without opening ports.
#include "../src/plugin.cpp"
#include "../src/SlowLFO.cpp"
#include "../src/lite/BearaminVirtual.cpp"
#include <cassert>
#include <iostream>
#include <fstream>

rack::engine::Engine* createTestEngine();

static void closeTo(double actual, double expected, double tolerance = 1e-5) {
    assert(std::abs(actual - expected) < tolerance);
}

int main(int argc, char** argv) {
    Context context;
    context.engine = createTestEngine();
    contextSet(&context);
    Plugin collection;
    json_error_t error{};
    json_t* manifest = json_load_file("plugin.json", 0, &error);
    assert(manifest);
    collection.fromJson(manifest);
    init(&collection);
    collection.modulesFromJson(json_object_get(manifest, "modules"));
    assert(collection.slug == "Bearamin" && collection.models.size() == 3);
    assert(collection.getModel("SlowLFO") == modelSlowLFO);
    assert(collection.getModel("BearaminVirtual") == modelBearaminVirtual);
    assert(modelSlowLFO->name == "SloFO");
    assert(collection.getModel("Virtual") == modelVirtual);
    assert(modelVirtual->name == "Lite" && !modelVirtual->hidden);
    assert(modelBearaminVirtual->hidden);
    assert(modelSlowLFO->modularGridUrl == "https://modulargrid.net/e/hive-mind-synthesis-slofo");
    assert(modelVirtual->modularGridUrl == "https://modulargrid.net/e/hive-mind-synthesis-bearamin-lite");

    plugin::plugins.push_back(&collection);

    // Resolve both collection identifiers through Rack's actual model registry.
    // Round-tripping uses the normal JSON loader and keeps the original slug.
    for (const char* slug : {"BearaminVirtual", "Virtual"}) {
        assert(plugin::getModel("Bearamin", slug) == collection.getModel(slug));
        auto* model = plugin::getModel("Bearamin", slug);
        auto* restored = dynamic_cast<BearaminVirtual*>(model->createModule());
        assert(restored);
        restored->stopping.store(true);
        restored->worker.join();
        json_t* old = json_pack("{s:s,s:s,s:I,s:[{s:i,s:f}],s:{s:s,s:i,s:b,s:b}}",
            "plugin", "Bearamin", "model", slug, "id", json_int_t(9),
            "params", "id", 0, "value", 0.7,
            "data", "port", "", "rotation", 2, "mirrorX", 1, "mirrorY", 0);
        restored->fromJson(old);
        json_t* savedModule = restored->toJson();
        assert(std::string(json_string_value(json_object_get(savedModule, "model"))) == slug);
        assert(json_integer_value(json_object_get(savedModule, "id")) == 9);
        assert(!restored->followDeviceSettings.load());
        json_t* savedData=json_object_get(savedModule,"data");
        assert(json_is_false(json_object_get(savedData,"followDeviceSettings")));
        json_object_del(savedData,"followDeviceSettings");
        assert(json_equal(json_object_get(old,"data"),savedData));
        closeTo(restored->params[0].getValue(), 0.7);
        json_decref(savedModule); json_decref(old);
        delete restored;
    }

    Module::ProcessArgs args{};
    args.sampleRate = 48000.f;
    args.sampleTime = 1.f / args.sampleRate;
    SlowLFO lfo;
    lfo.process(args);
    assert(lfo.phase > 0.0);  // A newly created module runs on its first sample.
    closeTo(lfo.phase, args.sampleTime / 60.0, 1e-12);
    closeTo(lfo.outputs[SlowLFO::SINE_OUTPUT].getVoltage(), 2.5);
    closeTo(lfo.outputs[SlowLFO::TRIANGLE_OUTPUT].getVoltage(), 2.5);
    closeTo(lfo.outputs[SlowLFO::SQUARE_OUTPUT].getVoltage(), 5.0);
    lfo.pendingDetents.store(1);
    lfo.pendingModeToggles.store(1);
    lfo.process(args);
    assert(lfo.periodSeconds.load() == 120 && lfo.bipolar.load());

    // Power-off remains a useful Rack preview: zero outputs and frozen phase.
    const double beforeOff = lfo.phase;
    lfo.pendingPowerToggles.store(1);
    lfo.process(args);
    for (auto& output : lfo.outputs) closeTo(output.getVoltage(), 0.0);
    closeTo(lfo.phase, beforeOff, 1e-12);
    lfo.pendingPowerToggles.store(1);
    lfo.pendingDetents.store(1);
    lfo.process(args);
    assert(lfo.phase > beforeOff && lfo.periodSeconds.load() == 180);
    assert(lfo.outputs[SlowLFO::SQUARE_OUTPUT].getVoltage() == 5.f);

    // Older saved data loads; obsolete charging settings have no effect.
    json_t* saved = json_pack("{s:I,s:f,s:i,s:b,s:i,s:i}",
        "periodSeconds", static_cast<json_int_t>(64 * MONTH), "phase", 0.25,
        "selectedUnit", 3, "bipolar", 1, "chargeDurationSeconds", 25, "timeScale", 86400);
    lfo.dataFromJson(saved);
    lfo.process(args);
    closeTo(lfo.phase, 0.25 + args.sampleTime * 86400.0 / (64 * MONTH), 1e-12);
    closeTo(lfo.outputs[SlowLFO::SINE_OUTPUT].getVoltage(), 5.0);
    closeTo(lfo.outputs[SlowLFO::TRIANGLE_OUTPUT].getVoltage(), 5.0);
    json_t* roundtrip = lfo.dataToJson();
    assert(!json_object_get(roundtrip, "chargeDurationSeconds"));
    assert(json_integer_value(json_object_get(roundtrip, "periodSeconds")) == 64 * MONTH);
    assert(json_integer_value(json_object_get(roundtrip, "selectedUnit")) == 3);
    assert(json_is_true(json_object_get(roundtrip, "bipolar")));
    lfo.displayView.store(0);
    MatrixWidget matrix;
    matrix.module = &lfo;
    auto pixels = matrix.frame();
    int lit = 0;
    for (const auto& pixel : pixels) if (pixel.r || pixel.g || pixel.b) ++lit;
    assert(lit > 0 && lit < 64);

    BearaminVirtual lite;  // No port selected: the worker does no device I/O.
    lite.stopping.store(true);
    lite.worker.join();  // Deterministic synthetic injection, without a live worker.
    lite.process(args);
    for (auto& output : lite.outputs) closeTo(output.getVoltage(), 0.0);
    Event frame;
    frame.state = STREAMING;
    frame.field.present = true;
    frame.field.spankHeld = true;
    frame.field.xNorm12 = 4095;
    frame.field.yNorm12 = 0;
    frame.field.zNorm12 = 2048;
    lite.params[BearaminVirtual::SLEW_PARAM].setValue(0.f);
    lite.push(frame);
    lite.state.store(STREAMING);
    lite.process(args);
    closeTo(lite.outputs[BearaminVirtual::X_OUTPUT].getVoltage(), 5.0);
    closeTo(lite.outputs[BearaminVirtual::Y_BI_OUTPUT].getVoltage(), -5.0);
    closeTo(lite.outputs[BearaminVirtual::PRES_OUTPUT].getVoltage(), 5.0);
    closeTo(lite.outputs[BearaminVirtual::SPANK_OUTPUT].getVoltage(), 5.0);
    lite.age = 0.251f;
    lite.process(args);
    for (auto& output : lite.outputs) closeTo(output.getVoltage(), 0.0);
    lite.push(frame); lite.state.store(STREAMING); lite.process(args);
    lite.state.store(SENSOR_FAULT); lite.process(args);
    for (auto& output : lite.outputs) closeTo(output.getVoltage(), 0.0);
    // Preserve orientation keys without triggering a connection.
    json_t* orientation = json_pack("{s:s,s:i,s:b,s:b}", "port", "", "rotation", 3,
                                   "mirrorX", 1, "mirrorY", 0);
    lite.dataFromJson(orientation);
    json_t* orientationCopy = lite.dataToJson();
    assert(!lite.followDeviceSettings.load());
    assert(json_is_false(json_object_get(orientationCopy,"followDeviceSettings")));
    json_object_del(orientationCopy,"followDeviceSettings");
    assert(json_equal(orientation, orientationCopy));
    json_object_set_new(orientation,"followDeviceSettings",json_true());
    lite.dataFromJson(orientation);
    assert(lite.followDeviceSettings.load());
    json_decref(orientationCopy); json_decref(orientation);
    // Real firmware fixtures traverse the actual parser, serial-to-event path,
    // audio-thread CV conversion and patch preference restoration.
    {
        std::ifstream fixtures(argc == 2 ? argv[1] : "tests/fixtures/field-guide-gestures.jsonl");assert(fixtures.good());
        std::string line;unsigned count=0;
        while(std::getline(fixtures,line)) {
            Message message{};bool unsupported=false;
            assert(parseLine(line,message,unsupported) && message.gesture.available);
            bearfield::Processor processor(bearfield::BearCvV1());
            auto event=frameEvent(message,true,3,true,true,processor);
            assert(event.deviceSettings); // patch transforms must not run twice
            lite.params[BearaminVirtual::RANGE_PARAM].setValue(1);
            lite.params[BearaminVirtual::SLEW_PARAM].setValue(0);
            lite.push(event);lite.state.store(STREAMING);lite.process(args);
            const auto& g=message.gesture;
            unsigned norm[]={g.x,g.y,g.z};
            for(int axis=0;axis<3;++axis) {
                closeTo(lite.outputs[axis].getVoltage(),g.present?5.f*norm[axis]/4095.f:0.f);
                closeTo(lite.outputs[axis+3].getVoltage(),g.present?5.f*(2.f*norm[axis]/4095.f-1.f):0.f);
            }
            closeTo(lite.outputs[BearaminVirtual::PRES_OUTPUT].getVoltage(),g.presence?5:0);
            closeTo(lite.outputs[BearaminVirtual::SPANK_OUTPUT].getVoltage(),g.spank?5:0);
            lite.params[BearaminVirtual::RANGE_PARAM].setValue(.5f);
            lite.process(args);closeTo(lite.outputs[0].getVoltage(),g.present?2.5f*g.x/4095.f:0.f);
            lite.state.store(SENSOR_FAULT);lite.process(args);
            for(auto& output:lite.outputs)closeTo(output.getVoltage(),0);
            auto legacy=frameEvent(message,false,0,false,false,processor);
            assert(!legacy.deviceSettings);
            ++count;
        }
        assert(count==384);
        std::cout << "384 firmware-to-Rack configurations, gates, Range and fault cases passed\n";
    }
    plugin::plugins.clear();
    json_decref(roundtrip); json_decref(saved); json_decref(manifest);
    std::cout << "Actual Rack modules: registration, immediate start/resume, saved state, CV and fault/stale checks passed\n";
}
