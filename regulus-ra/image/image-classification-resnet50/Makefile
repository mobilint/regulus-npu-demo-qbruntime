
.PHONY: all clean

all: resnet50

resnet50: resnet50.cc
	$(CXX) -o $@ $^ -lqbruntime

clean:
	rm resnet50
