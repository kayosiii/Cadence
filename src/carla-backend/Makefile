#!/usr/bin/make -f
# Makefile for Carla-Backend #
# ------------------------------------- #
# Created by falkTX
#

CC  ?= gcc
CXX ?= g++

CARLA_C_FLAGS    = -Wall -fPIC -I. -I../carla-includes $(CFLAGS)
CARLA_CXX_FLAGS  = -Wall -std=c++0x -fPIC -I. -I../carla-includes `pkg-config --cflags jack fluidsynth liblo QtCore QtGui`
CARLA_CXX_FLAGS += -DCARLA_BACKEND_NO_NAMESPACE -DCARLA_ENGINE_JACK $(CXXFLAGS)
CARLA_CXX_FLAGS += -DVESTIGE_HEADER -I../carla-includes/vestige # Comment this line to not use vestige header
CARLA_CXX_FLAGS += -DNDEBUG -DQT_NO_DEBUG -DQT_NO_DEBUG_STREAM -DQT_NO_DEBUG_OUTPUT -O2 -ffast-math -fomit-frame-pointer -fvisibility=hidden -mtune=generic -msse
# CARLA_CXX_FLAGS += -DDEBUG -O0 -g
CARLA_LINK_FLAGS = -shared -fPIC -ldl `pkg-config --libs jack fluidsynth liblo QtCore QtGui` $(LDFLAGS)

OBJS = carla_backend.o carla_bridge.o carla_engine_jack.o carla_osc.o carla_shared.o carla_threads.o ladspa.o dssi.o lv2.o vst.o sf2.o lv2-rtmempool/rtmempool.o


all: carla_backend.so

carla_backend.so: $(OBJS)
	$(CXX) $(OBJS) ../carla-lilv/carla_lilv.a $(CARLA_LINK_FLAGS) -o carla_backend.so

carla_backend.a: $(OBJS)
	$(CXX) $(OBJS) ../carla-lilv/carla_lilv.a -static $(CARLA_LINK_FLAGS) -o carla_backend.a

.c.o:
	$(CC) -c $< $(CARLA_C_FLAGS) -o $@

.cpp.o:
	$(CXX) -c $< $(CARLA_CXX_FLAGS) -o $@

clean:
	rm -f *.o lv2-rtmempool/*.o *.so *.dll