#!/usr/bin/env python3
# -*- coding: utf-8 -*-

# Common/Shared code related to JACK
# Copyright (C) 2010-2012 Filipe Coelho <falktx@gmail.com>
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# For a full copy of the GNU General Public License see the COPYING file

# Imports (Global)
from PyQt4.QtCore import pyqtSlot, QTimer
from PyQt4.QtGui import QCursor, QFontMetrics, QMenu

# Imports (Custom Stuff)
import jacksettings, logs, render
from shared import *
from jacklib_helpers import *

# Have JACK2 ?
if jacklib.JACK2:
    print("Using JACK2, version %s" % cString(jacklib.get_version_string()))

# Can Render ?
for iPATH in PATH:
    if os.path.exists(os.path.join(iPATH, "jack_capture")):
        canRender = True
        break
else:
    canRender = False

# Variables
TRANSPORT_VIEW_HMS = 0
TRANSPORT_VIEW_BBT = 1
TRANSPORT_VIEW_FRAMES = 2

buffer_sizes = (16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192)
sample_rates = (22050, 32000, 44100, 48000, 88200, 96000, 192000)

# DBus object
class DBusObject(object):
    __slots__ = [
        'loop',
        'bus',
        'a2j',
        'jack',
        'ladish_control',
        'ladish_studio',
        'ladish_room',
        'ladish_graph',
        'ladish_manager',
        'ladish_app_iface',
        'ladish_app_daemon',
        'patchbay'
    ]

DBus = DBusObject()

# Jack object
class JackObject(object):
    __slots__ = [
        'client'
    ]

jack = JackObject()

# Init objects
DBus.loop = None
DBus.bus = None
DBus.a2j = None
DBus.jack = None
DBus.ladish_control = None
DBus.ladish_studio = None
DBus.ladish_room = None
DBus.ladish_graph = None
DBus.ladish_app_iface = None
DBus.ladish_app_daemon = None
DBus.patchbay = None

jack.client = None

# -------------------------------------------------------------
# Property change calls

def jack_buffer_size(self_, buffer_size):
    if self_.m_buffer_size != buffer_size:
        if jack.client:
            failed = bool(jacklib.set_buffer_size(jack.client, buffer_size) != 0)
        else:
            failed = bool(jacksettings.setBufferSize(buffer_size))

        if failed:
            print("Failed to change buffer-size as %i, reset to %i" % (buffer_size, self_.m_buffer_size))
            setBufferSize(self_, self_.m_buffer_size, True)

def jack_sample_rate(self_, sample_rate):
    if jack.client:
        setSampleRate(self_, sample_rate, True)
    else:
        if jacksettings.setSampleRate(sample_rate):
            setSampleRate(self_, sample_rate)

def slot_jackBufferSize_ComboBox(self_, text):
    if text and text.isdigit():
        jack_buffer_size(self_, int(text))

#@pyqtSlot(int)
def slot_jackBufferSize_Menu(self_, buffer_size):
    jack_buffer_size(self_, buffer_size)

#@pyqtSlot(str)
def slot_jackSampleRate_ComboBox(self_, text):
    if text and text.isdigit():
        jack_sample_rate(self_, int(text))

# -------------------------------------------------------------
# Transport calls

def setTransportView(self_, view):
    if view == TRANSPORT_VIEW_HMS:
        self_.m_selected_transport_view = TRANSPORT_VIEW_HMS
        self_.label_time.setMinimumWidth(QFontMetrics(self_.label_time.font()).width("00:00:00") + 3)
    elif view == TRANSPORT_VIEW_BBT:
        self_.m_selected_transport_view = TRANSPORT_VIEW_BBT
        self_.label_time.setMinimumWidth(QFontMetrics(self_.label_time.font()).width("000|00|0000") + 3)
    elif view == TRANSPORT_VIEW_FRAMES:
        self_.m_selected_transport_view = TRANSPORT_VIEW_FRAMES
        self_.label_time.setMinimumWidth(QFontMetrics(self_.label_time.font()).width("000'000'000") + 3)
    else:
        self_.m_selected_transport_view = None

#@pyqtSlot(bool)
def slot_transportPlayPause(self_, play):
    if not jack.client: return
    if play:
        jacklib.transport_start(jack.client)
    else:
        jacklib.transport_stop(jack.client)
    refreshTransport(self_)

#@pyqtSlot()
def slot_transportStop(self_):
    if not jack.client: return
    jacklib.transport_stop(jack.client)
    jacklib.transport_locate(jack.client, 0)
    refreshTransport(self_)

#@pyqtSlot()
def slot_transportBackwards():
    if not jack.client: return
    new_frame = jacklib.get_current_transport_frame(jack.client) - 100000
    if new_frame < 0:
        new_frame = 0
    jacklib.transport_locate(jack.client, new_frame)

#@pyqtSlot()
def slot_transportForwards():
    if not jack.client: return
    new_frame = jacklib.get_current_transport_frame(jack.client) + 100000
    jacklib.transport_locate(jack.client, new_frame)

#@pyqtSlot()
def slot_transportViewMenu(self_):
    menu = QMenu(self_)
    act_t_hms = menu.addAction("Hours:Minutes:Seconds")
    act_t_bbt = menu.addAction("Beat:Bar:Tick")
    act_t_fr  = menu.addAction("Frames")

    act_t_hms.setCheckable(True)
    act_t_bbt.setCheckable(True)
    act_t_fr.setCheckable(True)

    if self_.m_selected_transport_view == TRANSPORT_VIEW_HMS:
        act_t_hms.setChecked(True)
    elif self_.m_selected_transport_view == TRANSPORT_VIEW_BBT:
        act_t_bbt.setChecked(True)
    elif self_.m_selected_transport_view == TRANSPORT_VIEW_FRAMES:
        act_t_fr.setChecked(True)

    act_selected = menu.exec_(QCursor().pos())

    if act_selected == act_t_hms:
        setTransportView(self_, TRANSPORT_VIEW_HMS)
    elif act_selected == act_t_bbt:
        setTransportView(self_, TRANSPORT_VIEW_BBT)
    elif act_selected == act_t_fr:
        setTransportView(self_, TRANSPORT_VIEW_FRAMES)

# -------------------------------------------------------------
# Refresh GUI stuff

def refreshDSPLoad(self_):
    if not jack.client: return
    setDSPLoad(self_, int(jacklib.cpu_load(jack.client)))

def refreshTransport(self_):
    if not jack.client: return
    pos = jacklib.jack_position_t()
    pos.valid = 0
    state = jacklib.transport_query(jack.client, jacklib.pointer(pos))

    if self_.m_selected_transport_view == TRANSPORT_VIEW_HMS:
        frame = pos.frame
        time = frame / self_.m_sample_rate
        secs = time % 60
        mins = (time / 60) % 60
        hrs  = (time / 3600) % 60
        self_.label_time.setText("%02i:%02i:%02i" % (hrs, mins, secs))

    elif self_.m_selected_transport_view == TRANSPORT_VIEW_BBT:
        if pos.valid & jacklib.JackPositionBBT:
            bar  = pos.bar
            beat = pos.beat
            tick = pos.tick
            if bar == 0:
                beat = 0
                tick = 0
            self_.label_time.setText("%03i|%02i|%04i" % (bar, beat, tick))
        else:
            self_.label_time.setText("000|00|0000")

    elif self_.m_selected_transport_view == TRANSPORT_VIEW_FRAMES:
        frame1 = pos.frame % 1000
        frame2 = (pos.frame / 1000) % 1000
        frame3 = (pos.frame / 1000000) % 1000
        self_.label_time.setText("%03i'%03i'%03i" % (frame3, frame2, frame1))

    if pos.valid & jacklib.JackPositionBBT:
        if self_.m_last_bpm != pos.beats_per_minute:
            self_.sb_bpm.setValue(pos.beats_per_minute)
            self_.sb_bpm.setStyleSheet("")
    else:
        pos.beats_per_minute = -1
        if self_.m_last_bpm != pos.beats_per_minute:
            self_.sb_bpm.setStyleSheet("QDoubleSpinBox { color: palette(mid); }")

    self_.m_last_bpm = pos.beats_per_minute

    if state != self_.m_last_transport_state:
        if state == jacklib.JackTransportStopped:
            icon = getIcon("media-playback-start")
            self_.act_transport_play.setChecked(False)
            self_.act_transport_play.setIcon(icon)
            self_.act_transport_play.setText(self_.tr("&Play"))
            self_.b_transport_play.setChecked(False)
            self_.b_transport_play.setIcon(icon)
        else:
            icon = getIcon("media-playback-pause")
            self_.act_transport_play.setChecked(True)
            self_.act_transport_play.setIcon(icon)
            self_.act_transport_play.setText(self_.tr("&Pause"))
            self_.b_transport_play.setChecked(True)
            self_.b_transport_play.setIcon(icon)

    self_.m_last_transport_state = state

# -------------------------------------------------------------
# Set GUI stuff

def setBufferSize(self_, buffer_size, forced=False):
    if self_.m_buffer_size == buffer_size and not forced:
        return

    self_.m_buffer_size = buffer_size

    if buffer_size:
        if buffer_size == 16:
            self_.cb_buffer_size.setCurrentIndex(0)
        elif buffer_size == 32:
            self_.cb_buffer_size.setCurrentIndex(1)
        elif buffer_size == 64:
            self_.cb_buffer_size.setCurrentIndex(2)
        elif buffer_size == 128:
            self_.cb_buffer_size.setCurrentIndex(3)
        elif buffer_size == 256:
            self_.cb_buffer_size.setCurrentIndex(4)
        elif buffer_size == 512:
            self_.cb_buffer_size.setCurrentIndex(5)
        elif buffer_size == 1024:
            self_.cb_buffer_size.setCurrentIndex(6)
        elif buffer_size == 2048:
            self_.cb_buffer_size.setCurrentIndex(7)
        elif buffer_size == 4096:
            self_.cb_buffer_size.setCurrentIndex(8)
        elif buffer_size == 8192:
            self_.cb_buffer_size.setCurrentIndex(9)
        else:
            QMessageBox.warning(self_, self_.tr("Warning"), self_.tr("Invalid JACK buffer-size requested: %i" % buffer_size))

    if "act_jack_bf_list" in dir(self_):
        if buffer_size:
            for act_bf in self_.act_jack_bf_list:
                act_bf.setEnabled(True)
                if act_bf.text().replace("&", "") == str(buffer_size):
                    if not act_bf.isChecked():
                        act_bf.setChecked(True)
                else:
                    if act_bf.isChecked():
                        act_bf.setChecked(False)
        #else:
            #for i in range(len(self.act_jack_bf_list)):
                #self.act_jack_bf_list[i].setEnabled(False)
                #if (self.act_jack_bf_list[i].isChecked()):
                    #self.act_jack_bf_list[i].setChecked(False)

def setSampleRate(self_, sample_rate, future=False):
    if self_.m_sample_rate == sample_rate:
        return

    if future:
        if self_.sender() == self_.cb_sample_rate: # Changed using GUI
            ask = QMessageBox.question(self_, self_.tr("Change Sample Rate"),
                self_.tr("It's not possible to change Sample Rate while JACK is running.\n"
                         "Do you want to change as soon as JACK stops?"), QMessageBox.Ok | QMessageBox.Cancel)
            if ask == QMessageBox.Ok:
                self_.m_next_sample_rate = sample_rate
            else:
                self_.m_next_sample_rate = 0

    # not future
    else:
        self_.m_sample_rate = sample_rate
        self_.m_next_sample_rate = 0

    for i in range(len(sample_rates)):
        sample_rate = sample_rates[i]
        sample_rate_str = str(sample_rate)

        self_.cb_sample_rate.setItemText(i, sample_rate_str)

        if self_.m_sample_rate == sample_rate:
            self_.cb_sample_rate.setCurrentIndex(i)

def setRealTime(self_, realtime):
    self_.label_realtime.setText(" RT " if realtime else " <s>RT</s> ")
    self_.label_realtime.setEnabled(realtime)

def setDSPLoad(self_, dsp_load):
    self_.pb_dsp_load.setValue(dsp_load)

def setXruns(self_, xruns):
    self_.b_xruns.setText("%s Xrun%s" % (str(xruns) if (xruns >= 0) else "--", "" if (xruns == 1) else "s"))

# -------------------------------------------------------------
# External Dialogs

@pyqtSlot()
def slot_showJackSettings(self_):
    jacksettings.JackSettingsW(self_).exec_()

    # Force update of gui widgets
    if not jack.client:
        self_.jackStopped()

@pyqtSlot()
def slot_showLogs(self_):
    logs.LogsW(self_).show()

@pyqtSlot()
def slot_showRender(self_):
    render.RenderW(self_).exec_()

# -------------------------------------------------------------
# Shared Connections

def setJackConnections(self_, modes):
    if "jack" in modes:
        self_.connect(self_.act_jack_clear_xruns, SIGNAL("triggered()"), SLOT("slot_JackClearXruns()"))
        self_.connect(self_.act_jack_render, SIGNAL("triggered()"), lambda: slot_showRender(self_))
        self_.connect(self_.act_jack_configure, SIGNAL("triggered()"), lambda: slot_showJackSettings(self_))
        self_.connect(self_.b_jack_clear_xruns, SIGNAL("clicked()"), SLOT("slot_JackClearXruns()"))
        self_.connect(self_.b_jack_configure, SIGNAL("clicked()"), lambda: slot_showJackSettings(self_))
        self_.connect(self_.b_jack_render, SIGNAL("clicked()"), lambda: slot_showRender(self_))
        self_.connect(self_.cb_buffer_size, SIGNAL("currentIndexChanged(QString)"), lambda: slot_jackBufferSize_ComboBox(self_, self_.cb_buffer_size.currentText()))
        self_.connect(self_.cb_sample_rate, SIGNAL("currentIndexChanged(QString)"), lambda: slot_jackSampleRate_ComboBox(self_, self_.cb_sample_rate.currentText()))
        self_.connect(self_.b_xruns, SIGNAL("clicked()"), SLOT("slot_JackClearXruns()"))

    if "buffer-size" in modes:
        self_.connect(self_.act_jack_bf_16, SIGNAL("triggered(bool)"), lambda: slot_jackBufferSize_Menu(self_, 16))
        self_.connect(self_.act_jack_bf_32, SIGNAL("triggered(bool)"), lambda: slot_jackBufferSize_Menu(self_, 32))
        self_.connect(self_.act_jack_bf_64, SIGNAL("triggered(bool)"), lambda: slot_jackBufferSize_Menu(self_, 64))
        self_.connect(self_.act_jack_bf_128, SIGNAL("triggered(bool)"), lambda: slot_jackBufferSize_Menu(self_, 128))
        self_.connect(self_.act_jack_bf_256, SIGNAL("triggered(bool)"), lambda: slot_jackBufferSize_Menu(self_, 256))
        self_.connect(self_.act_jack_bf_512, SIGNAL("triggered(bool)"), lambda: slot_jackBufferSize_Menu(self_, 512))
        self_.connect(self_.act_jack_bf_1024, SIGNAL("triggered(bool)"), lambda: slot_jackBufferSize_Menu(self_, 1024))
        self_.connect(self_.act_jack_bf_2048, SIGNAL("triggered(bool)"), lambda: slot_jackBufferSize_Menu(self_, 2048))
        self_.connect(self_.act_jack_bf_4096, SIGNAL("triggered(bool)"), lambda: slot_jackBufferSize_Menu(self_, 4096))
        self_.connect(self_.act_jack_bf_8192, SIGNAL("triggered(bool)"), lambda: slot_jackBufferSize_Menu(self_, 8192))

    if "transport" in modes:
        self_.connect(self_.act_transport_play, SIGNAL("triggered(bool)"), lambda: slot_transportPlayPause(self_, self_.act_transport_play.isChecked()))
        self_.connect(self_.act_transport_stop, SIGNAL("triggered()"), lambda: slot_transportStop(self_))
        self_.connect(self_.act_transport_backwards, SIGNAL("triggered()"), lambda: slot_transportBackwards())
        self_.connect(self_.act_transport_forwards, SIGNAL("triggered()"), lambda: slot_transportForwards())
        self_.connect(self_.b_transport_play, SIGNAL("clicked(bool)"), lambda: slot_transportPlayPause(self_, self_.b_transport_play.isChecked()))
        self_.connect(self_.b_transport_stop, SIGNAL("clicked()"), lambda: slot_transportStop(self_))
        self_.connect(self_.b_transport_backwards, SIGNAL("clicked()"), lambda: slot_transportBackwards())
        self_.connect(self_.b_transport_forwards, SIGNAL("clicked()"), lambda: slot_transportForwards())
        self_.connect(self_.label_time, SIGNAL("customContextMenuRequested(QPoint)"), lambda: slot_transportViewMenu(self_))

    if "misc" in modes:
        if LINUX:
            self_.connect(self_.act_show_logs, SIGNAL("triggered()"), lambda: slot_showLogs(self_))
        else:
            self_.act_show_logs.setEnabled(False)
