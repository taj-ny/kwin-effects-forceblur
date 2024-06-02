#pragma once

#include <effect/globals.h>
#include "core/pixelgrid.h"
#include <QRegion>

namespace KWin
{

inline QRegion scaledRegion(const QRegion &region, qreal scale)
{
    QRegion scaledRegion;
    for (const QRect &rect : region) {
        scaledRegion += QRect(std::round(rect.x() * scale), std::round(rect.y() * scale), std::round(rect.width() * scale), std::round(rect.height() * scale));
    }

    return scaledRegion;
}

}