SOURCES += src/plugin.cpp src/SlowLFO.cpp src/lite/BearaminVirtual.cpp
DISTRIBUTABLES += res LICENSE LICENSE-BEARFIELD
FLAGS += -Ithird_party/bearfield-core/include
FLAGS += -std=c++17
include $(RACK_DIR)/plugin.mk
