# Monitor Buddy simulator: the real src/main.cpp built for this PC.
#
#   make            build .build/mbsim
#   make test       build, then run every scenario (pytest)
#   make clean
#
# CONFIG picks the simulated Buddy's config.h folder; DEFS adds -D flags
# (a second build with other flags: make BUILD=.build-nokeys DEFS=-DMBSIM_NO_KEYS).
# SCREEN=WxH builds the same firmware for another panel size into .build-WxH
# (an experiment; other displays are not planned).
ROOT   := $(abspath ../..)
DEPS   := $(abspath .deps)
SCREEN ?=
ifeq ($(SCREEN),)
BUILD  ?= .build
else
BUILD  ?= .build-$(SCREEN)
SCREEN_DEFS := -DMBSIM_SCREEN_W=$(word 1,$(subst x, ,$(SCREEN))) -DMBSIM_SCREEN_H=$(word 2,$(subst x, ,$(SCREEN))) -Wno-narrowing
endif
# The libraries must be there before make reads the rules below: on a fresh
# checkout a rule found no source in .deps/ and the first `make` stopped.
ifeq ($(wildcard $(DEPS)/mbedtls/build/library/libmbedcrypto.a),)
ifeq ($(filter clean,$(MAKECMDGOALS)),)
_ := $(shell ./fetch_deps.sh >&2)
endif
endif
CONFIG ?= config
DEFS   ?=
CXX    ?= g++
CC     ?= gcc
OPT    ?= -O2

# The libraries are fetched while the Makefile is read: on a fresh checkout
# their sources must exist before make looks for rules to build them.
ifeq ($(filter clean,$(MAKECMDGOALS)),)
ifeq ($(wildcard $(DEPS)/mbedtls/build/library/libmbedcrypto.a),)
ifneq ($(shell ./fetch_deps.sh 1>&2 && echo ok),ok)
$(error fetch_deps.sh failed, see above)
endif
endif
endif

GFX := $(DEPS)/gfx/src
INC := -I host -I $(BUILD) -I $(CONFIG) -I $(ROOT)/src \
       $(foreach l,settings nightmode protocol display button coin github odoo,-I $(ROOT)/lib/$(l)) \
       $(foreach d,$(wildcard $(ROOT)/lib/*),-I $(d)) \
       -I $(GFX) -I $(DEPS)/ArduinoJson/src -I $(DEPS)/AnimatedGIF/src -I $(DEPS)/mbedtls/include
# The build date is fixed: the firmware seeds its clock from it, and a run must not depend on the day it was built.
CPPFLAGS := $(INC) $(DEFS) $(SCREEN_DEFS) -DMBSIM=1 -D__LINUX__ -DARDUINOJSON_ENABLE_ARDUINO_STRING=1 -DARDUINOJSON_ENABLE_ARDUINO_STREAM=1 \
            -DARDUINOJSON_ENABLE_ARDUINO_PRINT=1 -DARDUINOJSON_ENABLE_PROGMEM=0 \
            -Wno-builtin-macro-redefined '-D__DATE__="Jan  5 2026"' '-D__TIME__="08:00:00"'
CXXFLAGS := -std=gnu++17 $(OPT) -g -fno-strict-aliasing -w
CFLAGS   := $(OPT) -g -w

FIRMWARE := $(ROOT)/src/main.cpp $(wildcard $(ROOT)/lib/*/*.cpp)
HOST     := $(wildcard host/*.cpp)
LIBS     := $(GFX)/Arduino_GFX.cpp $(GFX)/Arduino_G.cpp $(GFX)/Arduino_DataBus.cpp $(GFX)/Arduino_TFT.cpp \
            $(GFX)/canvas/Arduino_Canvas.cpp $(GFX)/display/Arduino_ST7789.cpp $(DEPS)/AnimatedGIF/src/AnimatedGIF.cpp

OBJS := $(patsubst %.cpp,$(BUILD)/obj/%.o,$(notdir $(FIRMWARE) $(HOST) $(LIBS))) $(BUILD)/obj/u8g2_sim_fonts.o
vpath %.cpp $(sort $(dir $(FIRMWARE) $(HOST) $(LIBS)))

all: $(BUILD)/mbsim

$(DEPS)/mbedtls/build/library/libmbedcrypto.a:
	./fetch_deps.sh

$(BUILD)/u8g2_sim_fonts.h: $(ROOT)/src/main.cpp gen_fonts.py | $(DEPS)/mbedtls/build/library/libmbedcrypto.a
	@mkdir -p $(BUILD)/obj
	python3 gen_fonts.py $(ROOT)/src/main.cpp $(DEPS)/u8g2_fonts.c $(BUILD)/u8g2_sim_fonts.h $(BUILD)/u8g2_sim_fonts.c

$(BUILD)/obj/u8g2_sim_fonts.o: $(BUILD)/u8g2_sim_fonts.h
	$(CC) $(CFLAGS) -I host -I $(BUILD) -c $(BUILD)/u8g2_sim_fonts.c -o $@

$(BUILD)/obj/%.o: %.cpp $(BUILD)/u8g2_sim_fonts.h $(wildcard host/*.h) $(wildcard $(CONFIG)/*.h) $(wildcard $(ROOT)/src/*.h) $(wildcard $(ROOT)/lib/*/*.h)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

$(BUILD)/mbsim: $(OBJS)
	$(CXX) $(OBJS) $(DEPS)/mbedtls/build/library/libmbedcrypto.a -lutil -o $@

test: all
	python3 -m pytest tests -q

# the browser installer (web/) against the simulated firmware; needs playwright
web: all
	python3 -m pytest tests_web -q

# the film of a whole day: out/workday/workday.mp4, contact sheet, timeline
workday: all
	python3 workday.py out/workday

clean:
	rm -rf .build .build-*

.PHONY: all test clean
