# kwin-forceblur [![AUR Version](https://img.shields.io/aur/version/kwin-effects-forceblur)](https://aur.archlinux.org/packages/kwin-effects-forceblur)
A fork of the KWin Blur effect for Plasma 6 with the ability to blur any window on Wayland and X11.

This effect cannot be used along with the stock blur effect, or any other fork of it.

![image](https://github.com/taj-ny/kwin-forceblur/assets/79316397/ca9892b5-2eba-47be-ae58-9009742a70f5)
<sup>Window opacity has been set to 85% in the screenshot.</sup>

# Installation
### Arch Linux
https://aur.archlinux.org/packages/kwin-effects-forceblur

### NixOS
https://gist.github.com/taj-ny/c1abdde710f33e34dc39dc53a5dc2c09

``pkgs.kdePackages.callPackage``

## Building from source
Required dependencies:
- CMake
- Extra CMake Modules
- Plasma 6
- Qt 6
- KF6
- KWin development packages

```sh
git clone https://github.com/taj-ny/kwin-forceblur
cd kwin-forceblur
mkdir qt6build; cd qt6build; cmake ../ -DCMAKE_INSTALL_PREFIX=/usr && make && sudo make install
```

# Usage
> [!NOTE]  
> The window needs to be transparent in order for the blur to be visible.

1. Install the plugin.
2. Open the ``Desktop Effects`` page in ``System Settings``.
3. Disable the Blur effect, and any other forks of the blur effect as well (such as the one provided by LightlyShaders).
4. Enable the ``Force Blur`` effect.

You can specify the classes of windows to blur in the effect settings.

Window borders will be blurred only if decoration blur is enabled.

# Credits
- [a-parhom/LightlyShaders](https://github.com/a-parhom/LightlyShaders) - CMakeLists.txt files
- [Alban-Boissard/kwin-effects-blur-respect-rounded-decorations](https://github.com/Alban-Boissard/kwin-effects-blur-respect-rounded-decorations) - Rounded corners
- [thegeekywanderer/kwin-effects-blur-respect-rounded-decorations](https://github.com/thegeekywanderer/kwin-effects-blur-respect-rounded-decorations) - Fix for bottom corners
