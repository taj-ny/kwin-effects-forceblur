# kwin-forceblur-wayland
> [!WARNING]
> I have no idea what I'm doing. You probably shouldn't use this.

A fork of [KWin](https://invent.kde.org/plasma/kwin) that allows the user to force-enable the blur effect on any window on Wayland.

For X11 use the [kwin-forceblur](https://github.com/esjeon/kwin-forceblur) script instead.

### Supported versions
- [5.27.10](https://github.com/taj-ny/kwin-forceblur-wayland/tree/v5.27.10)

# How to use
1. Build and install the fork.
2. Go to System Settings -> Window Management -> Window Rules.
3. Create a new rule matching the windows you want to be blurred.
4. Add the ``Force blur`` property and set it to ``Yes``.
5. Click Apply.

You may need to resize the affected windows in order to update the blurred regions.

![image](https://github.com/taj-ny/kwin-forceblur-wayland/assets/79316397/795f0038-e0c8-4efc-a555-d3390fbda1c0)
