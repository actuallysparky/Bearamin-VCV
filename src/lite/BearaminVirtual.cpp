// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Bearamin contributors

#include "../plugin.hpp"
#include "protocol.hpp"
#include "output.hpp"
#include "panel_layout.hpp"
#include <bearfield/bearfield.h>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>
#include <algorithm>
#ifndef _WIN32
#include <fcntl.h>
#include <glob.h>
#include <sys/select.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#else
#define NOMINMAX
#include <windows.h>
#endif

namespace {
using Clock = std::chrono::steady_clock;

std::mutex claimsMutex;
std::set<std::string> claimedPorts;

std::vector<std::string> ports() {
    std::vector<std::string> found;
#ifndef _WIN32
    for (const char* pattern : {"/dev/cu.usbmodem*", "/dev/ttyACM*", "/dev/serial/by-id/*"}) {
        glob_t g{};
        if (glob(pattern, 0, nullptr, &g) == 0)
            for (size_t i = 0; i < g.gl_pathc; ++i) found.emplace_back(g.gl_pathv[i]);
        globfree(&g);
    }
#else
    for (int i = 1; i <= 32; ++i) found.push_back("COM" + std::to_string(i));
#endif
    return found;
}

struct SerialPort {
#ifndef _WIN32
    int fd = -1;
#else
    HANDLE handle = INVALID_HANDLE_VALUE;
#endif
    bool openPort(const std::string& name) {
#ifndef _WIN32
        fd = open(name.c_str(), O_RDONLY | O_NOCTTY | O_NONBLOCK);
        if (fd < 0) return false;
        termios t{};
        if (tcgetattr(fd, &t) != 0) { closePort(); return false; }
        cfmakeraw(&t); cfsetispeed(&t, B115200); cfsetospeed(&t, B115200);
        t.c_cflag |= CLOCAL | CREAD;
        if (tcsetattr(fd, TCSANOW, &t) != 0) { closePort(); return false; }
        int dtr = TIOCM_DTR;
        if (ioctl(fd, TIOCMBIS, &dtr) != 0) { closePort(); return false; }
#else
        std::string path = "\\\\.\\" + name;
        handle = CreateFileA(path.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (handle == INVALID_HANDLE_VALUE) return false;
        DCB dcb{}; dcb.DCBlength = sizeof(dcb);
        if (!GetCommState(handle, &dcb)) { closePort(); return false; }
        dcb.BaudRate = CBR_115200; dcb.ByteSize = 8; dcb.Parity = NOPARITY; dcb.StopBits = ONESTOPBIT;
        dcb.fDtrControl = DTR_CONTROL_ENABLE;
        if (!SetCommState(handle, &dcb)) { closePort(); return false; }
        COMMTIMEOUTS timeout{}; timeout.ReadIntervalTimeout = MAXDWORD;
        timeout.ReadTotalTimeoutConstant = 40;
        if (!SetCommTimeouts(handle, &timeout)) { closePort(); return false; }
#endif
        return true;
    }
    int readBytes(char* dst, int size) {
#ifndef _WIN32
        fd_set fds; FD_ZERO(&fds); FD_SET(fd, &fds);
        timeval tv{0, 40000};
        int ready = select(fd + 1, &fds, nullptr, nullptr, &tv);
        if (ready < 0) return -1;
        if (ready == 0) return 0;
        int n = static_cast<int>(read(fd, dst, size));
        return n < 0 && (errno == EAGAIN || errno == EINTR) ? 0 : n;
#else
        DWORD n = 0;
        return ReadFile(handle, dst, static_cast<DWORD>(size), &n, nullptr) ? static_cast<int>(n) : -1;
#endif
    }
    void closePort() {
#ifndef _WIN32
        if (fd >= 0) close(fd); fd = -1;
#else
        if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle); handle = INVALID_HANDLE_VALUE;
#endif
    }
    ~SerialPort() { closePort(); }
};

using namespace bearamin_virtual_protocol;

struct Event {
    State state = DISCONNECTED;
    bearfield::ProcessedFieldState field{};
    std::array<uint16_t,64> mm{};
    int point = -1;
    Event() = default;
    explicit Event(State s) : state(s) {}
};
}

struct BearaminVirtual : Module {
    enum Params { RANGE_PARAM, SLEW_PARAM, NUM_PARAMS };
    enum Outputs { X_OUTPUT, Y_OUTPUT, Z_OUTPUT, X_BI_OUTPUT, Y_BI_OUTPUT, Z_BI_OUTPUT, PRES_OUTPUT, SPANK_OUTPUT, NUM_OUTPUTS };
    std::atomic<State> state{DISCONNECTED};
    std::atomic<int> rotation{0};
    std::atomic<bool> mirrorX{false}, mirrorY{false};
    std::array<std::atomic<uint16_t>,64> display{};
    std::atomic<int> point{-1};
    std::mutex selectionMutex;
    std::string selectedPort;
    std::thread worker;
    std::atomic<bool> stopping{false};
    std::array<Event,8> queue{};
    std::atomic<unsigned> head{0}, tail{0};
    Event latest{};
    float age = 1000.f;
    bearamin_virtual_output::OutputState voltage;
    bool haveFrame = false;

    BearaminVirtual() {
        config(NUM_PARAMS, 0, NUM_OUTPUTS);
        configParam(RANGE_PARAM, 0.f, 1.f, 1.f, "Range", "%", 0.f, 100.f);
        configParam(SLEW_PARAM, 0.f, 2.f, 0.15f, "Slew", " s");
        static const char* labels[NUM_OUTPUTS] = {"X","Y","Z","X bipolar","Y bipolar","Z bipolar","Presence","Spank"};
        for (int i=0;i<NUM_OUTPUTS;++i) configOutput(i, labels[i]);
        for (auto& cell : display) cell.store(0);
        worker = std::thread([this]{ run(); });
    }
    ~BearaminVirtual() override { stopping.store(true); if (worker.joinable()) worker.join(); }
    void selectPort(const std::string& port) { std::lock_guard<std::mutex> lock(selectionMutex); selectedPort = port; }
    std::string port() { std::lock_guard<std::mutex> lock(selectionMutex); return selectedPort; }
    void push(const Event& e) {
        unsigned w = head.load(std::memory_order_relaxed);
        if (w - tail.load(std::memory_order_acquire) >= queue.size()) return;
        queue[w % queue.size()] = e;
        head.store(w+1,std::memory_order_release);
    }
    void run() {
        bearfield::Processor processor(bearfield::BearCvV1());
        std::string active;
        while (!stopping.load()) {
            std::string requested = port();
            if (requested.empty()) { state.store(DISCONNECTED); std::this_thread::sleep_for(std::chrono::milliseconds(100)); continue; }
            bool busy = false;
            {
                std::lock_guard<std::mutex> lock(claimsMutex);
                busy = !claimedPorts.insert(requested).second;
            }
            if (busy) { state.store(BUSY); std::this_thread::sleep_for(std::chrono::milliseconds(200)); continue; }
            active = requested; state.store(CONNECTING);
            SerialPort serial;
            if (!serial.openPort(active)) { state.store(DISCONNECTED); }
            else {
                std::string line;
                bool overflow = false, seen = false;
                uint32_t boot = 0, retiredBoot = 0, seq = 0;
                bool retired = false;
                auto last = Clock::now();
                while (!stopping.load() && port() == active) {
                    char bytes[256]; int count = serial.readBytes(bytes,sizeof(bytes));
                    if (count < 0) break;
                    if (count == 0 && Clock::now()-last > std::chrono::milliseconds(250)) state.store(STALE);
                    for (int i=0;i<count;++i) {
                        char c=bytes[i];
                        if (c=='\n') {
                            if (!overflow && !line.empty()) {
                                Message m{}; bool unsupported = false;
                                if (parseLine(line,m,unsupported)) {
                                    if (retired && m.boot == retiredBoot) { line.clear(); overflow=false; continue; }
                                    if (!seen || m.boot != boot) { if (seen) { retiredBoot = boot; retired = true; } boot=m.boot; seen=true; processor.reset(); seq=m.seq-1; push(Event{SENSOR_INIT}); }
                                    if (static_cast<int32_t>(m.seq-seq)>0) {
                                        seq=m.seq; last=Clock::now();
                                        if (m.frame) {
                                            bearfield::SensorMountConfig mount{};
                                            mount.rawToCanonical.rotation = static_cast<bearfield::QuarterTurns>(rotation.load());
                                            mount.rawToCanonical.mirrorX=mirrorX.load(); mount.rawToCanonical.mirrorY=mirrorY.load();
                                            auto canonical=bearfield::orientFrame(m.depth,mount);
                                            auto field=processor.process(canonical,m.capture);
                                            Event e{}; e.state=STREAMING; e.field=field;
                                            for (size_t j=0;j<64;++j) e.mm[j] = (canonical.validMask>>j)&1u ? canonical.distanceMm[j] : 0;
                                            if (field.present) e.point = static_cast<int>(std::round(field.yMilli*7.f/1000.f))*8 + static_cast<int>(std::round(field.xMilli*7.f/1000.f));
                                            push(e); state.store(STREAMING);
                                        } else { push(Event{m.state}); state.store(m.state); }
                                    }
                                } else if (unsupported) { push(Event{UNSUPPORTED}); state.store(UNSUPPORTED); }
                            }
                            line.clear(); overflow=false;
                        } else if (!overflow) {
                            if (line.size() < 1023) line.push_back(c); else { line.clear(); overflow=true; }
                        }
                    }
                }
            }
            serial.closePort();
            { std::lock_guard<std::mutex> lock(claimsMutex); claimedPorts.erase(active); }
            push(Event{DISCONNECTED}); state.store(DISCONNECTED);
            if (!stopping.load()) std::this_thread::sleep_for(std::chrono::milliseconds(300));
        }
    }
    void process(const ProcessArgs& args) override {
        unsigned r=tail.load(std::memory_order_relaxed);
        while (r != head.load(std::memory_order_acquire)) {
            latest=queue[r%queue.size()]; ++r;
            age=0.f;
            haveFrame=latest.state==STREAMING;
            if (haveFrame) { for (size_t i=0;i<64;++i) display[i].store(latest.mm[i]); point.store(latest.point); }
            else { for (auto& cell:display) cell.store(0); point.store(-1); }
        }
        tail.store(r,std::memory_order_release);
        age += args.sampleTime;
        State s=state.load();
        bool safe = !haveFrame || age>0.25f || s!=STREAMING;
        if (age>0.25f && s==STREAMING) state.store(STALE);
        voltage.step(latest.field,safe,params[RANGE_PARAM].getValue(),params[SLEW_PARAM].getValue(),args.sampleTime);
        for (int i=0;i<6;++i) outputs[i].setVoltage(voltage.cv[i]);
        outputs[PRES_OUTPUT].setVoltage(voltage.presence);
        outputs[SPANK_OUTPUT].setVoltage(voltage.spank);
    }
    json_t* dataToJson() override {
        json_t* root=json_object(); std::string p=port();
        json_object_set_new(root,"port",json_string(p.c_str()));
        json_object_set_new(root,"rotation",json_integer(rotation.load()));
        json_object_set_new(root,"mirrorX",json_boolean(mirrorX.load()));
        json_object_set_new(root,"mirrorY",json_boolean(mirrorY.load()));
        return root;
    }
    void dataFromJson(json_t* root) override {
        json_t* j=json_object_get(root,"port"); if (json_is_string(j)) selectPort(json_string_value(j));
        j=json_object_get(root,"rotation"); if (json_is_integer(j)) rotation.store(std::max(0,std::min(3,static_cast<int>(json_integer_value(j)))));
        j=json_object_get(root,"mirrorX"); if (json_is_boolean(j)) mirrorX.store(json_is_true(j));
        j=json_object_get(root,"mirrorY"); if (json_is_boolean(j)) mirrorY.store(json_is_true(j));
    }
};

struct FieldWidget : widget::Widget {
    BearaminVirtual* module=nullptr;
    void draw(const DrawArgs& args) override {
        nvgBeginPath(args.vg); nvgRoundedRect(args.vg,0,0,box.size.x,box.size.y,4); nvgFillColor(args.vg,nvgRGB(8,19,22)); nvgFill(args.vg);
        if (!module) return;
        float cell=box.size.x/8.f;
        for (int y=0;y<8;++y) for(int x=0;x<8;++x) {
            int i=y*8+x; uint16_t mm=module->display[i].load();
            if (!mm) continue;
            float strength=std::max(0.f,std::min(1.f,(750.f-mm)/690.f));
            nvgBeginPath(args.vg); nvgRect(args.vg,x*cell+1,y*cell+1,cell-2,cell-2);
            nvgFillColor(args.vg,nvgRGB(static_cast<unsigned char>(40+170*strength),static_cast<unsigned char>(90+130*strength),140)); nvgFill(args.vg);
        }
        int p=module->point.load(); if (p>=0 && p<64) { nvgBeginPath(args.vg); nvgCircle(args.vg,(p%8+.5f)*cell,(p/8+.5f)*cell,cell*.31f); nvgStrokeColor(args.vg,nvgRGB(255,230,140)); nvgStrokeWidth(args.vg,2); nvgStroke(args.vg); }
        if (module->state.load()!=STREAMING) { nvgBeginPath(args.vg); nvgRect(args.vg,0,box.size.y-4,box.size.x,4); nvgFillColor(args.vg,nvgRGB(220,75,65)); nvgFill(args.vg); }
    }
};
struct BearaminVirtualWidget : ModuleWidget {
    BearaminVirtualWidget(BearaminVirtual* module) {
        setModule(module); setPanel(createPanel(asset::plugin(pluginInstance,"res/BearaminVirtual.svg")));
        auto* field=new FieldWidget; field->module=module;
        field->box.pos=Vec(panel_layout::fieldTopLeft.x,panel_layout::fieldTopLeft.y);
        field->box.size=Vec(panel_layout::fieldSize.x,panel_layout::fieldSize.y); addChild(field);
        addParam(createParamCentered<RoundSmallBlackKnob>(Vec(panel_layout::range.x,panel_layout::range.y),module,BearaminVirtual::RANGE_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(Vec(panel_layout::slew.x,panel_layout::slew.y),module,BearaminVirtual::SLEW_PARAM));
        for(int i=0;i<8;++i)
            addOutput(createOutputCentered<PJ301MPort>(Vec(panel_layout::outputs[i].x,panel_layout::outputs[i].y),module,i));
    }
    void appendContextMenu(ui::Menu* menu) override {
        auto* m=dynamic_cast<BearaminVirtual*>(module); if (!m) return;
        menu->addChild(new ui::MenuSeparator);
        menu->addChild(createMenuLabel("USB serial port"));
        menu->addChild(createCheckMenuItem("None","",[m]{return m->port().empty();},[m]{m->selectPort("");}));
        for (auto& p:ports()) menu->addChild(createCheckMenuItem(p,"",[m,p]{return m->port()==p;},[m,p]{m->selectPort(p);}));
        menu->addChild(createMenuLabel("State: "+std::string([](State s){switch(s){case CONNECTING:return "connecting";case STREAMING:return "streaming";case STALE:return "stale";case SENSOR_INIT:return "sensor init";case SENSOR_FAULT:return "sensor fault";case UNSUPPORTED:return "unsupported protocol";case BUSY:return "port busy";default:return "disconnected";}}(m->state.load()))));
        menu->addChild(createMenuLabel("Orientation"));
        for (int turn=0;turn<4;++turn) menu->addChild(createCheckMenuItem(std::to_string(turn*90)+" degrees","",[m,turn]{return m->rotation.load()==turn;},[m,turn]{m->rotation.store(turn);}));
        menu->addChild(createCheckMenuItem("Mirror X","",[m]{return m->mirrorX.load();},[m]{m->mirrorX.store(!m->mirrorX.load());}));
        menu->addChild(createCheckMenuItem("Mirror Y","",[m]{return m->mirrorY.load();},[m]{m->mirrorY.store(!m->mirrorY.load());}));
    }
};
Model* modelBearaminVirtual=createModel<BearaminVirtual,BearaminVirtualWidget>("BearaminVirtual");
