#!/usr/bin/make -f
# Makefile for carla-discovery #
# ----------------------------------------- #
# Created by falkTX
#

CXX     ?= g++
WINECXX ?= wineg++

BUILD_FLAGS  = -O2 -ffast-math -fomit-frame-pointer -mtune=generic -msse -std=c++0x -Wall $(CXXFLAGS)
BUILD_FLAGS += -I../carla-backend -I../carla-includes $(shell pkg-config --cflags QtCore)
BUILD_FLAGS += -DNDEBUG -DQT_NO_DEBUG -DQT_NO_DEBUG_STREAM -DQT_NO_DEBUG_OUTPUT
BUILD_FLAGS += -DVESTIGE_HEADER -I../carla-includes/vestige # Comment this line to not use vestige header

32BIT_FLAGS  = -m32
64BIT_FLAGS  = -m64
LINK_FLAGS   = $(shell pkg-config --libs QtCore) $(LDFLAGS)

ifneq ($(NATIVE),)
BUILD_FLAGS += -DBUILD_NATIVE
HAVE_FLUIDSYNTH   = $(shell pkg-config --exists fluidsynth && echo true)
HAVE_LINUXSAMPLER = $(shell pkg-config --exists linuxsampler && echo true)

ifeq ($(HAVE_FLUIDSYNTH),true)
BUILD_FLAGS += $(shell pkg-config --cflags fluidsynth) -DWANT_FLUIDSYNTH
LINK_FLAGS  += $(shell pkg-config --libs fluidsynth)
endif

ifeq ($(HAVE_LINUXSAMPLER),true)
BUILD_FLAGS += $(shell pkg-config --cflags linuxsampler) -DWANT_LINUXSAMPLER
LINK_FLAGS  += $(shell pkg-config --libs linuxsampler)
endif
endif

UNIX_BUILD_FLAGS = $(BUILD_FLAGS)
UNIX_32BIT_FLAGS = $(32BIT_FLAGS) -L/usr/lib32 -L/usr/lib/i386-linux-gnu
UNIX_64BIT_FLAGS = $(64BIT_FLAGS) -L/usr/lib64 -L/usr/lib/x86_64-linux-gnu
UNIX_LINK_FLAGS  = $(LINK_FLAGS) -ldl

WIN_BUILD_FLAGS  = $(BUILD_FLAGS)
WIN_32BIT_FLAGS  = $(32BIT_FLAGS)
WIN_64BIT_FLAGS  = $(64BIT_FLAGS)
WIN_LINK_FLAGS   = $(LINK_FLAGS) -static

WINE_BUILD_FLAGS = $(BUILD_FLAGS) # -fpermissive
WINE_32BIT_FLAGS = $(32BIT_FLAGS) -L/usr/lib32/wine -L/usr/lib/i386-linux-gnu/wine
WINE_64BIT_FLAGS = $(64BIT_FLAGS) -L/usr/lib64/wine -L/usr/lib/x86_64-linux-gnu/wine
WINE_LINK_FLAGS  = $(LINK_FLAGS) -ldl

# --------------------------------------------------------------

all:
	@echo "Build type must be unix32, unix64, win[e]32 or win[e]64"

unix32: carla-discovery-unix32
unix64: carla-discovery-unix64
win32:  carla-discovery-win32.exe
win64:  carla-discovery-win64.exe
wine32: carla-discovery-win32.exe.so
wine64: carla-discovery-win64.exe.so

# --------------------------------------------------------------

32BIT_OBJS = carla-discovery.cpp ../carla-lilv/carla_lilv_32bit.a
64BIT_OBJS = carla-discovery.cpp ../carla-lilv/carla_lilv_64bit.a

carla-discovery-unix32: $(32BIT_OBJS)
	$(CXX) $^ $(UNIX_BUILD_FLAGS) $(UNIX_32BIT_FLAGS) $(UNIX_LINK_FLAGS) -o $@ && strip $@

carla-discovery-unix64: $(64BIT_OBJS)
	$(CXX) $^ $(UNIX_BUILD_FLAGS) $(UNIX_64BIT_FLAGS) $(UNIX_LINK_FLAGS) -o $@ && strip $@

carla-discovery-win32.exe: $(32BIT_OBJS)
	$(CXX) $^ $(WIN_BUILD_FLAGS) $(WIN_32BIT_FLAGS) $(WIN_LINK_FLAGS) -o $@

carla-discovery-win64.exe: $(64BIT_OBJS)
	$(CXX) $^ $(WIN_BUILD_FLAGS) $(WIN_64BIT_FLAGS) $(WIN_LINK_FLAGS) -o $@

carla-discovery-win32.exe.so: $(32BIT_OBJS)
	$(WINECXX) $^ $(WINE_BUILD_FLAGS) $(WINE_32BIT_FLAGS) $(WINE_LINK_FLAGS) -o carla-discovery-win32.exe

carla-discovery-win64.exe.so: $(64BIT_OBJS)
	$(WINECXX) $^ $(WINE_BUILD_FLAGS) $(WINE_64BIT_FLAGS) $(WINE_LINK_FLAGS) -o carla-discovery-win64.exe

../carla-lilv/carla_lilv_32bit.a:
	$(MAKE) -C ../carla-lilv 32bit

../carla-lilv/carla_lilv_64bit.a:
	$(MAKE) -C ../carla-lilv 64bit

# --------------------------------------------------------------

clean:
	rm -f carla-discovery-*
