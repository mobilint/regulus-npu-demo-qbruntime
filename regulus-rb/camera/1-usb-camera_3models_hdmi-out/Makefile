DEMO_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
BUILD_DIR ?= build
BIN := demo_usb_hdmi

SOURCES := \
	demo.cc \
	src/core/argparse.cc \
	src/core/benchmarker.cc \
	src/core/qbmodel.cc \
	src/core/tsqueue.cc \
	src/gst/gst_base_pipeline.cc \
	src/gst/gst_capture_pipeline.cc \
	src/gst/gst_kmssink_pipeline.cc \
	src/stage/capture/capture_module.cc \
	src/stage/pre/dma350_copier.cc \
	src/stage/pre/pre_module.cc \
	src/stage/pre/resize_crop.cc \
	src/stage/infer/infer_module.cc \
	src/stage/decode/decode_module.cc \
	src/stage/plot/cma_display_ring.cc \
	src/stage/plot/dma350_composer.cc \
	src/stage/plot/draw_det.cc \
	src/stage/plot/draw_pose.cc \
	src/stage/plot/plot_module.cc \
	src/stage/display/display_module.cc

INCLUDE_DIRS := \
	$(DEMO_DIR)/src/core \
	$(DEMO_DIR)/src/gst \
	$(DEMO_DIR)/src/stage/capture \
	$(DEMO_DIR)/src/stage/pre \
	$(DEMO_DIR)/src/stage/infer \
	$(DEMO_DIR)/src/stage/decode \
	$(DEMO_DIR)/src/stage/plot \
	$(DEMO_DIR)/src/stage/display

GSTREAMER_PACKAGES := gstreamer-1.0 gstreamer-allocators-1.0 gstreamer-app-1.0
OBJECTS := $(patsubst %.cc,$(BUILD_DIR)/%.o,$(SOURCES))
DEPS := $(OBJECTS:.o=.d)

CPPFLAGS += $(addprefix -I,$(INCLUDE_DIRS)) \
	$(shell pkg-config --cflags opencv4 $(GSTREAMER_PACKAGES))
CXXFLAGS += -std=c++20 -O3 -fopenmp -pthread -MMD -MP
LDLIBS += -lopencv_imgcodecs -lopencv_imgproc -lopencv_core \
	$(shell pkg-config --libs $(GSTREAMER_PACKAGES)) \
	-lqbruntime -lsp500 -fopenmp -pthread

.PHONY: all clean
all: $(BIN)

$(BIN): $(OBJECTS)
	$(CXX) $(CXXFLAGS) $(OBJECTS) $(LDFLAGS) $(LDLIBS) -o $@

$(BUILD_DIR)/%.o: %.cc
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

clean:
	$(RM) -r $(BUILD_DIR) $(BIN)

-include $(DEPS)
