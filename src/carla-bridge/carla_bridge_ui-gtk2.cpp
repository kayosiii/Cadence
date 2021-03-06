/*
 * Carla UI bridge code
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

#include "carla_bridge.h"
#include "carla_bridge_osc.h"

#ifdef BRIDGE_LV2_X11
#error X11 UI uses Qt4
#endif

#include <gtk/gtk.h>
#include <QtCore/QSettings>

static GtkWidget* window = nullptr;
static QSettings settings("Cadence", "Carla-Gtk2UIs");

// -------------------------------------------------------------------------

gint last_x, last_y, last_width, last_height;

void save_window_settings()
{
    if (client)
    {
        settings.setValue(QString("%1/pos_x").arg(client->get_title()), last_x);
        settings.setValue(QString("%1/pos_y").arg(client->get_title()), last_y);
        settings.setValue(QString("%1/width").arg(client->get_title()), last_width);
        settings.setValue(QString("%1/height").arg(client->get_title()), last_height);
        settings.sync();
    }
}

// -------------------------------------------------------------------------

gboolean gtk_ui_recheck(void*)
{
    if (window)
    {
        gtk_window_get_position(GTK_WINDOW(window), &last_x, &last_y);
        gtk_window_get_size(GTK_WINDOW(window), &last_width, &last_height);
    }

    return client ? client->run_messages() : false;
}

void gtk_ui_destroy(GtkWidget*, void*)
{
    toolkit_quit();
}

// -------------------------------------------------------------------------

void toolkit_init()
{
    static int argc = 0;
    static char** argv = { nullptr };
    gtk_init(&argc, &argv);
}

void toolkit_loop()
{
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_container_add(GTK_CONTAINER(window), (GtkWidget*)client->get_widget());

    g_timeout_add(50, gtk_ui_recheck, nullptr);
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_ui_destroy), nullptr);

    gtk_window_set_resizable(GTK_WINDOW(window), client->is_resizable());
    gtk_window_set_title(GTK_WINDOW(window), client->get_title());

    gtk_window_get_position(GTK_WINDOW(window), &last_x, &last_y);
    gtk_window_get_size(GTK_WINDOW(window), &last_width, &last_height);

    if (settings.contains(QString("%1/pos_x").arg(client->get_title())))
    {
        last_x = settings.value(QString("%1/pos_x").arg(client->get_title()), last_x).toInt();
        last_y = settings.value(QString("%1/pos_y").arg(client->get_title()), last_y).toInt();
        gtk_window_move(GTK_WINDOW(window), last_x, last_y);

        if (client->is_resizable())
        {
            last_width  = settings.value(QString("%1/width").arg(client->get_title()), last_width).toInt();
            last_height = settings.value(QString("%1/height").arg(client->get_title()), last_height).toInt();
            gtk_window_resize(GTK_WINDOW(window), last_width, last_height);
        }
    }

    osc_send_update();

    // Main loop
    gtk_main();
}

void toolkit_quit()
{
    save_window_settings();

    if (window)
    {
        gtk_widget_destroy(window);
        gtk_main_quit();
    }

    window = nullptr;
}

void toolkit_window_show()
{
    if (window)
        gtk_widget_show_all(window);
}

void toolkit_window_hide()
{
    if (window)
        gtk_widget_hide_all(window);
}

void toolkit_window_resize(int width, int height)
{
    if (window)
        gtk_window_resize(GTK_WINDOW(window), width, height);
}
