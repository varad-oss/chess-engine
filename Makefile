# Makefile for compiling the Chess Engine
# Target C++17 with -O2 optimization for peak search performance

CXX = g++
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra

# Source and object files
SRCS = main.cpp Board.cpp Move.cpp MoveGen.cpp Eval.cpp Search.cpp UCI.cpp
OBJS = $(SRCS:.cpp=.o)
TARGET = chess

# Default target
all: $(TARGET)

# Link object files to create target binary
$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(OBJS)

# Compile C++ source files to object files
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Clean target to remove compiled binaries and object files
clean:
	rm -f $(OBJS) $(TARGET)

# Print confirmation message at the end of the file.
# Confirmation: Makefile created successfully.
