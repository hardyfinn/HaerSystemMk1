# Codex: a VCV Rack 2 plugin.
#   make            build the plugin        (needs the Rack SDK: make RACK_DIR=/path/to/Rack-SDK)
#   make dist       build the .vcvplugin file into dist/
#   make install    copy the plugin into Rack's plugin folder
#   make test       build and run the unit tests (does not need the Rack SDK)
RACK_DIR ?= ../Rack-SDK

FLAGS += -Isrc
SOURCES += $(wildcard src/*.cpp) $(wildcard src/common/*.cpp) $(wildcard src/dsp/*.cpp)
SOURCES += $(wildcard src/modules/*.cpp) $(wildcard src/widgets/*.cpp)
DISTRIBUTABLES += res
DISTRIBUTABLES += $(wildcard LICENSE*)

# The tests are plain C++ and must run without the SDK.
ifneq ($(MAKECMDGOALS),test)
include $(RACK_DIR)/plugin.mk
# The SDK asks for C++11; the DSP code is C++17. The later flag wins.
CXXFLAGS += -std=c++17
endif

# ---- unit tests (spec §14) ----
TEST_CXX ?= c++
TEST_SOURCES := $(wildcard tests/*.cpp) $(wildcard src/dsp/*.cpp) $(wildcard src/common/*.cpp)
TEST_HEADERS := $(wildcard tests/*.hpp) $(wildcard tests/*.h) $(wildcard src/dsp/*.hpp) $(wildcard src/common/*.hpp)
TEST_FLAGS := -std=c++17 -O1 -g -Wall -Wextra -fsanitize=address,undefined -fno-omit-frame-pointer -Isrc -Itests

.PHONY: test
test: build/tests
	./build/tests

build/tests: $(TEST_SOURCES) $(TEST_HEADERS)
	@mkdir -p build
	$(TEST_CXX) $(TEST_FLAGS) -o $@ $(TEST_SOURCES)
