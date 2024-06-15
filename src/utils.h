#pragma once

#include <effect/globals.h>
#include "core/pixelgrid.h"

namespace KWin
{

inline QRegion scaledRegion(const QRegion &region, qreal scale)
{
    QRegion scaledRegion;
    for (const QRect &rect : region) {
        scaledRegion += snapToPixelGridF(scaledRect(QRectF(rect), scale)).toRect();
    }

    return scaledRegion;
}

}