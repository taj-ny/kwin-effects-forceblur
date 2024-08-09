#pragma once

namespace KWin
{

inline bool isMenu(const EffectWindow *w)
{
    return w->isMenu() || w->isDropdownMenu() || w->isPopupMenu() || w->isPopupWindow();
}

inline bool isDockFloating(const EffectWindow *dock, const QRegion blurRegion)
{
    // If the pixel at (0, height / 2) for horizontal panels and (width / 2, 0) for vertical panels doesn't belong to
    // the blur region, the dock is most likely floating. The (0,0) pixel may be outside the blur region if the dock
    // can float but isn't at the moment.
    return !blurRegion.intersects(QRect(0, dock->height() / 2, 1, 1)) && !blurRegion.intersects(QRect(dock->width() / 2, 0, 1, 1));
}

}