# Makefile for compiling the Chess Engine
# Target C++17 with -O2 optimization for peak search performance

CXX = g++
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra -Iinclude

# Source files
SRCS = src/main.cpp src/Board.cpp src/Move.cpp src/MoveGen.cpp src/Eval.cpp src/Search.cpp src/UCI.cpp
OBJS = $(SRCS:src/%.cpp=build/%.o)
TARGET = chess

# Default target
all: build $(TARGET)

# Create build directory if it doesn't exist
build:
	mkdir -p build

# Link object files to create target binary
$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(OBJS)

# Compile C++ source files to object files in the build directory
build/%.o: src/%.cpp | build
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Clean target to remove compiled binaries and object files
clean:
	rm -rf build $(TARGET)
