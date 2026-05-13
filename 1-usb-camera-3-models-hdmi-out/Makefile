TARGET := demo
TARGET_OBJ := $(TARGET).o

SRC_DIR := regulus-camera-demo-src regulus-camera-demo-src/post_hdmi regulus-camera-demo-src/post_lcd
SRCS := $(foreach dir, $(SRC_DIR), $(wildcard $(dir)/*.cc))

OBJ_DIR := obj
OBJS := $(SRCS:%.cc=$(OBJ_DIR)/%.o) $(OBJ_DIR)/$(TARGET_OBJ)

DEPS := $(OBJS:.o=.d)

CXXFLAGS := -std=c++20 -O3 -fopenmp -pthread
CPPFLAGS := -MMD -MP \
		$(addprefix -I./, $(SRC_DIR)) \
		`pkg-config --cflags opencv4` \
		`pkg-config --cflags gstreamer-1.0` \
		-I.

LDFLAGS := `pkg-config --libs opencv4` \
		`pkg-config --libs gstreamer-1.0` -lgstapp-1.0 \
		-lqbruntime

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) $(OBJS) -o $(TARGET) $(LDFLAGS)

$(OBJ_DIR)/%.o: %.cc
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c $< -o $@

-include $(DEPS)

clean:
	rm -rf $(TARGET) $(OBJ_DIR) $(DEPS)
