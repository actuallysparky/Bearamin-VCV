// SPDX-License-Identifier: GPL-3.0-or-later
#include "plugin.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <string>

namespace {

constexpr double PI = 3.14159265358979323846;
constexpr int MATRIX_SIDE = 8;
constexpr int MATRIX_CELLS = MATRIX_SIDE * MATRIX_SIDE;
constexpr int64_t DAY = 86400;
constexpr int64_t MONTH = 30 * DAY;
constexpr int64_t MAX_PERIOD = 64 * MONTH;
constexpr int64_t MINIMUM[4] = {60, 3600, DAY, MONTH};
constexpr int64_t MAXIMUM[4] = {3600, DAY, 64 * DAY, MAX_PERIOD};
constexpr int64_t STEP[4] = {60, 3600, DAY / 2, MONTH};

struct Color {
    float r, g, b;
    constexpr Color(float red = 0.f, float green = 0.f, float blue = 0.f)
        : r(red), g(green), b(blue) {}
};

template <typename T>
T clampValue(T value, T lo, T hi) {
    return std::max(lo, std::min(value, hi));
}

constexpr Color UNIT_COLOR[4] = {
    {0.15f, 0.76f, 1.f},  // minutes: cyan
    {0.24f, 1.f, 0.45f},  // hours: green
    {1.f, 0.64f, 0.13f},  // days: amber
    {0.91f, 0.26f, 1.f},  // months: violet
};
constexpr int TRIANGLE_PATH_BI[] = {
    32, 24, 17, 9, 2, 11, 19, 28, 36, 45, 53, 62, 55, 47, 39
};
constexpr int TRIANGLE_PATH_UNI[] = {
    16, 8, 9, 1, 2, 3, 11, 12, 20, 21, 29, 30, 31, 23
};

std::array<int, MATRIX_CELLS> pieOrder() {
    std::array<int, MATRIX_CELLS> order{};
    for (int i = 0; i < MATRIX_CELLS; ++i) order[i] = i;
    auto turn = [](int index) {
        const double x = index % MATRIX_SIDE - 3.5;
        const double y = 3.5 - index / MATRIX_SIDE;
        double angle = std::atan2(x, y);
        if (angle < 0) angle += 2.0 * PI;
        return angle;
    };
    std::sort(order.begin(), order.end(), [&](int a, int b) {
        const double aa = turn(a), bb = turn(b);
        if (aa != bb) return aa < bb;
        const double ra = std::hypot(a % MATRIX_SIDE - 3.5, a / MATRIX_SIDE - 3.5);
        const double rb = std::hypot(b % MATRIX_SIDE - 3.5, b / MATRIX_SIDE - 3.5);
        return ra < rb;
    });
    return order;
}

float clamp01(double x) {
    return static_cast<float>(clampValue(x, 0.0, 1.0));
}

void addColor(Color& dst, Color src, float brightness) {
    dst.r = std::min(1.f, dst.r + src.r * brightness);
    dst.g = std::min(1.f, dst.g + src.g * brightness);
    dst.b = std::min(1.f, dst.b + src.b * brightness);
}

double wave(int which, double phase) {
    phase -= std::floor(phase);
    if (which == 0)
        return 0.5 + 0.5 * std::sin(2.0 * PI * phase);
    if (which == 1) {
        if (phase < 0.25) return 0.5 + 2.0 * phase;
        if (phase < 0.75) return 1.5 - 2.0 * phase;
        return 2.0 * phase - 1.5;
    }
    return phase < 0.5 ? 1.0 : 0.0;
}

std::string periodLabel(int64_t seconds, int selectedUnit) {
    if (selectedUnit == 2 && seconds >= DAY && seconds <= 64 * DAY && seconds % (DAY / 2) == 0) {
        const std::string wholeDays = std::to_string(seconds / DAY);
        return wholeDays + (seconds % DAY ? ".5 d" : " d");
    }
    if (seconds >= MONTH && seconds % MONTH == 0)
        return std::to_string(seconds / MONTH) + " mo (30 d each)";
    if (seconds % DAY == 0) return std::to_string(seconds / DAY) + " d";
    if (seconds >= DAY) return std::to_string(seconds / DAY) + ".5 d";
    if (seconds % 3600 == 0) return std::to_string(seconds / 3600) + " h";
    if (seconds % 60 == 0) return std::to_string(seconds / 60) + " min";
    return std::to_string(seconds) + " s";
}

}  // namespace

struct SlowLFO : Module {
    enum OutputId { SINE_OUTPUT, TRIANGLE_OUTPUT, SQUARE_OUTPUT, OUTPUTS_LEN };

    std::atomic<int64_t> periodSeconds{60};
    std::atomic<double> phaseSnapshot{0.0};
    std::atomic<int> selectedUnit{0};
    std::atomic<int> displayView{0};  // 0 waveform, 1 selector, 2 time
    std::atomic<bool> bipolar{false};
    std::atomic<bool> modeHoldPreview{false};
    std::atomic<bool> simulatedOff{false};
    std::atomic<int> timeScale{1};

    std::atomic<int> pendingDetents{0};
    std::atomic<int> pendingShortPresses{0};
    std::atomic<int> pendingModeToggles{0};
    std::atomic<int> pendingPowerToggles{0};
    std::atomic<int> pendingPhaseQuarter{-1};
    std::atomic<bool> pendingRestore{false};
    std::atomic<double> restorePhase{0.0};

    double phase = 0.0;
    double uiClock = 0.0;
    double viewDeadline = 0.0;
    unsigned snapshotCounter = 0;

    SlowLFO() {
        config(0, 0, OUTPUTS_LEN);
        configOutput(SINE_OUTPUT, "Sine CV");
        configOutput(TRIANGLE_OUTPUT, "Triangle CV");
        configOutput(SQUARE_OUTPUT, "Square CV");
    }

    void process(const ProcessArgs& args) override {
        uiClock += args.sampleTime;
        if (pendingRestore.exchange(false)) {
            phase = clampValue(restorePhase.load(), 0.0, 0.999999999999);
            phaseSnapshot.store(phase);
        }

        if (pendingPowerToggles.exchange(0) & 1) {
            simulatedOff.store(!simulatedOff.load());
            displayView.store(0);
        }
        const int forcedQuarter = pendingPhaseQuarter.exchange(-1);
        if (forcedQuarter >= 0) {
            phase = 0.25 * (forcedQuarter % 4);
            phaseSnapshot.store(phase);
        }

        const int presses = pendingShortPresses.exchange(0);
        if (presses > 0 && !simulatedOff.load()) {
            selectedUnit.store((selectedUnit.load() + presses) % 4);
            displayView.store(1);
            viewDeadline = uiClock + 2.0;
        }
        const int modeToggles = pendingModeToggles.exchange(0);
        if ((modeToggles & 1) && !simulatedOff.load()) {
            bipolar.store(!bipolar.load());
        }

        const int detents = clampValue(pendingDetents.exchange(0), -100, 100);
        if (detents != 0 && !simulatedOff.load()) {
            const int unit = selectedUnit.load();
            int64_t period = periodSeconds.load();
            const int dir = detents < 0 ? -1 : 1;
            for (int i = 0; i < std::abs(detents); ++i) {
                if (period < MINIMUM[unit]) period = MINIMUM[unit];
                else if (period > MAXIMUM[unit]) period = MAXIMUM[unit];
                else period = clampValue(period + dir * STEP[unit], MINIMUM[unit], MAXIMUM[unit]);
            }
            periodSeconds.store(period);
            displayView.store(2);
            viewDeadline = uiClock + 2.0;
        }
        if (displayView.load() != 0 && uiClock >= viewDeadline)
            displayView.store(0);

        if (simulatedOff.load()) {
            for (int i = 0; i < OUTPUTS_LEN; ++i) outputs[i].setVoltage(0.f);
        }
        else {
            const double period = static_cast<double>(periodSeconds.load());
            phase += static_cast<double>(args.sampleTime) * timeScale.load() / period;
            phase -= std::floor(phase);
            const bool bi = bipolar.load();
            for (int i = 0; i < OUTPUTS_LEN; ++i) {
                const double normalized = wave(i, phase);
                outputs[i].setVoltage(static_cast<float>(normalized * (bi ? 10.0 : 5.0) - (bi ? 5.0 : 0.0)));
            }
        }
        if (++snapshotCounter >= 256) {
            snapshotCounter = 0;
            phaseSnapshot.store(phase);
        }
    }

    json_t* dataToJson() override {
        json_t* root = json_object();
        json_object_set_new(root, "periodSeconds", json_integer(periodSeconds.load()));
        json_object_set_new(root, "phase", json_real(phaseSnapshot.load()));
        json_object_set_new(root, "selectedUnit", json_integer(selectedUnit.load()));
        json_object_set_new(root, "bipolar", json_boolean(bipolar.load()));
        json_object_set_new(root, "timeScale", json_integer(timeScale.load()));
        return root;
    }

    void dataFromJson(json_t* root) override {
        if (json_t* j = json_object_get(root, "periodSeconds"))
            periodSeconds.store(clampValue<int64_t>(json_integer_value(j), 60, MAX_PERIOD));
        if (json_t* j = json_object_get(root, "selectedUnit"))
            selectedUnit.store(clampValue<int>(json_integer_value(j), 0, 3));
        if (json_t* j = json_object_get(root, "bipolar"))
            bipolar.store(json_boolean_value(j));
        if (json_t* j = json_object_get(root, "timeScale")) {
            const int value = json_integer_value(j);
            timeScale.store(value == 60 || value == 3600 || value == 86400 ? value : 1);
        }
        if (json_t* j = json_object_get(root, "phase")) {
            const double value = json_number_value(j);
            if (std::isfinite(value)) {
                restorePhase.store(value - std::floor(value));
                pendingRestore.store(true);
            }
        }
    }
};

struct MatrixWidget : widget::Widget {
    SlowLFO* module = nullptr;

    std::array<Color, MATRIX_CELLS> frame() const {
        std::array<Color, MATRIX_CELLS> cells{};
        if (!module || module->simulatedOff.load()) return cells;
        const int view = module->displayView.load();
        const int unit = module->selectedUnit.load();
        const double phase = module->phaseSnapshot.load();
        const int64_t period = module->periodSeconds.load();

        if (module->modeHoldPreview.load()) {
            const bool bi = module->bipolar.load();
            for (int y = 0; y < MATRIX_SIDE; ++y)
                for (int x = 0; x < MATRIX_SIDE; ++x) {
                    if (y < 4)
                        cells[y * MATRIX_SIDE + x] = Color(0.06f, 0.32f, 1.f);
                    else if (bi)
                        cells[y * MATRIX_SIDE + x] = Color(1.f, 0.07f, 0.08f);
                }
        }
        else if (view == 1) {
            const int bases[4][2] = {{0, 0}, {4, 0}, {0, 4}, {4, 4}};
            for (int u = 0; u < 4; ++u)
                for (int dy = 0; dy < 4; ++dy)
                    for (int dx = 0; dx < 4; ++dx)
                        addColor(cells[(bases[u][1] + dy) * MATRIX_SIDE + bases[u][0] + dx],
                                 UNIT_COLOR[u], u == unit ? 1.f : 0.10f);
        }
        else if (view == 2 && unit >= 2) {
            // Days: one cell per day, with a half-lit next cell at half days.
            // Months: one cell per fixed 30-day month.
            const double filled = period / static_cast<double>(unit == 2 ? DAY : MONTH);
            for (int i = 0; i < MATRIX_CELLS; ++i)
                addColor(cells[i], UNIT_COLOR[unit], clamp01(filled - i));
        }
        else if (view == 2) {
            static const auto order = pieOrder();
            const double fraction = clampValue(period / static_cast<double>(MAXIMUM[unit]), 0.0, 1.0);
            const double filled = fraction * MATRIX_CELLS;
            for (int i = 0; i < MATRIX_CELLS; ++i)
                addColor(cells[order[i]], UNIT_COLOR[unit], clamp01(filled - i));
        }
        else {
            const bool bi = module->bipolar.load();
            const int* path = bi ? TRIANGLE_PATH_BI : TRIANGLE_PATH_UNI;
            const int count = bi ? sizeof(TRIANGLE_PATH_BI) / sizeof(TRIANGLE_PATH_BI[0])
                                 : sizeof(TRIANGLE_PATH_UNI) / sizeof(TRIANGLE_PATH_UNI[0]);
            for (int i = 0; i < count; ++i)
                addColor(cells[path[i]], UNIT_COLOR[unit], 0.48f);
            const int marker = std::min(count - 1, static_cast<int>(phase * count));
            cells[path[marker]] = Color(1.f, 1.f, 1.f);
        }
        return cells;
    }

    void draw(const DrawArgs& args) override {
        const auto cells = frame();
        const float pitch = box.size.x / MATRIX_SIDE;
        for (int y = 0; y < MATRIX_SIDE; ++y) for (int x = 0; x < MATRIX_SIDE; ++x) {
            const Color c = cells[y * MATRIX_SIDE + x];
            const float cx = (x + 0.5f) * pitch;
            const float cy = (y + 0.5f) * pitch;
            nvgBeginPath(args.vg);
            nvgCircle(args.vg, cx, cy, pitch * 0.38f);
            nvgFillColor(args.vg, nvgRGB(10, 20, 24));
            nvgFill(args.vg);
            if (c.r > 0.99f && c.g > 0.99f && c.b > 0.99f) {
                nvgBeginPath(args.vg);
                nvgCircle(args.vg, cx, cy, pitch * 0.46f);
                nvgFillColor(args.vg, nvgRGBA(255, 255, 255, 85));
                nvgFill(args.vg);
            }
            nvgBeginPath(args.vg);
            nvgCircle(args.vg, cx, cy, pitch * 0.32f);
            nvgFillColor(args.vg, nvgRGBf(c.r, c.g, c.b));
            nvgFill(args.vg);
        }
        Widget::draw(args);
    }
};

struct EncoderWidget : widget::Widget {
    SlowLFO* module = nullptr;
    bool pressed = false;
    bool pressEligible = false;
    bool turnedDuringPress = false;
    bool modeSwitched = false;
    double pressStart = 0.0;
    float dragRemainder = 0.f;
    float wheelRemainder = 0.f;
    float knobAngle = 0.f;

    void finishPress() {
        if (!pressed) return;
        pressed = false;
        if (!module) return;
        module->modeHoldPreview.store(false);
        if (!pressEligible || turnedDuringPress) return;
        const double held = system::getTime() - pressStart;
        if (held >= 2.0 && !modeSwitched)
            module->pendingModeToggles.fetch_add(1);
        else if (held < 0.5)
            module->pendingShortPresses.fetch_add(1);
    }

    void step() override {
        if (pressed && module) {
            const bool ready = pressEligible && !turnedDuringPress &&
                               !module->simulatedOff.load();
            const double held = system::getTime() - pressStart;
            module->modeHoldPreview.store(ready && held >= 0.5);
            if (ready && held >= 2.0 && !modeSwitched) {
                modeSwitched = true;
                module->pendingModeToggles.fetch_add(1);
            }
        }
        Widget::step();
    }

    void onButton(const ButtonEvent& e) override {
        if (e.button != GLFW_MOUSE_BUTTON_LEFT) return;
        if (e.action == GLFW_PRESS) {
            pressed = true;
            pressEligible = module && !module->simulatedOff.load();
            turnedDuringPress = false;
            modeSwitched = false;
            pressStart = system::getTime();
            dragRemainder = 0.f;
            if (module) module->modeHoldPreview.store(false);
            e.consume(this);
        }
        else if (e.action == GLFW_RELEASE) {
            finishPress();
            e.consume(this);
        }
    }

    void onDragEnd(const DragEndEvent& e) override { finishPress(); }

    void onDragMove(const DragMoveEvent& e) override {
        if (!module) return;
        dragRemainder -= e.mouseDelta.y;
        const int detents = static_cast<int>(dragRemainder / 8.f);
        if (detents != 0) {
            dragRemainder -= detents * 8.f;
            knobAngle += detents * 0.28f;
            turnedDuringPress = true;
            module->modeHoldPreview.store(false);
            module->pendingDetents.fetch_add(detents);
        }
    }

    void onHoverScroll(const HoverScrollEvent& e) override {
        if (!module) return;
        wheelRemainder += e.scrollDelta.y;
        const int detents = static_cast<int>(wheelRemainder);
        if (detents != 0) {
            wheelRemainder -= detents;
            knobAngle += detents * 0.28f;
            if (pressed) {
                turnedDuringPress = true;
                module->modeHoldPreview.store(false);
            }
            module->pendingDetents.fetch_add(detents);
        }
        e.consume(this);
    }

    void draw(const DrawArgs& args) override {
        const float cx = box.size.x / 2.f;
        const float cy = box.size.y / 2.f;
        const float radius = std::min(cx, cy) - 2.f;
        nvgBeginPath(args.vg);
        nvgCircle(args.vg, cx, cy, radius);
        nvgFillColor(args.vg, nvgRGB(12, 17, 21));
        nvgFill(args.vg);
        nvgStrokeWidth(args.vg, 2.f);
        nvgStrokeColor(args.vg, nvgRGB(174, 194, 197));
        nvgStroke(args.vg);
        nvgBeginPath(args.vg);
        nvgMoveTo(args.vg, cx + std::sin(knobAngle) * radius * 0.35f,
                  cy - std::cos(knobAngle) * radius * 0.35f);
        nvgLineTo(args.vg, cx + std::sin(knobAngle) * radius * 0.8f,
                  cy - std::cos(knobAngle) * radius * 0.8f);
        nvgStrokeWidth(args.vg, 3.f);
        nvgStrokeColor(args.vg, nvgRGB(230, 239, 224));
        nvgStroke(args.vg);
        Widget::draw(args);
    }
};

struct SlowLFOWidget : ModuleWidget {
    SlowLFOWidget(SlowLFO* module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/SlowLFO.svg")));

        auto* matrix = new MatrixWidget;
        matrix->module = module;
        matrix->box.pos = Vec(30, 37);
        matrix->box.size = Vec(60, 60);
        addChild(matrix);

        auto* encoder = new EncoderWidget;
        encoder->module = module;
        encoder->box.pos = Vec(38, 122);
        encoder->box.size = Vec(44, 44);
        addChild(encoder);

        addOutput(createOutputCentered<PJ301MPort>(Vec(60, 227), module, SlowLFO::SINE_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(Vec(60, 275), module, SlowLFO::TRIANGLE_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(Vec(60, 325), module, SlowLFO::SQUARE_OUTPUT));
    }

    void appendContextMenu(ui::Menu* menu) override {
        SlowLFO* m = dynamic_cast<SlowLFO*>(module);
        menu->addChild(new ui::MenuSeparator);
        if (!m) return;
        menu->addChild(createMenuLabel("Period: " + periodLabel(m->periodSeconds.load(), m->selectedUnit.load())));
        menu->addChild(createMenuLabel("Prototype time acceleration"));
        for (int speed : {1, 60, 3600, 86400}) {
            menu->addChild(createCheckMenuItem(std::to_string(speed) + "x", "",
                [m, speed] { return m->timeScale.load() == speed; },
                [m, speed] { m->timeScale.store(speed); }));
        }
        menu->addChild(new ui::MenuSeparator);
        menu->addChild(createMenuLabel("Phase and power-loss preview"));
        for (int quarter = 0; quarter < 4; ++quarter) {
            menu->addChild(createMenuItem("Set phase " + std::to_string(quarter * 25) + "%", "",
                [m, quarter] { m->pendingPhaseQuarter.store(quarter); }));
        }
        menu->addChild(createCheckMenuItem("Simulated power off", "",
            [m] { return m->simulatedOff.load(); },
            [m] { m->pendingPowerToggles.fetch_add(1); }));
    }
};

Model* modelSlowLFO = createModel<SlowLFO, SlowLFOWidget>("SlowLFO");
