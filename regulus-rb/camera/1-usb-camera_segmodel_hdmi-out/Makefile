BUILD_DIR ?= build
BIN := demo_usb_hdmi
SOURCES := demo.cc
OBJECTS := $(patsubst %.cc,$(BUILD_DIR)/%.o,$(SOURCES))
DEPS := $(OBJECTS:.o=.d)

GSTREAMER_PACKAGES := gstreamer-1.0 gstreamer-allocators-1.0 gstreamer-app-1.0 gstreamer-video-1.0

CPPFLAGS += $(shell pkg-config --cflags opencv4 $(GSTREAMER_PACKAGES))
CXXFLAGS += -std=c++17 -O3 -march=armv8-a -pthread -MMD -MP
LDLIBS += -lopencv_imgproc -lopencv_core \
	$(shell pkg-config --libs $(GSTREAMER_PACKAGES)) \
	-lqbruntime -lsp500 -pthread

.PHONY: all clean
all: $(BIN)

$(BIN): $(OBJECTS)
	$(CXX) $(CXXFLAGS) $(OBJECTS) $(LDFLAGS) $(LDLIBS) -o $@

$(BUILD_DIR)/%.o: %.cc
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

clean:
	$(RM) $(OBJECTS) $(DEPS) $(BIN)

-include $(DEPS)
