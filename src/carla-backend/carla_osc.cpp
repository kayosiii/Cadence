/*
 * Carla Backend
 * Copyright (C) 2011-2012 Filipe Coelho <falktx@gmail.com>
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

#include "carla_osc.h"
#include "carla_plugin.h"

#include <iostream>

size_t client_name_len  = 0;
const char* client_name = nullptr;

const char* global_osc_server_path = nullptr;
lo_server_thread global_osc_server_thread = nullptr;
OscData global_osc_data = { nullptr, nullptr, nullptr };

// -------------------------------------------------------------------------------------------------------------------
// Exported symbols (API)

CARLA_BACKEND_START_NAMESPACE

const char* get_host_osc_url()
{
    qDebug("get_host_osc_url()");
    return global_osc_server_path;
}

CARLA_BACKEND_END_NAMESPACE

// End of exported symbols (API)
// -------------------------------------------------------------------------------------------------------------------

#ifndef CARLA_BACKEND_NO_NAMESPACE
using namespace CarlaBackend;
#endif

void osc_init(const char* host_client_name)
{
    qDebug("osc_init(%s)", host_client_name);
    client_name = strdup(host_client_name);
    client_name_len = strlen(client_name);

    // create new OSC thread
    global_osc_server_thread = lo_server_thread_new(nullptr, osc_error_handler);

    // get our full OSC server path
    char* osc_thread_path = lo_server_thread_get_url(global_osc_server_thread);
    global_osc_server_path = strdup(QString("%1%2").arg(osc_thread_path).arg(client_name).toUtf8().constData());
    free(osc_thread_path);

    // register message handler and start OSC thread
    lo_server_thread_add_method(global_osc_server_thread, nullptr, nullptr, osc_message_handler, nullptr);
    lo_server_thread_start(global_osc_server_thread);

    // print our server path just to make sure everything is ok
    printf("Carla-Backend OSC -> %s\n", global_osc_server_path);
}

void osc_close()
{
    qDebug("osc_close()");

    osc_clear_data(&global_osc_data);

    lo_server_thread_stop(global_osc_server_thread);
    lo_server_thread_del_method(global_osc_server_thread, nullptr, nullptr);
    lo_server_thread_free(global_osc_server_thread);

    free((void*)global_osc_server_path);
    global_osc_server_path = nullptr;

    free((void*)client_name);
    client_name = nullptr;
    client_name_len = 0;
}

void osc_clear_data(OscData* osc_data)
{
    qDebug("osc_clear_data(%p)", osc_data);

    if (osc_data->path)
        free((void*)osc_data->path);

    if (osc_data->source)
        lo_address_free(osc_data->source);

    if (osc_data->target)
        lo_address_free(osc_data->target);

    osc_data->path = nullptr;
    osc_data->source = nullptr;
    osc_data->target = nullptr;
}

// -------------------------------------------------------------------------------------------------------------------

void osc_error_handler(int num, const char* msg, const char* path)
{
    qCritical("osc_error_handler(%i, %s, %s)", num, msg, path);
}

int osc_message_handler(const char* path, const char* types, lo_arg** argv, int argc, void* data, void* user_data)
{
#ifdef DEBUG
    qDebug("osc_message_handler(%s, %s, %p, %i, %p, %p)", path, types, argv, argc, data, user_data);
#endif

    // Initial path check
    if (strcmp(path, "/register") == 0)
    {
        lo_message message = lo_message(data);
        lo_address source  = lo_message_get_source(message);
        return osc_handle_register(argv, source);
    }

    if (strcmp(path, "/unregister") == 0)
    {
        return osc_handle_unregister();
    }

    // Check if message is for this client
    if (strlen(path) <= client_name_len || strncmp(path+1, client_name, client_name_len) != 0)
    {
        qWarning("osc_message_handler() - message not for this client -> '%s' != '/%s/'", path, client_name);
        return 1;
    }

    // Get id from message
    int plugin_id = 0;

    if (std::isdigit(path[client_name_len+2]))
        plugin_id += path[client_name_len+2]-'0';

    if (std::isdigit(path[client_name_len+3]))
        plugin_id += (path[client_name_len+3]-'0')*10;

    if (plugin_id < 0 || plugin_id > MAX_PLUGINS)
    {
        qCritical("osc_message_handler() - failed to get plugin_id -> %i", plugin_id);
        return 1;
    }

    CarlaPlugin* plugin = CarlaPlugins[plugin_id];

    if (plugin == nullptr || plugin->id() != plugin_id)
    {
        qWarning("osc_message_handler() - invalid plugin '%i', probably has been removed", plugin_id);
        return 1;
    }

    // Get method from path (/Carla/i/method)
    int offset = (plugin_id >= 10) ? 4 : 3;
    char method[32] = { 0 };
    memcpy(method, path + (client_name_len + offset), 32);

    // Common OSC methods
    if (strcmp(method, "/update") == 0)
    {
        lo_message message = lo_message(data);
        lo_address source  = lo_message_get_source(message);
        return osc_handle_update(plugin, argv, source);
    }
    if (strcmp(method, "/configure") == 0)
        return osc_handle_configure(plugin, argv);
    if (strcmp(method, "/control") == 0)
        return osc_handle_control(plugin, argv);
    if (strcmp(method, "/program") == 0)
        return osc_handle_program(plugin, argv);
    if (strcmp(method, "/midi") == 0)
        return osc_handle_midi(plugin, argv);
    if (strcmp(method, "/exiting") == 0)
        return osc_handle_exiting(plugin);

    // Plugin-specific methods
    if (strcmp(method, "/lv2_atom_transfer") == 0)
        return osc_handle_lv2_atom_transfer(plugin, argv);
    if (strcmp(method, "/lv2_event_transfer") == 0)
        return osc_handle_lv2_event_transfer(plugin, argv);

    // Plugin Bridges
    if (plugin->hints() & PLUGIN_IS_BRIDGE)
    {
        if (strcmp(method, "/bridge_ains_peak") == 0)
            return osc_handle_bridge_ains_peak(plugin, argv);
        if (strcmp(method, "/bridge_aouts_peak") == 0)
            return osc_handle_bridge_aouts_peak(plugin, argv);
        if (strcmp(method, "/bridge_audio_count") == 0)
            return plugin->setOscBridgeInfo(PluginBridgeAudioCount, argv);
        if (strcmp(method, "/bridge_midi_count") == 0)
            return plugin->setOscBridgeInfo(PluginBridgeMidiCount, argv);
        if (strcmp(method, "/bridge_param_count") == 0)
            return plugin->setOscBridgeInfo(PluginBridgeParameterCount, argv);
        if (strcmp(method, "/bridge_program_count") == 0)
            return plugin->setOscBridgeInfo(PluginBridgeProgramCount, argv);
        if (strcmp(method, "/bridge_midi_program_count") == 0)
            return plugin->setOscBridgeInfo(PluginBridgeMidiProgramCount, argv);
        if (strcmp(method, "/bridge_plugin_info") == 0)
            return plugin->setOscBridgeInfo(PluginBridgePluginInfo, argv);
        if (strcmp(method, "/bridge_param_info") == 0)
            return plugin->setOscBridgeInfo(PluginBridgeParameterInfo, argv);
        if (strcmp(method, "/bridge_param_data") == 0)
            return plugin->setOscBridgeInfo(PluginBridgeParameterDataInfo, argv);
        if (strcmp(method, "/bridge_param_ranges") == 0)
            return plugin->setOscBridgeInfo(PluginBridgeParameterRangesInfo, argv);
        if (strcmp(method, "/bridge_program_info") == 0)
            return plugin->setOscBridgeInfo(PluginBridgeProgramInfo, argv);
        if (strcmp(method, "/bridge_midi_program_info") == 0)
            return plugin->setOscBridgeInfo(PluginBridgeMidiProgramInfo, argv);
        if (strcmp(method, "/bridge_custom_data") == 0)
            return plugin->setOscBridgeInfo(PluginBridgeCustomData, argv);
        if (strcmp(method, "/bridge_chunk_data") == 0)
            return plugin->setOscBridgeInfo(PluginBridgeChunkData, argv);
        if (strcmp(method, "/bridge_update") == 0)
            return plugin->setOscBridgeInfo(PluginBridgeUpdateNow, argv);
    }

    // Internal OSC Stuff
    if (global_osc_data.target)
    {
        if (strcmp(method, "/set_active") == 0)
            return osc_handle_set_active(plugin, argv);
        if (strcmp(method, "/set_drywet") == 0)
            return osc_handle_set_drywet(plugin, argv);
        if (strcmp(method, "/set_vol") == 0)
            return osc_handle_set_volume(plugin, argv);
        if (strcmp(method, "/set_balance_left") == 0)
            return osc_handle_set_balance_left(plugin, argv);
        if (strcmp(method, "/set_balance_right") == 0)
            return osc_handle_set_balance_right(plugin, argv);
        if (strcmp(method, "/set_parameter") == 0)
            return osc_handle_set_parameter(plugin, argv);
        if (strcmp(method, "/set_program") == 0)
            return osc_handle_set_program(plugin, argv);
        if (strcmp(method, "/note_on") == 0)
            return osc_handle_note_on(plugin, argv);
        if (strcmp(method, "/note_off") == 0)
            return osc_handle_note_off(plugin, argv);
    }

    qWarning("osc_message_handler() - unsupported OSC method '%s'", method);

    return 1;

    Q_UNUSED(types);
    Q_UNUSED(argc);
    Q_UNUSED(user_data);
}

// -------------------------------------------------------------------------------------------------------------------

int osc_handle_configure(CarlaPlugin* plugin, lo_arg** argv)
{
    qDebug("osc_handle_configure()");

    const char* key   = (const char*)&argv[0]->s;
    const char* value = (const char*)&argv[1]->s;

    if (plugin->hints() & PLUGIN_IS_BRIDGE)
    {
        if (strcmp(key, CARLA_BRIDGE_MSG_HIDE_GUI) == 0)
        {
            callback_action(CALLBACK_SHOW_GUI, plugin->id(), 0, 0, 0.0);
            return 0;
        }

        if (strcmp(key, CARLA_BRIDGE_MSG_SAVED) == 0)
        {
            return plugin->setOscBridgeInfo(PluginBridgeSaved, nullptr);
        }
    }

    plugin->setCustomData(CUSTOM_DATA_STRING, key, value, false);

    return 0;
}

int osc_handle_control(CarlaPlugin* plugin, lo_arg** argv)
{
    qDebug("osc_handle_control()");

    int rindex  = argv[0]->i;
    float value = argv[1]->f;

    plugin->setParameterValueByRIndex(rindex, value, false, true, true);

    return 0;
}

int osc_handle_program(CarlaPlugin* plugin, lo_arg** argv)
{
    qDebug("osc_handle_program()");

    if (plugin->type() == PLUGIN_DSSI)
    {
        uint32_t bank_id    = argv[0]->i;
        uint32_t program_id = argv[1]->i;

        plugin->setMidiProgramById(bank_id, program_id, false, true, true, true);
    }
    else
    {
        uint32_t program_id = argv[0]->i;

        if (program_id < plugin->programCount())
        {
            plugin->setProgram(program_id, false, true, true, true);
            return 0;
        }

        qCritical("osc_handle_program() - program_id '%i' out of bounds", program_id);
    }

    return 1;
}

int osc_handle_midi(CarlaPlugin* plugin, lo_arg **argv)
{
    qDebug("osc_handle_midi()");

    if (plugin->midiInCount() > 0)
    {
        uint8_t* data  = argv[0]->m;
        uint8_t status = data[1];

        // Fix bad note-off
        if (MIDI_IS_STATUS_NOTE_ON(status) && data[3] == 0)
            status -= 0x10;

        if (MIDI_IS_STATUS_NOTE_OFF(status))
        {
            uint8_t note = data[2];
            plugin->sendMidiSingleNote(note, 0, false, true, true);
        }
        else if (MIDI_IS_STATUS_NOTE_ON(status))
        {
            uint8_t note = data[2];
            uint8_t velo = data[3];
            plugin->sendMidiSingleNote(note, velo, false, true, true);
        }

        return 0;
    }

    qWarning("osc_handle_midi() - recived midi when plugin has no midi inputs");
    return 1;
}

// -------------------------------------------------------------------------------------------------------------------

int osc_handle_register(lo_arg** argv, lo_address source)
{
    qDebug("osc_handle_register()");

    if (! global_osc_data.path)
    {
        const char* url = (const char*)&argv[0]->s;
        const char* host;
        const char* port;

        qDebug("osc_handle_register() - OSC backend registered to %s", url);

        host = lo_address_get_hostname(source);
        port = lo_address_get_port(source);
        global_osc_data.source = lo_address_new(host, port);

        host = lo_url_get_hostname(url);
        port = lo_url_get_port(url);
        global_osc_data.path   = lo_url_get_path(url);
        global_osc_data.target = lo_address_new(host, port);

        free((void*)host);
        free((void*)port);

        for (unsigned short i=0; i<MAX_PLUGINS; i++)
        {
            CarlaPlugin* plugin = CarlaPlugins[i];
            if (plugin && plugin->enabled())
                plugin->registerToOsc();
        }

        return 0;
    }
    else
        qWarning("osc_handle_register() - OSC backend already registered to %s", global_osc_data.path);

    return 1;
}

int osc_handle_unregister()
{
    qDebug("osc_handle_unregister()");

    if (global_osc_data.path)
    {
        osc_clear_data(&global_osc_data);
        return 0;
    }
    else
        qWarning("osc_handle_unregister() - OSC backend is not registered yet");

    return 1;
}

// -------------------------------------------------------------------------------------------------------------------

int osc_handle_update(CarlaPlugin* plugin, lo_arg** argv, lo_address source)
{
    qDebug("osc_handle_update()");

    const char* url = (const char*)&argv[0]->s;
    plugin->updateOscData(source, url);

    return 0;
}

int osc_handle_exiting(CarlaPlugin* plugin)
{
    qDebug("osc_handle_exiting()");

    // TODO - check for non-UIs (dssi-vst) and set to -1 instead
    callback_action(CALLBACK_SHOW_GUI, plugin->id(), 0, 0, 0.0);
    plugin->clearOscData();

    return 0;
}

// -------------------------------------------------------------------------------------------------------------------

int osc_handle_set_active(CarlaPlugin* plugin, lo_arg** argv)
{
    qDebug("osc_handle_set_active()");

    bool value = (bool)argv[0]->i;
    plugin->setActive(value, false, true);

    return 0;
}

int osc_handle_set_drywet(CarlaPlugin* plugin, lo_arg** argv)
{
    qDebug("osc_handle_set_drywet()");

    double value = argv[0]->f;
    plugin->setDryWet(value, false, true);

    return 0;
}

int osc_handle_set_volume(CarlaPlugin* plugin, lo_arg** argv)
{
    qDebug("osc_handle_set_volume()");

    double value = argv[0]->f;
    plugin->setVolume(value, false, true);

    return 0;
}

int osc_handle_set_balance_left(CarlaPlugin* plugin, lo_arg** argv)
{
    qDebug("osc_handle_set_balance_left()");

    double value = argv[0]->f;
    plugin->setBalanceLeft(value, false, true);

    return 0;
}

int osc_handle_set_balance_right(CarlaPlugin* plugin, lo_arg** argv)
{
    qDebug("osc_handle_set_balance_right()");

    double value = argv[0]->f;
    plugin->setBalanceRight(value, false, true);

    return 0;
}

int osc_handle_set_parameter(CarlaPlugin* plugin, lo_arg** argv)
{
    qDebug("osc_handle_set_parameter()");

    uint32_t parameter_id = argv[0]->i;
    double value = argv[1]->f;
    plugin->setParameterValue(parameter_id, value, true, false, true);

    return 0;
}

int osc_handle_set_program(CarlaPlugin* plugin, lo_arg** argv)
{
    qDebug("osc_handle_set_program()");

    uint32_t program_id = argv[0]->i;
    plugin->setProgram(program_id, true, false, true, true);

    return 0;
}

int osc_handle_note_on(CarlaPlugin* plugin, lo_arg** argv)
{
    qDebug("osc_handle_note_on()");

    int note = argv[0]->i;
    int velo = argv[1]->i;
    plugin->sendMidiSingleNote(note, velo, true, false, true);

    return 0;
}

int osc_handle_note_off(CarlaPlugin* plugin, lo_arg** argv)
{
    qDebug("osc_handle_note_off()");

    int note = argv[0]->i;
    plugin->sendMidiSingleNote(note, 0, true, false, true);

    return 0;
}

int osc_handle_bridge_ains_peak(CarlaPlugin* plugin, lo_arg** argv)
{
    int index    = argv[0]->i;
    double value = argv[1]->f;

    ains_peak[(plugin->id()*2)+index-1] = value;
    return 0;
}

int osc_handle_bridge_aouts_peak(CarlaPlugin* plugin, lo_arg** argv)
{
    int index    = argv[0]->i;
    double value = argv[1]->f;

    aouts_peak[(plugin->id()*2)+index-1] = value;
    return 0;
}

// -------------------------------------------------------------------------------------------------------------------

void osc_send_configure(const OscData* const osc_data, const char* key, const char* value)
{
    qDebug("osc_send_configure(%s, %s)", key, value);
    if (osc_data->target)
    {
        char target_path[strlen(osc_data->path)+11];
        strcpy(target_path, osc_data->path);
        strcat(target_path, "/configure");
        lo_send(osc_data->target, target_path, "ss", key, value);
    }
}

void osc_send_control(const OscData* const osc_data, int index, double value)
{
    qDebug("osc_send_control(%i, %f)", index, value);
    if (osc_data->target)
    {
        char target_path[strlen(osc_data->path)+9];
        strcpy(target_path, osc_data->path);
        strcat(target_path, "/control");
        lo_send(osc_data->target, target_path, "if", index, value);
    }
}

void osc_send_program(const OscData* const osc_data, int program_id)
{
    qDebug("osc_send_program(%i)", program_id);
    if (osc_data->target)
    {
        char target_path[strlen(osc_data->path)+9];
        strcpy(target_path, osc_data->path);
        strcat(target_path, "/program");
        lo_send(osc_data->target, target_path, "i", program_id);
    }
}

void osc_send_midi_program(const OscData* const osc_data, int bank, int program, bool asProgram)
{
    qDebug("osc_send_midi_program(%i, %i)", bank, program);
    if (osc_data->target)
    {
        char target_path[strlen(osc_data->path)+14];
        strcpy(target_path, osc_data->path);
        strcat(target_path, asProgram ? "/program" : "/midi_program");
        lo_send(osc_data->target, target_path, "ii", bank, program);
    }
}

void osc_send_midi(const OscData* const osc_data, uint8_t buf[4])
{
    qDebug("osc_send_midi()");
    if (osc_data->target)
    {
        char target_path[strlen(osc_data->path)+6];
        strcpy(target_path, osc_data->path);
        strcat(target_path, "/midi");
        lo_send(osc_data->target, target_path, "m", buf);
    }
}

void osc_send_show(const OscData* const osc_data)
{
    qDebug("osc_send_show()");
    if (osc_data->target)
    {
        char target_path[strlen(osc_data->path)+6];
        strcpy(target_path, osc_data->path);
        strcat(target_path, "/show");
        lo_send(osc_data->target, target_path, "");
    }
}

void osc_send_hide(const OscData* const osc_data)
{
    qDebug("osc_send_hide()");
    if (osc_data->target)
    {
        char target_path[strlen(osc_data->path)+6];
        strcpy(target_path, osc_data->path);
        strcat(target_path, "/hide");
        lo_send(osc_data->target, target_path, "");
    }
}

void osc_send_quit(const OscData* const osc_data)
{
    qDebug("osc_send_quit()");
    if (osc_data->target)
    {
        char target_path[strlen(osc_data->path)+6];
        strcpy(target_path, osc_data->path);
        strcat(target_path, "/quit");
        lo_send(osc_data->target, target_path, "");
    }
}

// -------------------------------------------------------------------------------------------------------------------

//void osc_send_lv2_atom_transfer(OscData* osc_data, )

void osc_send_lv2_event_transfer(const OscData* const osc_data, const char* type, const char* key, const char* value)
{
    qDebug("osc_send_lv2_event_transfer(%s, %s, %s)", type, key, value);
    if (osc_data->target)
    {
        char target_path[strlen(osc_data->path)+20];
        strcpy(target_path, osc_data->path);
        strcat(target_path, "/lv2_event_transfer");
        lo_send(osc_data->target, target_path, "sss", type, key, value);
    }
}

// -------------------------------------------------------------------------------------------------------------------

bool osc_global_registered()
{
    return bool(global_osc_data.target);
}

void osc_global_send_add_plugin(int plugin_id, const char* plugin_name)
{
    qDebug("osc_global_send_add_plugin(%i, %s)", plugin_id, plugin_name);
    if (global_osc_data.target)
    {
        char target_path[strlen(global_osc_data.path)+12];
        strcpy(target_path, global_osc_data.path);
        strcat(target_path, "/add_plugin");
        lo_send(global_osc_data.target, target_path, "is", plugin_id, plugin_name);
    }
}

void osc_global_send_remove_plugin(int plugin_id)
{
    qDebug("osc_global_send_remove_plugin(%i)", plugin_id);
    if (global_osc_data.target)
    {
        char target_path[strlen(global_osc_data.path)+15];
        strcpy(target_path, global_osc_data.path);
        strcat(target_path, "/remove_plugin");
        lo_send(global_osc_data.target, target_path, "i", plugin_id);
    }
}

void osc_global_send_set_plugin_data(int plugin_id, int type, int category, int hints, const char* name, const char* label, const char* maker, const char* copyright, long unique_id)
{
    qDebug("osc_global_send_set_plugin_data(%i, %i, %i, %i, %s, %s, %s, %s, %li)", plugin_id, type, category, hints, name, label, maker, copyright, unique_id);
    if (global_osc_data.target)
    {
        char target_path[strlen(global_osc_data.path)+17];
        strcpy(target_path, global_osc_data.path);
        strcat(target_path, "/set_plugin_data");
        lo_send(global_osc_data.target, target_path, "iiiissssi", plugin_id, type, category, hints, name, label, maker, copyright, unique_id);
    }
}

void osc_global_send_set_plugin_ports(int plugin_id, int ains, int aouts, int mins, int mouts, int cins, int couts, int ctotals)
{
    qDebug("osc_global_send_set_plugin_ports(%i, %i, %i, %i, %i, %i, %i, %i)", plugin_id, ains, aouts, mins, mouts, cins, couts, ctotals);
    if (global_osc_data.target)
    {
        char target_path[strlen(global_osc_data.path)+18];
        strcpy(target_path, global_osc_data.path);
        strcat(target_path, "/set_plugin_ports");
        lo_send(global_osc_data.target, target_path, "iiiiiiii", plugin_id, ains, aouts, mins, mouts, cins, couts, ctotals);
    }
}

void osc_global_send_set_parameter_value(int plugin_id, int param_id, double value)
{
    qDebug("osc_global_send_set_parameter_value(%i, %i, %f)", plugin_id, param_id, value);
    if (global_osc_data.target)
    {
        char target_path[strlen(global_osc_data.path)+21];
        strcpy(target_path, global_osc_data.path);
        strcat(target_path, "/set_parameter_value");
        lo_send(global_osc_data.target, target_path, "iif", plugin_id, param_id, value);
    }
}

void osc_global_send_set_parameter_data(int plugin_id, int param_id, int ptype, int hints, const char* name, const char* label, double current)
{
    qDebug("osc_global_send_set_parameter_data(%i, %i, %i, %i, %s, %s, %f)", plugin_id, param_id, ptype, hints, name, label, current);
    if (global_osc_data.target)
    {
        char target_path[strlen(global_osc_data.path)+20];
        strcpy(target_path, global_osc_data.path);
        strcat(target_path, "/set_parameter_data");
        lo_send(global_osc_data.target, target_path, "iiiissf", plugin_id, param_id, ptype, hints, name, label, current);
    }
}

void osc_global_send_set_parameter_ranges(int plugin_id, int param_id, double x_min, double x_max, double x_def, double x_step, double x_step_small, double x_step_large)
{
    qDebug("osc_global_send_set_parameter_ranges(%i, %i, %f, %f, %f, %f, %f, %f)", plugin_id, param_id, x_min, x_max, x_def, x_step, x_step_small, x_step_large);
    if (global_osc_data.target)
    {
        char target_path[strlen(global_osc_data.path)+22];
        strcpy(target_path, global_osc_data.path);
        strcat(target_path, "/set_parameter_ranges");
        lo_send(global_osc_data.target, target_path, "iiffffff", plugin_id, param_id, x_min, x_max, x_def, x_step, x_step_small, x_step_large);
    }
}

void osc_global_send_set_parameter_midi_channel(int plugin_id, int parameter_id, int midi_channel)
{
    qDebug("osc_global_send_set_parameter_midi_channel(%i, %i, %i)", plugin_id, parameter_id, midi_channel);
    if (global_osc_data.target)
    {
        char target_path[strlen(global_osc_data.path)+28];
        strcpy(target_path, global_osc_data.path);
        strcat(target_path, "/set_parameter_midi_channel");
        lo_send(global_osc_data.target, target_path, "iii", plugin_id, parameter_id, midi_channel);
    }
}

void osc_global_send_set_parameter_midi_cc(int plugin_id, int parameter_id, int midi_cc)
{
    qDebug("osc_global_send_set_parameter_midi_cc(%i, %i, %i)", plugin_id, parameter_id, midi_cc);
    if (global_osc_data.target)
    {
        char target_path[strlen(global_osc_data.path)+23];
        strcpy(target_path, global_osc_data.path);
        strcat(target_path, "/set_parameter_midi_cc");
        lo_send(global_osc_data.target, target_path, "iii", plugin_id, parameter_id, midi_cc);
    }
}

void osc_global_send_set_default_value(int plugin_id, int param_id, double value)
{
    qDebug("osc_global_send_set_default_value(%i, %i, %f)", plugin_id, param_id, value);
    if (global_osc_data.target)
    {
        char target_path[strlen(global_osc_data.path)+19];
        strcpy(target_path, global_osc_data.path);
        strcat(target_path, "/set_default_value");
        lo_send(global_osc_data.target, target_path, "iif", plugin_id, param_id, value);
    }
}

void osc_global_send_set_input_peak_value(int plugin_id, int port_id, double value)
{
    qDebug("osc_global_send_set_input_peak_value(%i, %i, %f)", plugin_id, port_id, value);
    if (global_osc_data.target)
    {
        char target_path[strlen(global_osc_data.path)+22];
        strcpy(target_path, global_osc_data.path);
        strcat(target_path, "/set_input_peak_value");
        lo_send(global_osc_data.target, target_path, "iif", plugin_id, port_id, value);
    }
}

void osc_global_send_set_output_peak_value(int plugin_id, int port_id, double value)
{
    qDebug("osc_global_send_set_output_peak_value(%i, %i, %f)", plugin_id, port_id, value);
    if (global_osc_data.target)
    {
        char target_path[strlen(global_osc_data.path)+23];
        strcpy(target_path, global_osc_data.path);
        strcat(target_path, "/set_output_peak_value");
        lo_send(global_osc_data.target, target_path, "iif", plugin_id, port_id, value);
    }
}

void osc_global_send_set_program(int plugin_id, int program_id)
{
    qDebug("osc_global_send_set_program(%i, %i)", plugin_id, program_id);
    if (global_osc_data.target)
    {
        char target_path[strlen(global_osc_data.path)+13];
        strcpy(target_path, global_osc_data.path);
        strcat(target_path, "/set_program");
        lo_send(global_osc_data.target, target_path, "ii", plugin_id, program_id);
    }
}

void osc_global_send_set_program_count(int plugin_id, int program_count)
{
    qDebug("osc_global_send_set_program_count(%i, %i)", plugin_id, program_count);
    if (global_osc_data.target)
    {
        char target_path[strlen(global_osc_data.path)+19];
        strcpy(target_path, global_osc_data.path);
        strcat(target_path, "/set_program_count");
        lo_send(global_osc_data.target, target_path, "ii", plugin_id, program_count);
    }
}

void osc_global_send_set_program_name(int plugin_id, int program_id, const char* program_name)
{
    qDebug("osc_global_send_set_program_name(%i, %i, %s)", plugin_id, program_id, program_name);
    if (global_osc_data.target)
    {
        char target_path[strlen(global_osc_data.path)+18];
        strcpy(target_path, global_osc_data.path);
        strcat(target_path, "/set_program_name");
        lo_send(global_osc_data.target, target_path, "iis", plugin_id, program_id, program_name);
    }
}

void osc_global_send_set_midi_program(int plugin_id, int midi_program_id)
{
    qDebug("osc_global_send_set_midi_program(%i, %i)", plugin_id, midi_program_id);
    if (global_osc_data.target)
    {
        char target_path[strlen(global_osc_data.path)+18];
        strcpy(target_path, global_osc_data.path);
        strcat(target_path, "/set_midi_program");
        lo_send(global_osc_data.target, target_path, "ii", plugin_id, midi_program_id);
    }
}

void osc_global_send_set_midi_program_count(int plugin_id, int midi_program_count)
{
    qDebug("osc_global_send_set_midi_program_count(%i, %i)", plugin_id, midi_program_count);
    if (global_osc_data.target)
    {
        char target_path[strlen(global_osc_data.path)+24];
        strcpy(target_path, global_osc_data.path);
        strcat(target_path, "/set_midi_program_count");
        lo_send(global_osc_data.target, target_path, "ii", plugin_id, midi_program_count);
    }
}

void osc_global_send_set_midi_program_data(int plugin_id, int midi_program_id, int bank_id, int program_id, const char* midi_program_name)
{
    qDebug("osc_global_send_set_midi_program_data(%i, %i, %i, %i, %s)", plugin_id, midi_program_id, bank_id, program_id, midi_program_name);
    if (global_osc_data.target)
    {
        char target_path[strlen(global_osc_data.path)+23];
        strcpy(target_path, global_osc_data.path);
        strcat(target_path, "/set_midi_program_data");
        lo_send(global_osc_data.target, target_path, "iiiis", plugin_id, midi_program_id, bank_id, program_id, midi_program_name);
    }
}

void osc_global_send_note_on(int plugin_id, int note, int velo)
{
    qDebug("osc_global_send_note_on(%i, %i, %i)", plugin_id, note, velo);
    if (global_osc_data.target)
    {
        char target_path[strlen(global_osc_data.path)+9];
        strcpy(target_path, global_osc_data.path);
        strcat(target_path, "/note_on");
        lo_send(global_osc_data.target, target_path, "iii", plugin_id, note, velo);
    }
}

void osc_global_send_note_off(int plugin_id, int note)
{
    qDebug("osc_global_send_note_off(%i, %i)", plugin_id, note);
    if (global_osc_data.target)
    {
        char target_path[strlen(global_osc_data.path)+10];
        strcpy(target_path, global_osc_data.path);
        strcat(target_path, "/note_off");
        lo_send(global_osc_data.target, target_path, "ii", plugin_id, note);
    }
}

void osc_global_send_exit()
{
    qDebug("osc_global_send_exit()");
    if (global_osc_data.target)
    {
        char target_path[strlen(global_osc_data.path)+6];
        strcpy(target_path, global_osc_data.path);
        strcat(target_path, "/exit");
        lo_send(global_osc_data.target, target_path, "");
    }
}
