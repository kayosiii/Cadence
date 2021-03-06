/*
 * Carla shared OSC code
 * Copyright (C) 2012 Filipe Coelho <falktx@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * For a full copy of the GNU General Public License see the COPYING file
 */

#ifndef CARLA_OSC_INCLUDES_H
#define CARLA_OSC_INCLUDES_H

#include "carla_includes.h"

#include <lo/lo.h>

#ifdef BUILD_BRIDGE
#define OSC_SEND_ARGS
#define OSC_HANDLE_ARGS
#else
#ifdef CARLA_BACKEND_NO_NAMESPACE
class CarlaPlugin;
#else
namespace CarlaBackend {
class CarlaPlugin;
}
typedef CarlaBackend::CarlaPlugin CarlaPlugin;
#endif
#define OSC_SEND_ARGS const OscData* const,
#define OSC_HANDLE_ARGS CarlaPlugin*,
#endif

struct OscData {
    char* path;
    lo_address source;
    lo_address target;
};

void osc_init(const char*);
void osc_close();
void osc_clear_data(OscData*);

void osc_error_handler(int num, const char* msg, const char* path);
int  osc_message_handler(const char* path, const char* types, lo_arg** argv, int argc, void* data, void* user_data);

int osc_handle_configure(OSC_HANDLE_ARGS lo_arg** argv);
int osc_handle_control(OSC_HANDLE_ARGS lo_arg** argv);
int osc_handle_program(OSC_HANDLE_ARGS lo_arg** argv);
int osc_handle_midi_program(OSC_HANDLE_ARGS lo_arg** argv);
int osc_handle_midi(OSC_HANDLE_ARGS lo_arg** argv);
#ifdef BUILD_BRIDGE
int osc_handle_show();
int osc_handle_hide();
int osc_handle_quit();
#endif

void osc_send_configure(OSC_SEND_ARGS const char* const key, const char* const value);
void osc_send_control(OSC_SEND_ARGS int control, double value);
void osc_send_program(OSC_SEND_ARGS int program);
void osc_send_midi_program(OSC_SEND_ARGS int bank, int program, bool);
void osc_send_midi(OSC_SEND_ARGS uint8_t buf[4]);
#ifndef BUILD_BRIDGE
void osc_send_show(const OscData* const);
void osc_send_hide(const OscData* const);
void osc_send_quit(const OscData* const);
#endif

#endif // CARLA_OSC_INCLUDES_H
