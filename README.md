# kwin-forceblur-wayland
A fork of [KWin](https://invent.kde.org/plasma/kwin) that allows the user to force-enable the blur effect on any window on Wayland. X11 is supported as well, but consider using the [kwin-forceblur](https://github.com/esjeon/kwin-forceblur) script instead if you don't use Wayland.

This feature could probably be implemented in a plugin, however forking was just easier for me.

# Building
See CONTRIBUTING.md.

# Usage
1. Create a new window rule matching the window(s) you want to be blurred.
2. Add the ``Force blur`` property and set it to ``Yes``.
3. Click Apply.

You may need to resize the matched window(s) in order to update the blurred regions.

![image](https://github.com/taj-ny/kwin-forceblur-wayland/assets/79316397/795f0038-e0c8-4efc-a555-d3390fbda1c0)

# Improving performance
Having too many blurred windows open may result in stuttering. To fix this, set ``Latency`` to ``Force smoothest animations`` in compositor settings and start KWin with ``KWIN_DRM_NO_AMS=1``.
