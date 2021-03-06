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

#include "carla_plugin.h"
#include "carla_vst_includes.h"

#ifndef __WINE__
#include <QtGui/QDialog>
#endif

CARLA_BACKEND_START_NAMESPACE

#if 0
} /* adjust editor indent */
#endif

/*!
 * @defgroup CarlaBackendVstPlugin Carla Backend VST Plugin
 *
 * The Carla Backend VST Plugin.
 * @{
 */

class VstPlugin : public CarlaPlugin
{
public:
    VstPlugin(unsigned short id) : CarlaPlugin(id)
    {
        qDebug("VstPlugin::VstPlugin()");

        m_type = PLUGIN_VST;

        effect = nullptr;
        events.numEvents = 0;
        events.reserved  = 0;

        gui.visible = false;
        gui.width  = 0;
        gui.height = 0;

        memset(midiEvents, 0, sizeof(VstMidiEvent)*MAX_MIDI_EVENTS*2);

        for (unsigned short i=0; i < MAX_MIDI_EVENTS*2; i++)
            events.data[i] = (VstEvent*)&midiEvents[i];

        // make plugin valid
        unique1 = unique2 = rand();
    }

    ~VstPlugin()
    {
        qDebug("VstPlugin::~VstPlugin()");

        // plugin is no longer valid
        unique1 = 0;
        unique2 = 1;

        if (effect)
        {
            // close UI
            if (m_hints & PLUGIN_HAS_GUI)
                effect->dispatcher(effect, effEditClose, 0, 0, nullptr, 0.0f);

            if (m_activeBefore)
            {
                effect->dispatcher(effect, effStopProcess, 0, 0, nullptr, 0.0f);
                effect->dispatcher(effect, effMainsChanged, 0, 0, nullptr, 0.0f);
            }

            effect->dispatcher(effect, effClose, 0, 0, nullptr, 0.0f);
        }
    }

    // -------------------------------------------------------------------
    // Information (base)

    PluginCategory category()
    {
        intptr_t VstCategory = effect->dispatcher(effect, effGetPlugCategory, 0, 0, nullptr, 0.0f);

        switch (VstCategory)
        {
        case kPlugCategSynth:
            return PLUGIN_CATEGORY_SYNTH;
        case kPlugCategAnalysis:
            return PLUGIN_CATEGORY_UTILITY;
        case kPlugCategMastering:
            return PLUGIN_CATEGORY_DYNAMICS;
        case kPlugCategRoomFx:
            return PLUGIN_CATEGORY_DELAY;
        case kPlugCategRestoration:
            return PLUGIN_CATEGORY_UTILITY;
        case kPlugCategGenerator:
            return PLUGIN_CATEGORY_SYNTH;
        }

        if (effect->flags & effFlagsIsSynth)
            return PLUGIN_CATEGORY_SYNTH;

        return get_category_from_name(m_name);
    }

    long uniqueId()
    {
        return effect->uniqueID;
    }

    // -------------------------------------------------------------------
    // Information (current data)

    int32_t chunkData(void** const dataPtr)
    {
        assert(dataPtr);
        return effect->dispatcher(effect, effGetChunk, 0 /* bank */, 0, dataPtr, 0.0f);
    }

    // -------------------------------------------------------------------
    // Information (per-plugin data)

    double getParameterValue(uint32_t parameterId)
    {
        assert(parameterId < param.count);
        return effect->getParameter(effect, parameterId);
    }

    void getLabel(char* const strBuf)
    {
        effect->dispatcher(effect, effGetProductString, 0, 0, strBuf, 0.0f);
    }

    void getMaker(char* const strBuf)
    {
        effect->dispatcher(effect, effGetVendorString, 0, 0, strBuf, 0.0f);
    }

    void getCopyright(char* const strBuf)
    {
        effect->dispatcher(effect, effGetVendorString, 0, 0, strBuf, 0.0f);
    }

    void getRealName(char* const strBuf)
    {
        effect->dispatcher(effect, effGetEffectName, 0, 0, strBuf, 0.0f);
    }

    void getParameterName(uint32_t parameterId, char* const strBuf)
    {
        assert(parameterId < param.count);
        effect->dispatcher(effect, effGetParamName, parameterId, 0, strBuf, 0.0f);
    }

    void getParameterUnit(uint32_t parameterId, char* const strBuf)
    {
        assert(parameterId < param.count);
        effect->dispatcher(effect, effGetParamLabel, parameterId, 0, strBuf, 0.0f);
    }

    void getParameterText(uint32_t parameterId, char* const strBuf)
    {
        assert(parameterId < param.count);
        effect->dispatcher(effect, effGetParamDisplay, parameterId, 0, strBuf, 0.0f);

        if (*strBuf == 0)
            sprintf(strBuf, "%f", getParameterValue(parameterId));
    }

    void getGuiInfo(GuiInfo* const info)
    {
        if (effect->flags & effFlagsHasEditor)
#ifdef Q_OS_WIN
            info->type = GUI_INTERNAL_HWND;
#else
            info->type = GUI_INTERNAL_X11;
#endif
        else
            info->type = GUI_NONE;
        info->resizable = false;
    }

    // -------------------------------------------------------------------
    // Set data (plugin-specific stuff)

    void setParameterValue(uint32_t parameterId, double value, bool sendGui, bool sendOsc, bool sendCallback)
    {
        assert(parameterId < param.count);
        effect->setParameter(effect, parameterId, fixParameterValue(value, param.ranges[parameterId]));
        CarlaPlugin::setParameterValue(parameterId, value, sendGui, sendOsc, sendCallback);
    }

    void setChunkData(const char* const stringData)
    {
        assert(stringData);

        static QByteArray chunk;
        chunk = QByteArray::fromBase64(stringData);

        if (CarlaEngine::isOffline())
        {
            carla_proc_lock();
            effect->dispatcher(effect, effSetChunk, 0 /* bank */, chunk.size(), chunk.data(), 0.0f);
            carla_proc_unlock();
        }
        else
        {
            const CarlaPluginScopedDisabler m(this);
            effect->dispatcher(effect, effSetChunk, 0 /* bank */, chunk.size(), chunk.data(), 0.0f);
        }
    }

    void setProgram(int32_t index, bool sendGui, bool sendOsc, bool sendCallback, bool block)
    {
        assert(index < (int32_t)prog.count);

        if (index >= 0)
        {
            if (CarlaEngine::isOffline())
            {
                if (block) carla_proc_lock();
                effect->dispatcher(effect, effSetProgram, 0, index, nullptr, 0.0f);
                if (block) carla_proc_unlock();
            }
            else
            {
                const CarlaPluginScopedDisabler m(this, block);
                effect->dispatcher(effect, effSetProgram, 0, index, nullptr, 0.0f);
            }
        }

        CarlaPlugin::setProgram(index, sendGui, sendOsc, sendCallback, block);
    }

    // -------------------------------------------------------------------
    // Set gui stuff

    void setGuiData(int data, GuiDataHandle handle)
    {
#ifdef __WINE__
        if (effect->dispatcher(effect, effEditOpen, 0, data, handle, 0.0f) == 1)
#else
        QDialog* dialog = handle;
        if (effect->dispatcher(effect, effEditOpen, 0, data, (void*)dialog->winId(), 0.0f) == 1)
#endif
        {
            ERect* vst_rect;

            if (effect->dispatcher(effect, effEditGetRect, 0, 0, &vst_rect, 0.0f))
            {
                int width  = vst_rect->right  - vst_rect->left;
                int height = vst_rect->bottom - vst_rect->top;

                if (width <= 0 || height <= 0)
                {
                    qCritical("Failed to get proper Plugin Window size");
                    return;
                }

                gui.width  = width;
                gui.height = height;
            }
            else
                qCritical("Failed to get Plugin Window size");
        }
        else
        {
            // failed to open UI
            m_hints &= ~PLUGIN_HAS_GUI;
            callback_action(CALLBACK_SHOW_GUI, m_id, -1, 0, 0.0);

            effect->dispatcher(effect, effEditClose, 0, 0, nullptr, 0.0f);
        }
    }

    void showGui(bool yesNo)
    {
        gui.visible = yesNo;

        if (gui.visible && gui.width > 0 && gui.height > 0)
            callback_action(CALLBACK_RESIZE_GUI, m_id, gui.width, gui.height, 0.0);
    }

    void idleGui()
    {
        //effect->dispatcher(effect, effIdle, 0, 0, nullptr, 0.0f);

        // FIXME
        if (gui.visible)
            effect->dispatcher(effect, effEditIdle, 0, 0, nullptr, 0.0f);
    }

    // -------------------------------------------------------------------
    // Plugin state

    void reload()
    {
        qDebug("VstPlugin::reload() - start");

        // Safely disable plugin for reload
        const CarlaPluginScopedDisabler m(this);

        if (x_client->isActive())
            x_client->deactivate();

        // Remove client ports
        removeClientPorts();

        // Delete old data
        deleteBuffers();

        uint32_t ains, aouts, mins, mouts, params, j;
        ains = aouts = mins = mouts = params = 0;

        ains   = effect->numInputs;
        aouts  = effect->numOutputs;
        params = effect->numParams;

        if (VstPluginCanDo(effect, "receiveVstEvents") || VstPluginCanDo(effect, "receiveVstMidiEvent") || (effect->flags & effFlagsIsSynth) > 0)
            mins = 1;

        if (VstPluginCanDo(effect, "sendVstEvents") || VstPluginCanDo(effect, "sendVstMidiEvent"))
            mouts = 1;

        if (ains > 0)
        {
            ain.ports    = new CarlaEngineAudioPort*[ains];
            ain.rindexes = new uint32_t[ains];
        }

        if (aouts > 0)
        {
            aout.ports    = new CarlaEngineAudioPort*[aouts];
            aout.rindexes = new uint32_t[aouts];
        }

        if (params > 0)
        {
            param.data    = new ParameterData[params];
            param.ranges  = new ParameterRanges[params];
        }

        const int portNameSize = CarlaEngine::maxPortNameSize() - 1;
        char portName[portNameSize];
        bool needsCin = (aouts > 0 || params > 0);

        for (j=0; j<ains; j++)
        {
#ifndef BUILD_BRIDGE
            if (carla_options.process_mode != PROCESS_MODE_MULTIPLE_CLIENTS)
                sprintf(portName, "%s:input_%02i", m_name, j+1);
            else
#endif
                sprintf(portName, "input_%02i", j+1);

            ain.ports[j]    = (CarlaEngineAudioPort*)x_client->addPort(portName, CarlaEnginePortTypeAudio, true);
            ain.rindexes[j] = j;
        }

        for (j=0; j<aouts; j++)
        {
#ifndef BUILD_BRIDGE
            if (carla_options.process_mode != PROCESS_MODE_MULTIPLE_CLIENTS)
                sprintf(portName, "%s:output_%02i", m_name, j+1);
            else
#endif
                sprintf(portName, "output_%02i", j+1);

            aout.ports[j]    = (CarlaEngineAudioPort*)x_client->addPort(portName, CarlaEnginePortTypeAudio, false);
            aout.rindexes[j] = j;
        }

        for (j=0; j<params; j++)
        {
            param.data[j].type   = PARAMETER_INPUT;
            param.data[j].index  = j;
            param.data[j].rindex = j;
            param.data[j].hints  = 0;
            param.data[j].midiChannel = 0;
            param.data[j].midiCC = -1;

            double min, max, def, step, step_small, step_large;

            VstParameterProperties prop;
            prop.flags = 0;

            if (effect->dispatcher(effect, effGetParameterProperties, j, 0, &prop, 0))
            {
                if (prop.flags & kVstParameterUsesIntegerMinMax)
                {
                    min = prop.minInteger;
                    max = prop.maxInteger;
                }
                else
                {
                    min = 0.0;
                    max = 1.0;
                }

                if (min > max)
                    max = min;
                else if (max < min)
                    min = max;

                if (max - min == 0.0)
                {
                    qWarning("Broken plugin parameter: max - min == 0");
                    max = min + 0.1;
                }

                if (prop.flags & kVstParameterIsSwitch)
                {
                    step = max - min;
                    step_small = step;
                    step_large = step;
                    param.data[j].hints |= PARAMETER_IS_BOOLEAN;
                }
                else if (prop.flags & kVstParameterUsesIntStep)
                {
                    step = prop.stepInteger;
                    step_small = prop.stepInteger;
                    step_large = prop.largeStepInteger;
                    param.data[j].hints |= PARAMETER_IS_INTEGER;
                }
                else if (prop.flags & kVstParameterUsesFloatStep)
                {
                    step = prop.stepFloat;
                    step_small = prop.smallStepFloat;
                    step_large = prop.largeStepFloat;
                }
                else
                {
                    double range = max - min;
                    step = range/100.0;
                    step_small = range/1000.0;
                    step_large = range/10.0;
                }

                if (prop.flags & kVstParameterCanRamp)
                    param.data[j].hints |= PARAMETER_IS_LOGARITHMIC;
            }
            else
            {
                min = 0.0;
                max = 1.0;
                step = 0.001;
                step_small = 0.0001;
                step_large = 0.1;
            }

            // no such thing as VST default parameters
            def = effect->getParameter(effect, j);

            if (def < min)
                def = min;
            else if (def > max)
                def = max;

            param.ranges[j].min = min;
            param.ranges[j].max = max;
            param.ranges[j].def = def;
            param.ranges[j].step = step;
            param.ranges[j].stepSmall = step_small;
            param.ranges[j].stepLarge = step_large;

            param.data[j].hints |= PARAMETER_IS_ENABLED;
#ifndef BUILD_BRIDGE
            param.data[j].hints |= PARAMETER_USES_CUSTOM_TEXT;
#endif

            if (effect->dispatcher(effect, effCanBeAutomated, j, 0, nullptr, 0.0f) != 0)
                param.data[j].hints |= PARAMETER_IS_AUTOMABLE;
        }

        if (needsCin)
        {
#ifndef BUILD_BRIDGE
            if (carla_options.process_mode != PROCESS_MODE_MULTIPLE_CLIENTS)
            {
                strcpy(portName, m_name);
                strcat(portName, ":control-in");
            }
            else
#endif
                strcpy(portName, "control-in");

            param.portCin = (CarlaEngineControlPort*)x_client->addPort(portName, CarlaEnginePortTypeControl, true);
        }

        if (mins == 1)
        {
#ifndef BUILD_BRIDGE
            if (carla_options.process_mode != PROCESS_MODE_MULTIPLE_CLIENTS)
            {
                strcpy(portName, m_name);
                strcat(portName, ":midi-in");
            }
            else
#endif
                strcpy(portName, "midi-in");

            midi.portMin = (CarlaEngineMidiPort*)x_client->addPort(portName, CarlaEnginePortTypeMIDI, true);
        }

        if (mouts == 1)
        {
#ifndef BUILD_BRIDGE
            if (carla_options.process_mode != PROCESS_MODE_MULTIPLE_CLIENTS)
            {
                strcpy(portName, m_name);
                strcat(portName, ":midi-out");
            }
            else
#endif
                strcpy(portName, "midi-out");

            midi.portMout = (CarlaEngineMidiPort*)x_client->addPort(portName, CarlaEnginePortTypeMIDI, false);
        }

        ain.count   = ains;
        aout.count  = aouts;
        param.count = params;

        // plugin checks
        m_hints &= ~(PLUGIN_IS_SYNTH | PLUGIN_USES_CHUNKS | PLUGIN_CAN_DRYWET | PLUGIN_CAN_VOLUME | PLUGIN_CAN_BALANCE);

        intptr_t VstCategory = effect->dispatcher(effect, effGetPlugCategory, 0, 0, nullptr, 0.0f);

        if (VstCategory == kPlugCategSynth || VstCategory == kPlugCategGenerator)
            m_hints |= PLUGIN_IS_SYNTH;

        if (effect->flags & effFlagsProgramChunks)
            m_hints |= PLUGIN_USES_CHUNKS;

        if (aouts > 0 && (ains == aouts || ains == 1))
            m_hints |= PLUGIN_CAN_DRYWET;

        if (aouts > 0)
            m_hints |= PLUGIN_CAN_VOLUME;

        if (aouts >= 2 && aouts%2 == 0)
            m_hints |= PLUGIN_CAN_BALANCE;

        reloadPrograms(true);

        x_client->activate();

        qDebug("VstPlugin::reload() - end");
    }

    void reloadPrograms(bool init)
    {
        qDebug("VstPlugin::reloadPrograms(%s)", bool2str(init));
        uint32_t i, oldCount = prog.count;

        // Delete old programs
        if (prog.count > 0)
        {
            for (uint32_t i=0; i < prog.count; i++)
                free((void*)prog.names[i]);

            delete[] prog.names;
        }

        prog.count = 0;
        prog.names = nullptr;

        // Query new programs
        prog.count = effect->numPrograms;

        if (prog.count > 0)
            prog.names = new const char* [prog.count];

        // Update names
        for (i=0; i < prog.count; i++)
        {
            char strBuf[STR_MAX] = { 0 };
            if (effect->dispatcher(effect, effGetProgramNameIndexed, i, 0, strBuf, 0.0f) != 1)
            {
                // program will be [re-]changed later
                effect->dispatcher(effect, effSetProgram, 0, i, nullptr, 0.0f);
                effect->dispatcher(effect, effGetProgramName, 0, 0, strBuf, 0.0f);
            }
            prog.names[i] = strdup(strBuf);
        }

#ifndef BUILD_BRIDGE
        // Update OSC Names
        osc_global_send_set_program_count(m_id, prog.count);

        for (i=0; i < prog.count; i++)
            osc_global_send_set_program_name(m_id, i, prog.names[i]);

        callback_action(CALLBACK_RELOAD_PROGRAMS, m_id, 0, 0, 0.0);
#endif

        if (init)
        {
            if (prog.count > 0)
                setProgram(0, false, false, false, true);
        }
        else
        {
            callback_action(CALLBACK_UPDATE, m_id, 0, 0, 0.0);

            // Check if current program is invalid
            bool programChanged = false;

            if (prog.count == oldCount+1)
            {
                // one program added, probably created by user
                prog.current = oldCount;
                programChanged = true;
            }
            else if (prog.current >= (int32_t)prog.count)
            {
                // current program > count
                prog.current = 0;
                programChanged = true;
            }
            else if (prog.current < 0 && prog.count > 0)
            {
                // programs exist now, but not before
                prog.current = 0;
                programChanged = true;
            }
            else if (prog.current >= 0 && prog.count == 0)
            {
                // programs existed before, but not anymore
                prog.current = -1;
                programChanged = true;
            }

            if (programChanged)
            {
                setProgram(prog.current, true, true, true, true);
            }
            else
            {
                // Program was changed during update, re-set it
                if (prog.current >= 0)
                    effect->dispatcher(effect, effSetProgram, 0, prog.current, nullptr, 0.0f);
            }
        }
    }

    // -------------------------------------------------------------------
    // Plugin processing

    void process(float** inBuffer, float** outBuffer, uint32_t frames, uint32_t framesOffset)
    {
        uint32_t i, k;
        uint32_t midiEventCount = 0;

        double ains_peak_tmp[2]  = { 0.0 };
        double aouts_peak_tmp[2] = { 0.0 };

        // reset MIDI
        events.numEvents = 0;
        midiEvents[0].type = 0;

        CARLA_PROCESS_CONTINUE_CHECK;

        // --------------------------------------------------------------------------------------------------------
        // Input VU

        if (ain.count > 0)
        {
            if (ain.count == 1)
            {
                for (k=0; k < frames; k++)
                {
                    if (abs(inBuffer[0][k]) > ains_peak_tmp[0])
                        ains_peak_tmp[0] = abs(inBuffer[0][k]);
                }
            }
            else if (ain.count >= 1)
            {
                for (k=0; k < frames; k++)
                {
                    if (abs(inBuffer[0][k]) > ains_peak_tmp[0])
                        ains_peak_tmp[0] = abs(inBuffer[0][k]);

                    if (abs(inBuffer[1][k]) > ains_peak_tmp[1])
                        ains_peak_tmp[1] = abs(inBuffer[1][k]);
                }
            }
        }

        CARLA_PROCESS_CONTINUE_CHECK;

        // --------------------------------------------------------------------------------------------------------
        // Parameters Input [Automation]

        if (param.portCin && m_active && m_activeBefore)
        {
            bool allNotesOffSent = false;
            void* cinBuffer = param.portCin->getBuffer();

            const CarlaEngineControlEvent* cinEvent;
            uint32_t time, nEvents = param.portCin->getEventCount(cinBuffer);

            for (i=0; i < nEvents; i++)
            {
                cinEvent = param.portCin->getEvent(cinBuffer, i);

                if (! cinEvent)
                    continue;

                time = cinEvent->time - framesOffset;

                if (time >= frames)
                    continue;

                // Control change
                switch (cinEvent->type)
                {
                case CarlaEngineEventControlChange:
                {
                    double value;

                    // Control backend stuff
                    if (cinEvent->channel == cin_channel)
                    {
                        if (MIDI_IS_CONTROL_BREATH_CONTROLLER(cinEvent->controller) && (m_hints & PLUGIN_CAN_DRYWET) > 0)
                        {
                            value = cinEvent->value;
                            setDryWet(value, false, false);
                            postponeEvent(PluginPostEventParameterChange, PARAMETER_DRYWET, value);
                            continue;
                        }

                        if (MIDI_IS_CONTROL_CHANNEL_VOLUME(cinEvent->controller) && (m_hints & PLUGIN_CAN_VOLUME) > 0)
                        {
                            value = cinEvent->value*127/100;
                            setVolume(value, false, false);
                            postponeEvent(PluginPostEventParameterChange, PARAMETER_VOLUME, value);
                            continue;
                        }

                        if (MIDI_IS_CONTROL_BALANCE(cinEvent->controller) && (m_hints & PLUGIN_CAN_BALANCE) > 0)
                        {
                            double left, right;
                            value = cinEvent->value/0.5 - 1.0;

                            if (value < 0)
                            {
                                left  = -1.0;
                                right = (value*2)+1.0;
                            }
                            else if (value > 0)
                            {
                                left  = (value*2)-1.0;
                                right = 1.0;
                            }
                            else
                            {
                                left  = -1.0;
                                right = 1.0;
                            }

                            setBalanceLeft(left, false, false);
                            setBalanceRight(right, false, false);
                            postponeEvent(PluginPostEventParameterChange, PARAMETER_BALANCE_LEFT, left);
                            postponeEvent(PluginPostEventParameterChange, PARAMETER_BALANCE_RIGHT, right);
                            continue;
                        }
                    }

                    // Control plugin parameters
                    for (k=0; k < param.count; k++)
                    {
                        if (param.data[k].midiChannel != cinEvent->channel)
                            continue;
                        if (param.data[k].midiCC != cinEvent->controller)
                            continue;
                        if (param.data[k].type != PARAMETER_INPUT)
                            continue;

                        if (param.data[k].hints & PARAMETER_IS_AUTOMABLE)
                        {
                            if (param.data[k].hints & PARAMETER_IS_BOOLEAN)
                            {
                                value = cinEvent->value < 0.5 ? param.ranges[k].min : param.ranges[k].max;
                            }
                            else
                            {
                                value = cinEvent->value * (param.ranges[k].max - param.ranges[k].min) + param.ranges[k].min;

                                if (param.data[k].hints & PARAMETER_IS_INTEGER)
                                    value = rint(value);
                            }

                            setParameterValue(k, value, false, false, false);
                            postponeEvent(PluginPostEventParameterChange, k, value);
                        }
                    }

                    break;
                }

                case CarlaEngineEventMidiBankChange:
                    break;

                case CarlaEngineEventMidiProgramChange:
                    if (cinEvent->channel == cin_channel)
                    {
                        uint32_t progId = rint(cinEvent->value);

                        if (progId < prog.count)
                        {
                            setProgram(progId, false, false, false, false);
                            postponeEvent(PluginPostEventMidiProgramChange, progId, 0.0);
                        }
                    }
                    break;

                case CarlaEngineEventAllSoundOff:
                    if (cinEvent->channel == cin_channel)
                    {
                        if (midi.portMin && ! allNotesOffSent)
                            sendMidiAllNotesOff();

                        effect->dispatcher(effect, effStopProcess, 0, 0, nullptr, 0.0f);
                        effect->dispatcher(effect, effMainsChanged, 0, 0, nullptr, 0.0f);

                        effect->dispatcher(effect, effMainsChanged, 0, 1, nullptr, 0.0f);
                        effect->dispatcher(effect, effStartProcess, 0, 0, nullptr, 0.0f);

                        allNotesOffSent = true;
                    }
                    break;

                case CarlaEngineEventAllNotesOff:
                    if (cinEvent->channel == cin_channel)
                    {
                        if (midi.portMin && ! allNotesOffSent)
                            sendMidiAllNotesOff();

                        allNotesOffSent = true;
                    }
                    break;
                }
            }
        } // End of Parameters Input

        CARLA_PROCESS_CONTINUE_CHECK;

        // --------------------------------------------------------------------------------------------------------
        // MIDI Input (External)

        if (midi.portMin && cin_channel >= 0 && cin_channel < 16 && m_active && m_activeBefore)
        {
            carla_midi_lock();

            for (i=0; i < MAX_MIDI_EVENTS && midiEventCount < MAX_MIDI_EVENTS; i++)
            {
                if (! extMidiNotes[i].valid)
                    break;

                VstMidiEvent* const midiEvent = &midiEvents[midiEventCount];
                memset(midiEvent, 0, sizeof(VstMidiEvent));

                midiEvent->type = kVstMidiType;
                midiEvent->byteSize = sizeof(VstMidiEvent);
                midiEvent->deltaFrames = framesOffset;
                midiEvent->midiData[0] = cin_channel + extMidiNotes[i].velo ? MIDI_STATUS_NOTE_ON : MIDI_STATUS_NOTE_OFF;
                midiEvent->midiData[1] = extMidiNotes[i].note;
                midiEvent->midiData[2] = extMidiNotes[i].velo;

                extMidiNotes[i].valid = false;
                midiEventCount += 1;
            }

            carla_midi_unlock();

        } // End of MIDI Input (External)

        CARLA_PROCESS_CONTINUE_CHECK;

        // --------------------------------------------------------------------------------------------------------
        // MIDI Input (System)

        if (midi.portMin && m_active && m_activeBefore)
        {
            void* minBuffer = midi.portMin->getBuffer();

            const CarlaEngineMidiEvent* minEvent;
            uint32_t time, nEvents = midi.portMin->getEventCount(minBuffer);

            for (i=0; i < nEvents && midiEventCount < MAX_MIDI_EVENTS; i++)
            {
                minEvent = midi.portMin->getEvent(minBuffer, i);

                if (! minEvent)
                    continue;

                time = minEvent->time - framesOffset;

                if (time >= frames)
                    continue;

                uint8_t status  = minEvent->data[0];
                uint8_t channel = status & 0x0F;

                // Fix bad note-off
                if (MIDI_IS_STATUS_NOTE_ON(status) && minEvent->data[2] == 0)
                    status -= 0x10;

                VstMidiEvent* const midiEvent = &midiEvents[midiEventCount];
                memset(midiEvent, 0, sizeof(VstMidiEvent));

                midiEvent->type = kVstMidiType;
                midiEvent->byteSize = sizeof(VstMidiEvent);
                midiEvent->deltaFrames = minEvent->time;

                if (MIDI_IS_STATUS_NOTE_OFF(status))
                {
                    uint8_t note = minEvent->data[1];

                    midiEvent->midiData[0] = status;
                    midiEvent->midiData[1] = note;

                    if (channel == cin_channel)
                        postponeEvent(PluginPostEventNoteOff, note, 0.0);
                }
                else if (MIDI_IS_STATUS_NOTE_ON(status))
                {
                    uint8_t note = minEvent->data[1];
                    uint8_t velo = minEvent->data[2];

                    midiEvent->midiData[0] = status;
                    midiEvent->midiData[1] = note;
                    midiEvent->midiData[2] = velo;

                    if (channel == cin_channel)
                        postponeEvent(PluginPostEventNoteOn, note, velo);
                }
                else if (MIDI_IS_STATUS_POLYPHONIC_AFTERTOUCH(status))
                {
                    uint8_t note     = minEvent->data[1];
                    uint8_t pressure = minEvent->data[2];

                    midiEvent->midiData[0] = status;
                    midiEvent->midiData[1] = note;
                    midiEvent->midiData[2] = pressure;
                }
                else if (MIDI_IS_STATUS_AFTERTOUCH(status))
                {
                    uint8_t pressure = minEvent->data[1];

                    midiEvent->midiData[0] = status;
                    midiEvent->midiData[1] = pressure;
                }
                else if (MIDI_IS_STATUS_PITCH_WHEEL_CONTROL(status))
                {
                    uint8_t lsb = minEvent->data[1];
                    uint8_t msb = minEvent->data[2];

                    midiEvent->midiData[0] = status;
                    midiEvent->midiData[1] = lsb;
                    midiEvent->midiData[2] = msb;
                }
                else
                    continue;

                midiEventCount += 1;
            }
        } // End of MIDI Input (System)

        CARLA_PROCESS_CONTINUE_CHECK;

        // --------------------------------------------------------------------------------------------------------
        // Plugin processing

        if (m_active)
        {
            if (! m_activeBefore)
            {
                // TODO - send sound-off notes-off events here

                effect->dispatcher(effect, effMainsChanged, 0, 1, nullptr, 0.0f);
                effect->dispatcher(effect, effStartProcess, 0, 0, nullptr, 0.0f);
            }

            if (midiEventCount > 0)
            {
                events.numEvents = midiEventCount;
                events.reserved  = 0;
                effect->dispatcher(effect, effProcessEvents, 0, 0, &events, 0.0f);
            }

            // FIXME - make this a global option
            // don't process if not needed
            //if ((effect->flags & effFlagsNoSoundInStop) > 0 && ains_peak_tmp[0] == 0 && ains_peak_tmp[1] == 0 && midi_event_count == 0 && ! midi.port_mout)
            //{
            if (effect->flags & effFlagsCanReplacing)
            {
                effect->processReplacing(effect, inBuffer, outBuffer, frames);
            }
            else
            {
                for (i=0; i < aout.count; i++)
                    memset(outBuffer[i], 0, sizeof(float)*frames);

#if ! VST_FORCE_DEPRECATED
                effect->process(effect, inBuffer, outBuffer, frames);
#endif
            }
            //}
        }
        else
        {
            if (m_activeBefore)
            {
                effect->dispatcher(effect, effStopProcess, 0, 0, nullptr, 0.0f);
                effect->dispatcher(effect, effMainsChanged, 0, 0, nullptr, 0.0f);
            }
        }

        CARLA_PROCESS_CONTINUE_CHECK;

        // --------------------------------------------------------------------------------------------------------
        // Post-processing (dry/wet, volume and balance)

        if (m_active)
        {
            bool do_drywet  = (m_hints & PLUGIN_CAN_DRYWET) > 0 && x_drywet != 1.0;
            bool do_volume  = (m_hints & PLUGIN_CAN_VOLUME) > 0 && x_vol != 1.0;
            bool do_balance = (m_hints & PLUGIN_CAN_BALANCE) > 0 && (x_bal_left != -1.0 || x_bal_right != 1.0);

            double bal_rangeL, bal_rangeR;
            float oldBufLeft[do_balance ? frames : 0];

            for (i=0; i < aout.count; i++)
            {
                // Dry/Wet and Volume
                if (do_drywet || do_volume)
                {
                    for (k=0; k < frames; k++)
                    {
                        if (do_drywet)
                        {
                            if (aout.count == 1)
                                outBuffer[i][k] = (outBuffer[i][k]*x_drywet)+(inBuffer[0][k]*(1.0-x_drywet));
                            else
                                outBuffer[i][k] = (outBuffer[i][k]*x_drywet)+(inBuffer[i][k]*(1.0-x_drywet));
                        }

                        if (do_volume)
                            outBuffer[i][k] *= x_vol;
                    }
                }

                // Balance
                if (do_balance)
                {
                    if (i%2 == 0)
                        memcpy(&oldBufLeft, outBuffer[i], sizeof(float)*frames);

                    bal_rangeL = (x_bal_left+1.0)/2;
                    bal_rangeR = (x_bal_right+1.0)/2;

                    for (k=0; k < frames; k++)
                    {
                        if (i%2 == 0)
                        {
                            // left output
                            outBuffer[i][k]  = oldBufLeft[k]*(1.0-bal_rangeL);
                            outBuffer[i][k] += outBuffer[i+1][k]*(1.0-bal_rangeR);
                        }
                        else
                        {
                            // right
                            outBuffer[i][k]  = outBuffer[i][k]*bal_rangeR;
                            outBuffer[i][k] += oldBufLeft[k]*bal_rangeL;
                        }
                    }
                }

                // Output VU
                for (k=0; i < 2 && k < frames; k++)
                {
                    if (abs(outBuffer[i][k]) > aouts_peak_tmp[i])
                        aouts_peak_tmp[i] = abs(outBuffer[i][k]);
                }
            }
        }
        else
        {
            // disable any output sound if not active
            for (i=0; i < aout.count; i++)
                memset(outBuffer[i], 0.0f, sizeof(float)*frames);

            aouts_peak_tmp[0] = 0.0;
            aouts_peak_tmp[1] = 0.0;

        } // End of Post-processing

        CARLA_PROCESS_CONTINUE_CHECK;

        // --------------------------------------------------------------------------------------------------------
        // MIDI Output

        if (midi.portMout && m_active)
        {
            uint8_t data[4];
            void* moutBuffer = midi.portMout->getBuffer();

            if (framesOffset == 0 || ! m_activeBefore)
                midi.portMout->initBuffer(moutBuffer);

            for (i = events.numEvents; i < MAX_MIDI_EVENTS*2; i++)
            {
                if (midiEvents[i].type != kVstMidiType)
                    break;

                data[0] = midiEvents[i].midiData[0];
                data[1] = midiEvents[i].midiData[1];
                data[2] = midiEvents[i].midiData[2];
                data[3] = midiEvents[i].midiData[3];

                // Fix bad note-off
                if (MIDI_IS_STATUS_NOTE_ON(data[0]) && data[2] == 0)
                    data[0] -= 0x10;

                midi.portMout->writeEvent(moutBuffer, midiEvents[i].deltaFrames, data, 3);
            }
        } // End of MIDI Output

        CARLA_PROCESS_CONTINUE_CHECK;

        // --------------------------------------------------------------------------------------------------------
        // Peak Values

        ains_peak[(m_id*2)+0]  = ains_peak_tmp[0];
        ains_peak[(m_id*2)+1]  = ains_peak_tmp[1];
        aouts_peak[(m_id*2)+0] = aouts_peak_tmp[0];
        aouts_peak[(m_id*2)+1] = aouts_peak_tmp[1];

        m_activeBefore = m_active;
    }

    void bufferSizeChanged(uint32_t newBufferSize)
    {
        if (m_active)
        {
            effect->dispatcher(effect, effStopProcess, 0, 0, nullptr, 0.0f);
            effect->dispatcher(effect, effMainsChanged, 0, 0, nullptr, 0.0f);
        }

#if ! VST_FORCE_DEPRECATED
        effect->dispatcher(effect, effSetBlockSizeAndSampleRate, 0, newBufferSize, nullptr, get_sample_rate());
#endif
        effect->dispatcher(effect, effSetBlockSize, 0, newBufferSize, nullptr, 0.0f);

        if (m_active)
        {
            effect->dispatcher(effect, effMainsChanged, 0, 1, nullptr, 0.0f);
            effect->dispatcher(effect, effStartProcess, 0, 0, nullptr, 0.0f);
        }
    }

    // -------------------------------------------------------------------

    bool init(const char* filename, const char* label)
    {
        // ---------------------------------------------------------------
        // open DLL

        if (! libOpen(filename))
        {
            set_last_error(libError(filename));
            return false;
        }

        // ---------------------------------------------------------------
        // get DLL main entry

        VST_Function vstfn = (VST_Function)libSymbol("VSTPluginMain");

        if (! vstfn)
        {
            vstfn = (VST_Function)libSymbol("main");

            if (! vstfn)
            {
                set_last_error("Could not find the VST main entry in the plugin library");
                return false;
            }
        }

        // ---------------------------------------------------------------
        // initialize plugin

        effect = vstfn(VstHostCallback);

        if (! effect || effect->magic != kEffectMagic)
        {
            set_last_error("Plugin failed to initialize");
            return false;
        }

        // ---------------------------------------------------------------
        // get info

        m_filename = strdup(filename);

        char strBuf[STR_MAX] = { 0 };
        effect->dispatcher(effect, effGetEffectName, 0, 0, strBuf, 0.0f);

        if (strBuf[0] != 0)
            m_name = get_unique_name(strBuf);
        else
            m_name = get_unique_name(label);

        // ---------------------------------------------------------------
        // initialize VST stuff

        effect->dispatcher(effect, effOpen, 0, 0, nullptr, 0.0f);
#if ! VST_FORCE_DEPRECATED
        effect->dispatcher(effect, effSetBlockSizeAndSampleRate, 0, get_buffer_size(), nullptr, get_sample_rate());
#endif
        effect->dispatcher(effect, effSetSampleRate, 0, 0, nullptr, get_sample_rate());
        effect->dispatcher(effect, effSetBlockSize, 0, get_buffer_size(), nullptr, 0.0f);
        effect->dispatcher(effect, effSetProcessPrecision, 0, kVstProcessPrecision32, nullptr, 0.0f);

#ifdef VESTIGE_HEADER
        effect->ptr1 = this;
#else
        effect->resvd1 = (intptr_t)this;
#endif

        // ---------------------------------------------------------------
        // register client

        x_client = new CarlaEngineClient(this);

        if (! x_client->isOk())
        {
            set_last_error("Failed to register plugin client");
            return false;
        }

        // ---------------------------------------------------------------
        // gui stuff

        if (effect->flags & effFlagsHasEditor)
            m_hints |= PLUGIN_HAS_GUI;

        return true;
    }

    // -------------------------------------------------------------------

    static intptr_t VstHostCallback(AEffect* effect, int32_t opcode, int32_t index, intptr_t value, void* ptr, float opt)
    {
#ifdef DEBUG
        //qDebug("VstHostCallback(%p, opcode: %s, index: %i, value: " P_INTPTR ", opt: %f", effect, VstOpcode2str(opcode), index, value, opt);
#endif

        // Check if 'resvd1' points to this plugin
        VstPlugin* self = nullptr;

#ifdef VESTIGE_HEADER
        if (effect && effect->ptr1)
        {
            self = (VstPlugin*)effect->ptr1;
#else
        if (effect && effect->resvd1)
        {
            self = (VstPlugin*)get_pointer(effect->resvd1);
#endif
            if (self->unique1 != self->unique2)
                self = nullptr;
        }

        switch (opcode)
        {
        case audioMasterAutomate:
            if (self)
            {
                if (CarlaEngine::isOnAudioThread())
                {
                    self->setParameterValue(index, opt, false, false, false);
                    self->postponeEvent(PluginPostEventParameterChange, index, opt);
                }
                else
                    self->setParameterValue(index, opt, false, true, true);
            }
            break;

        case audioMasterVersion:
            return kVstVersion;

        case audioMasterCurrentId:
            return 0; // TODO

        case audioMasterIdle:
            if (effect)
                effect->dispatcher(effect, effEditIdle, 0, 0, nullptr, 0.0f);
            break;

#if ! VST_FORCE_DEPRECATED
        case audioMasterWantMidi: // TODO
            // Deprecated in VST SDK 2.4
#if 0
            if (plugin && plugin->jack_client && plugin->min.count == 0)
            {
                i = plugin->id;
                bool unlock = carla_proc_trylock();
                plugin->id = -1;
                if (unlock) carla_proc_unlock();

                const int port_name_size = jack_port_name_size();
                char port_name[port_name_size];

#ifndef BUILD_BRIDGE
                if (carla_options.global_jack_client)
                {
                    strncpy(port_name, plugin->name, (port_name_size/2)-2);
                    strcat(port_name, ":midi-in");
                }
                else
#endif
                    strcpy(port_name, "midi-in");

                plugin->min.count    = 1;
                plugin->min.ports    = new jack_port_t*[1];
                plugin->min.ports[0] = jack_port_register(plugin->jack_client, port_name, JACK_DEFAULT_MIDI_TYPE, JackPortIsInput, 0);

                plugin->id = i;
#ifndef BRIDGE_WINVST
                callback_action(CALLBACK_RELOAD_INFO, plugin->id, 0, 0, 0.0f);
#endif
            }
#endif
            break;
#endif

#if 0
        case audioMasterGetTime:
        {
            static VstTimeInfo_R timeInfo;
            memset(&timeInfo, 0, sizeof(VstTimeInfo_R));

            jack_position_t* jack_pos;
            bool playing = carla_jack_transport_query(&jack_pos);

            timeInfo.flags |= kVstTransportChanged;

            if (playing)
                timeInfo.flags |= kVstTransportPlaying;

            if (jack_pos->unique_1 == jack_pos->unique_2)
            {
                timeInfo.samplePos  = jack_pos->frame;
                timeInfo.sampleRate = jack_pos->frame_rate;

                if (jack_pos->valid & JackPositionBBT)
                {
                    // Tempo
                    timeInfo.tempo  = jack_pos->beats_per_minute;
                    timeInfo.flags |= kVstTempoValid;

                    // Time Signature
                    timeInfo.timeSigNumerator   = jack_pos->beats_per_bar;
                    timeInfo.timeSigDenominator = jack_pos->beat_type;
                    timeInfo.flags |= kVstTimeSigValid;

                    // Position
                    double dPos = timeInfo.samplePos / timeInfo.sampleRate;
                    timeInfo.barStartPos = 0;
                    timeInfo.nanoSeconds = dPos * 1000.0;
                    timeInfo.ppqPos = dPos * timeInfo.tempo / 60.0;
                    timeInfo.flags |= kVstBarsValid|kVstNanosValid|kVstPpqPosValid;
                }
            }
            else
                timeInfo.sampleRate = get_sample_rate();

            return (intptr_t)&timeInfo;
        }

#endif
        case audioMasterProcessEvents:
            if (self && ptr && self->midi.portMout)
            {
                int32_t i;
                const VstEvents* const events = (VstEvents*)ptr;

                for (i=0; i < events->numEvents && self->events.numEvents+i < MAX_MIDI_EVENTS*2; i++)
                {
                    const VstMidiEvent* const midiEvent = (VstMidiEvent*)events->events[i];

                    if (midiEvent && midiEvent->type == kVstMidiType)
                        memcpy(&self->midiEvents[self->events.numEvents+i], midiEvent, sizeof(VstMidiEvent));
                }

                self->midiEvents[self->events.numEvents+i].type = 0;
            }
            else
                qDebug("VstHostCallback:audioMasterProcessEvents - Some MIDI Out events were ignored");

            break;

#if 0
#if ! VST_FORCE_DEPRECATED
#if 0
        case audioMasterSetTime:
            // Deprecated in VST SDK 2.4
            break;
#endif
#endif

#if ! VST_FORCE_DEPRECATED
        case audioMasterTempoAt:
            // Deprecated in VST SDK 2.4
            jack_position_t* jack_pos;
            carla_jack_transport_query(&jack_pos);

            if (jack_pos->unique_1 == jack_pos->unique_2 && (jack_pos->valid & JackPositionBBT) > 0)
                return jack_pos->beats_per_minute * 10000;

            return 120 * 10000;

        case audioMasterGetNumAutomatableParameters:
            // Deprecated in VST SDK 2.4
            return carla_options.max_parameters;

#if 0
        case audioMasterGetParameterQuantization:
            // Deprecated in VST SDK 2.4
            break;
#endif
#endif
#endif

#if 0
        case audioMasterIOChanged:
            if (self && self->m_id >= 0)
            {
                short _id = self->m_id;

                carla_proc_lock();
                plugin->m_id = -1;
                carla_proc_unlock();

                if (self->m_active)
                {
                    self->effect->dispatcher(self->effect, effStopProcess, 0, 0, nullptr, 0.0f);
                    self->effect->dispatcher(self->effect, effMainsChanged, 0, 0, nullptr, 0.0f);
                }

                self->reload();

                if (self->m_active)
                {
                    self->effect->dispatcher(self->effect, effMainsChanged, 0, 1, nullptr, 0.0f);
                    self->effect->dispatcher(self->effect, effStartProcess, 0, 0, nullptr, 0.0f);
                }

                self->m_id = _id;

#ifndef BUILD_BRIDGE
                callback_action(CALLBACK_RELOAD_ALL, plugin->id, 0, 0, 0.0);
#endif
            }
            break;

        case audioMasterNeedIdle:
            // Deprecated in VST SDK 2.4
            break;
#endif

        case audioMasterSizeWindow:
            if (self)
            {
                self->gui.width  = index;
                self->gui.height = value;
                callback_action(CALLBACK_RESIZE_GUI, self->id(), index, value, 0.0);
            }
            return 1;

        case audioMasterGetSampleRate:
            return get_sample_rate();

        case audioMasterGetBlockSize:
            return get_buffer_size();

#if 0
        case audioMasterGetInputLatency:
            return 0;

        case audioMasterGetOutputLatency:
            return 0;

        case audioMasterGetPreviousPlug:
            // Deprecated in VST SDK 2.4
            break;

        case audioMasterGetNextPlug:
            // Deprecated in VST SDK 2.4
            break;

        case audioMasterWillReplaceOrAccumulate:
            // Deprecated in VST SDK 2.4
            break;
#endif

#if 0
        case audioMasterGetCurrentProcessLevel:
            if (carla_jack_on_audio_thread())
            {
                if (carla_jack_on_freewheel())
                    return kVstProcessLevelOffline;
                return 	kVstProcessLevelRealtime;
            }
            return 	kVstProcessLevelUser;
#endif

#if 0
        case audioMasterGetAutomationState:
            // TODO
            return 0;

        case audioMasterOfflineStart:
        case audioMasterOfflineRead:
        case audioMasterOfflineWrite:
        case audioMasterOfflineGetCurrentPass:
        case audioMasterOfflineGetCurrentMetaPass:
            // TODO
            break;

        case audioMasterSetOutputSampleRate:
            // Deprecated in VST SDK 2.4
            break;

        case audioMasterGetOutputSpeakerArrangement:
            // Deprecated in VST SDK 2.4
            break;
#endif

        case audioMasterGetVendorString:
            strcpy((char*)ptr, "falkTX");
            break;

        case audioMasterGetProductString:
            strcpy((char*)ptr, "Carla");
            break;

        case audioMasterGetVendorVersion:
            return 0x05; // 0.5

        case audioMasterVendorSpecific:
            break;

#if ! VST_FORCE_DEPRECATED
        case audioMasterSetIcon:
            // Deprecated in VST SDK 2.4
            break;
#endif

        case audioMasterCanDo:
#ifdef DEBUG
            qDebug("VstHostCallback:audioMasterCanDo - %s", (char*)ptr);
#endif

            if (strcmp((char*)ptr, "supplyIdle") == 0)
                return 1;
            if (strcmp((char*)ptr, "sendVstEvents") == 0)
                return 1;
            if (strcmp((char*)ptr, "sendVstMidiEvent") == 0)
                return 1;
            if (strcmp((char*)ptr, "sendVstMidiEventFlagIsRealtime") == 0)
                return -1;
            if (strcmp((char*)ptr, "sendVstTimeInfo") == 0)
                return 1;
            if (strcmp((char*)ptr, "receiveVstEvents") == 0)
                return 1;
            if (strcmp((char*)ptr, "receiveVstMidiEvent") == 0)
                return 1;
            if (strcmp((char*)ptr, "receiveVstTimeInfo") == 0)
                return -1;
            if (strcmp((char*)ptr, "reportConnectionChanges") == 0)
                return -1;
            if (strcmp((char*)ptr, "acceptIOChanges") == 0)
                return 1;
            if (strcmp((char*)ptr, "sizeWindow") == 0)
                return 1;
            if (strcmp((char*)ptr, "offline") == 0)
                return -1;
            if (strcmp((char*)ptr, "openFileSelector") == 0)
                return -1;
            if (strcmp((char*)ptr, "closeFileSelector") == 0)
                return -1;
            if (strcmp((char*)ptr, "startStopProcess") == 0)
                return 1;
            if (strcmp((char*)ptr, "supportShell") == 0)
                return -1;
            if (strcmp((char*)ptr, "shellCategory") == 0)
                return -1;

            // unimplemented
            qWarning("VstHostCallback:audioMasterCanDo - Got unknown feature request '%s'", (char*)ptr);
            return 0;

        case audioMasterGetLanguage:
            return kVstLangEnglish;

#if 0
        case audioMasterOpenWindow:
        case audioMasterCloseWindow:
            // Deprecated in VST SDK 2.4
            break;

        case audioMasterGetDirectory:
            // TODO
            break;
#endif

        case audioMasterUpdateDisplay:
            if (self)
            {
                // Update current program name
                if (self->prog.count > 0 && self->prog.current >= 0)
                {
                    char strBuf[STR_MAX] = { 0 };
                    self->effect->dispatcher(self->effect, effGetProgramName, 0, 0, strBuf, 0.0f);

                    if (strBuf[0] != 0 && !(self->prog.names[self->prog.current] && strcmp(strBuf, self->prog.names[self->prog.current]) == 0))
                    {
                        if (self->prog.names[self->prog.current])
                            free((void*)self->prog.names[self->prog.current]);

                        self->prog.names[self->prog.current] = strdup(strBuf);
                    }
                }

#ifndef BUILD_BRIDGE
                // Tell backend to update
                callback_action(CALLBACK_UPDATE, self->id(), 0, 0, 0.0);
#endif
            }
            break;

#if 0
        case audioMasterBeginEdit:
        case audioMasterEndEdit:
            // TODO
            break;

        case audioMasterOpenFileSelector:
        case audioMasterCloseFileSelector:
            // TODO
            break;

        case audioMasterEditFile:
            // Deprecated in VST SDK 2.4
            break;

        case audioMasterGetChunkFile:
            // Deprecated in VST SDK 2.4
            break;

        case audioMasterGetInputSpeakerArrangement:
            // Deprecated in VST SDK 2.4
            break;
#endif

        default:
            //qDebug("VstHostCallback(%p, opcode: %s, index: %i, value: " P_INTPTR ", opt: %f", effect, VstOpcode2str(opcode), index, value, opt);
            break;
        }

        return 0;
    }

private:
    int unique1;
    AEffect* effect;
    struct {
        int32_t numEvents;
        intptr_t reserved;
        VstEvent* data[MAX_MIDI_EVENTS*2];
    } events;
    VstMidiEvent midiEvents[MAX_MIDI_EVENTS*2];

    struct {
        bool visible;
        int width;
        int height;
    } gui;
    int unique2;
};

short add_plugin_vst(const char* filename, const char* label)
{
    qDebug("add_plugin_vst(%s, %s)", filename, label);

    short id = get_new_plugin_id();

    if (id >= 0)
    {
        VstPlugin* plugin = new VstPlugin(id);

        if (plugin->init(filename, label))
        {
            plugin->reload();

            unique_names[id] = plugin->name();
            CarlaPlugins[id] = plugin;

            plugin->registerToOsc();
        }
        else
        {
            delete plugin;
            id = -1;
        }
    }
    else
        set_last_error("Maximum number of plugins reached");

    return id;
}

/**@}*/

CARLA_BACKEND_END_NAMESPACE
