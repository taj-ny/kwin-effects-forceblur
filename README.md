# kwin-forceblur
A fork of the KWin Blur effect that allows blurring any user-specified window. Both X11 and Wayland are supported.

# Dependencies
Plasma >= 6.0, qt6, kf6 and kwin development packages

# Building
```sh
git clone https://github.com/taj-ny/kwin-forceblur --single-branch
cd kwin-forceblur
mkdir qt6build; cd qt6build; cmake ../ -DCMAKE_INSTALL_PREFIX=/usr && make && sudo make install
```

# Usage
1. Install the plugin.
2. Open the ``Desktop Effects`` page in ``System Settings``.
3. Disable the Blur effect, and any other forks of the blur effect as well (such as LightlyShaders).
4. Enable the ``Force Blur`` effect.

You can specify the classes of windows to blur in the effect settings.

Blurring window decorations doesn't work well with GTK windows, so you have to exclude them.