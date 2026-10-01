CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic
INCLUDES  = -Iinclude
SRC       = src/myers.cpp src/tokenize.cpp src/format.cpp src/patch.cpp src/slide.cpp
OBJ       = $(SRC:src/%.cpp=build/%.o)

all: build/mydiff

build/%.o: src/%.cpp include/mydiff/*.hpp | build
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

build/mydiff: $(OBJ) build/main.o
	$(CXX) $(CXXFLAGS) $^ -o $@

build/test_myers: $(OBJ) tests/test_myers.cpp | build
	$(CXX) $(CXXFLAGS) $(INCLUDES) $^ -o $@

build:
	mkdir -p build

test: build/mydiff build/test_myers
	./build/test_myers
	./tests/compat.sh ./build/mydiff

clean:
	rm -rf build

.PHONY: all test clean
