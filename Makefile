# SPDX-License-Identifier: MIT
# Squatch Tuner: header-only core in include/, measured by bench/, tested by tests/ and
# hosts/tests/. No dependencies to fetch: a C++17 compiler and make.
# clang++ where it exists, g++ otherwise. CXX=... on the command line wins.
ifeq ($(origin CXX),default)
CXX := $(if $(shell command -v clang++ 2>/dev/null),clang++,g++)
endif
CXXFLAGS ?= -std=c++17 -O3 -Wall -Wextra -Wpedantic -Wshadow -Iinclude
BUILD := build

BENCH_COMMON := bench/pv.cpp bench/poly_real.cpp
HEADERS := $(wildcard include/squatch/tuner/*.hpp) $(wildcard bench/*.hpp)
HOST_HEADERS := $(wildcard hosts/common/*.hpp) $(wildcard hosts/jack/*.hpp) include/squatch/dsp/latest.hpp

# The Guitar-TECHS single-note DI recordings (CC BY 4.0), downloaded separately: see RESULTS.md.
GUITAR_TECHS ?= data/guitar-techs
P1 := $(GUITAR_TECHS)/P1_singlenotes/P1_singlenotes/audio/directinput/directinput_allsinglenotes.wav
P2 := $(GUITAR_TECHS)/P2_singlenotes/P2_singlenotes/audio/directinput/directinput_allsinglenotes.wav
DEMO := $(BUILD)/web-demo

.PHONY: all test results lint live demo demo-test verify-export check-upstream clean
all: $(BUILD)/tests $(BUILD)/eval_synth $(BUILD)/eval_real $(BUILD)/eval_poly $(BUILD)/perf

$(BUILD)/%: bench/%.cpp $(BENCH_COMMON) $(HEADERS) | $(BUILD)
	$(CXX) $(CXXFLAGS) -o $@ $< $(BENCH_COMMON) -lpthread

$(BUILD)/tests: tests/test_core.cpp $(HEADERS) | $(BUILD)
	$(CXX) $(CXXFLAGS) -fsanitize=address,undefined -o $@ $< $(BENCH_COMMON)

# The host layer's handoff is tested for data races, so it gets ThreadSanitizer, not ASan.
# Where TSan cannot run (a Pi 5's 47-bit address space): make test HOST_SAN=
HOST_SAN ?= -fsanitize=thread
$(BUILD)/test_hosts: hosts/tests/test_hosts.cpp $(HEADERS) $(HOST_HEADERS) | $(BUILD)
	$(CXX) $(CXXFLAGS) -O1 -g $(HOST_SAN) -o $@ $< -lpthread

$(BUILD):
	mkdir -p $(BUILD)

test: $(BUILD)/tests $(BUILD)/test_hosts
	./$(BUILD)/tests
	./$(BUILD)/test_hosts

# The live JACK host and its exact test tone (hosts/jack). Needs the JACK headers
# (libjack-jackd2-dev on Debian and Ubuntu); not part of `all`.
live: $(BUILD)/squatch-tuner-live $(BUILD)/squatch-tone

$(BUILD)/squatch-tuner-live: hosts/jack/tuner_live.cpp $(HEADERS) $(HOST_HEADERS) | $(BUILD)
	$(CXX) $(CXXFLAGS) -o $@ $< -ljack -lpthread

$(BUILD)/squatch-tone: hosts/jack/tone.cpp $(HOST_HEADERS) | $(BUILD)
	$(CXX) $(CXXFLAGS) -o $@ $< -ljack -lpthread

# The browser demo, built into build/web-demo (docs/ holds the published copy). Needs
# Emscripten on PATH; demo-test also needs Node 22 and Chrome.
demo:
	python3 hosts/wasm/build.py $(DEMO)

demo-test: demo
	node hosts/wasm/test/core.test.mjs $(DEMO)/tuner.wasm
	node hosts/wasm/test/browser.test.mjs --dir $(DEMO)

# Fails if any exported file was edited here instead of upstream (see README).
verify-export:
	python3 tools/export-public tuner --verify .

# Fails if this copy of the tuner differs from squatch-dsp's main branch (the source).
SQUATCH_DSP ?= https://github.com/squatch-stack/squatch-dsp.git
check-upstream:
	rm -rf $(BUILD)/squatch-dsp
	git clone -q --depth 1 $(SQUATCH_DSP) $(BUILD)/squatch-dsp
	python3 $(BUILD)/squatch-dsp/tools/export-public tuner --check --no-site .

# Every number in RESULTS.md (about 20 minutes on an M1 Max; the 48 kHz baselines dominate).
results: all
	mkdir -p results
	./$(BUILD)/perf > results/perf.md
	./$(BUILD)/eval_poly 300 $(P1) > results/poly.md
	./$(BUILD)/eval_real $(P1) $(P2) > results/real.md
	VARIANTS=1 ./$(BUILD)/eval_real $(P1) $(P2) > results/real-variants.md
	PER_NOTE=1 ./$(BUILD)/eval_synth 8 --full-rate > results/synth.md

# Complexity limits: cyclomatic complexity 10, 60 lines and 5 parameters per function.
lint:
	lizard -C 10 -L 60 -a 5 -w include bench tests hosts

clean:
	rm -rf $(BUILD)
