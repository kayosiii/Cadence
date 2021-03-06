/*
 * Patchbay Canvas engine using QGraphicsView/Scene
 * Copyright (C) 2010-2012 Filipe Coelho <falktx@gmail.com>
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

#ifndef CANVASBEZIERLINE_H
#define CANVASBEZIERLINE_H

#include <QtGui/QGraphicsPathItem>

#include "abstractcanvasline.h"

class QPainter;

START_NAMESPACE_PATCHCANVAS

class CanvasPort;
class CanvasPortGlow;

class CanvasBezierLine :
        public AbstractCanvasLine,
        public QGraphicsPathItem
{
public:
    CanvasBezierLine(CanvasPort* item1, CanvasPort* item2, QGraphicsItem* parent);
    ~CanvasBezierLine();

    virtual void deleteFromScene();

    virtual bool isLocked() const;
    virtual void setLocked(bool yesno);

    virtual bool isLineSelected() const;
    virtual void setLineSelected(bool yesno);

    virtual void updateLinePos();

    virtual int type() const;

    // QGraphicsItem generic calls
    virtual void setZValue(qreal z)
    {
        QGraphicsPathItem::setZValue(z);
    }

private:
    CanvasPort* item1;
    CanvasPort* item2;
    CanvasPortGlow* glow;
    bool m_locked;
    bool m_lineSelected;

    void updateLineGradient();

    virtual void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget);
};

END_NAMESPACE_PATCHCANVAS

#endif // CANVASBEZIERLINE_H
