# Build configurations (select with CONFIG=...):
#   release   optimised engine (default)
#   debug     -O0 -g with libc++/libstdc++ assertions
#   asan      AddressSanitizer + UndefinedBehaviorSanitizer
#   tsan      ThreadSanitizer (for the multi-threaded search)
#
# Main targets:
#   make                  build build/release/chess-engine
#   make test             unit tests + UCI and HTTP-server integration tests
#   make frontend-test    perft check of the web demo's JavaScript move generator (needs node)
#   make perft            full perft suite at the recorded depths
#   make bench            fixed-depth search benchmark (node count + speed)
#   make check            test + perft under the release build
#   make sanitize         unit tests and a bench run under ASan/UBSan and TSan
#   make tools            match runner and tuner (build/release/match, .../tune)
#
# ARCH=native adds -mcpu/-march=native (not portable to other machines).

CXX      ?= c++
CONFIG   ?= release
ARCH     ?=
PYTHON   ?= python3

BUILD_DIR := build/$(CONFIG)
WARNINGS  := -Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wcast-align \
             -Woverloaded-virtual -Wnull-dereference -Wdouble-promotion -Wformat=2
CXXFLAGS  := -std=c++20 $(WARNINGS) -Isrc -MMD -MP
LDFLAGS   := -pthread

ifeq ($(CONFIG),release)
  CXXFLAGS += -O3 -DNDEBUG
else ifeq ($(CONFIG),debug)
  CXXFLAGS += -O0 -g -D_GLIBCXX_ASSERTIONS -D_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_DEBUG
else ifeq ($(CONFIG),asan)
  CXXFLAGS += -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=undefined
  LDFLAGS  += -fsanitize=address,undefined
else ifeq ($(CONFIG),tsan)
  CXXFLAGS += -O1 -g -fsanitize=thread
  LDFLAGS  += -fsanitize=thread
else
  $(error unknown CONFIG '$(CONFIG)')
endif

ifeq ($(ARCH),native)
  ifeq ($(shell uname -m),arm64)
    CXXFLAGS += -mcpu=native
  else
    CXXFLAGS += -march=native
  endif
endif

LIB_SRCS    := $(wildcard src/core/*.cpp src/engine/*.cpp src/uci/*.cpp src/server/*.cpp)
ENGINE_SRCS := $(LIB_SRCS) $(wildcard src/app/*.cpp) src/main.cpp
TEST_SRCS   := $(LIB_SRCS) $(wildcard src/app/*.cpp) $(wildcard tests/*.cpp)
MATCH_SRCS  := $(wildcard src/core/*.cpp) $(wildcard tools/match/*.cpp)
TUNE_SRCS   := $(wildcard src/core/*.cpp) src/engine/eval.cpp src/engine/eval_params.cpp $(wildcard tools/tune/*.cpp)

obj = $(patsubst %.cpp,$(BUILD_DIR)/obj/%.o,$(1))

ENGINE := $(BUILD_DIR)/chess-engine
TESTS  := $(BUILD_DIR)/tests
MATCH  := $(BUILD_DIR)/match
TUNE   := $(BUILD_DIR)/tune

.PHONY: all engine tools test unit-test uci-test serve-test frontend-test perft bench check sanitize clean

all: engine
engine: $(ENGINE)
tools: $(MATCH) $(TUNE)

$(ENGINE): $(call obj,$(ENGINE_SRCS))
	$(CXX) $^ -o $@ $(LDFLAGS)

$(TESTS): $(call obj,$(TEST_SRCS))
	$(CXX) $^ -o $@ $(LDFLAGS)

$(MATCH): $(call obj,$(MATCH_SRCS))
	$(CXX) $^ -o $@ $(LDFLAGS)

$(TUNE): $(call obj,$(TUNE_SRCS))
	$(CXX) $^ -o $@ $(LDFLAGS)

$(BUILD_DIR)/obj/tests/%.o: CXXFLAGS += -Itests -DTEST_DATA_DIR='"$(CURDIR)/tests/data"'

$(BUILD_DIR)/obj/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

unit-test: $(TESTS)
	$(TESTS)

uci-test: $(ENGINE)
	$(PYTHON) tests/integration/test_uci.py $(ENGINE)

serve-test: $(ENGINE)
	$(PYTHON) tests/integration/test_serve.py $(ENGINE)

frontend-test:
	node tests/integration/frontend_perft.js

test: unit-test uci-test serve-test

perft: $(ENGINE)
	$(ENGINE) perft-suite tests/data/perft.epd

bench: $(ENGINE)
	$(ENGINE) bench

check: test perft

sanitize:
	$(MAKE) CONFIG=asan unit-test
	$(MAKE) CONFIG=asan engine && build/asan/chess-engine bench 9
	$(MAKE) CONFIG=tsan unit-test
	$(MAKE) CONFIG=tsan engine && build/tsan/chess-engine bench 9 4

clean:
	rm -rf build

-include $(shell find build -name '*.d' 2>/dev/null)
