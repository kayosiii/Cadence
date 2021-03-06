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

#ifndef CARLA_THREADS_H
#define CARLA_THREADS_H

#include "carla_backend.h"

#include <QtCore/QThread>

class QProcess;

// --------------------------------------------------------------------------------------------------------
// CarlaCheckThread

class CarlaCheckThread : public QThread
{
public:
    CarlaCheckThread(QObject* parent = 0);
    void run();
    void stopNow();

private:
    bool m_stopNow;
};

// --------------------------------------------------------------------------------------------------------
// CarlaPluginThread

class CarlaPluginThread : public QThread
{
public:
    enum PluginThreadMode {
        PLUGIN_THREAD_DSSI_GUI,
        PLUGIN_THREAD_LV2_GUI,
        PLUGIN_THREAD_BRIDGE
    };

    CarlaPluginThread(CarlaPlugin* plugin, PluginThreadMode mode);
    ~CarlaPluginThread();

    void setOscData(const char* const binary, const char* const label, const char* const data1="");

protected:
    void run();

private:
    CarlaPlugin* m_plugin;
    PluginThreadMode m_mode;

    QString m_binary;
    QString m_label;
    QString m_data1;

    QProcess* m_process;
};

#endif // CARLA_THREADS_H
