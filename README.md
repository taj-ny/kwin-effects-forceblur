# kwin-effects-forceblur [![AUR Version](https://img.shields.io/aur/version/kwin-effects-forceblur)](https://aur.archlinux.org/packages/kwin-effects-forceblur)
A fork of the KWin Blur effect for KDE Plasma 6 with the ability to blur any window on Wayland and X11.

Latest features are available on the ``develop`` branch.

![image](https://github.com/taj-ny/kwin-effects-forceblur/assets/79316397/9d2f337e-badd-4d95-ba55-96c80202e196)
<sup>Window opacity has been set to 85% for System Settings and Dolphin, Firefox uses a transparent theme | [NixOS configuration](https://github.com/taj-ny/nix-config)</sup>

# Features
- Wayland support
- Draw image behind windows instead of blurring (can be used with a blurred image of the wallpaper in order to achieve a very similar effect to blur but with **much** lower GPU usage)
- Rounded corners
- Fix for [artifacts](https://github.com/taj-ny/kwin-effects-forceblur/pull/38) when using a transparent color scheme

# Installation
<details>
  <summary>NixOS (flakes)</summary>
  <br>
  
  ``flake.nix``:
  ```nix
  {
    inputs = {
      nixpkgs.url = "github:nixos/nixpkgs/nixos-unstable";
  
      kwin-effects-forceblur = {
        url = "github:taj-ny/kwin-effects-forceblur";
        inputs.nixpkgs.follows = "nixpkgs";
      };
    };
  }
  ```
  
  ```nix
  { inputs, pkgs, ... }:
  
  {
    environment.systemPackages = [
      inputs.kwin-effects-forceblur.packages.${pkgs.system}.default
    ];
  }
  ```
</details>

## Building from source
Dependencies:
- CMake
- Extra CMake Modules
- Plasma 6
- Qt 6
- KF6
- KWin development packages

```sh
git clone https://github.com/taj-ny/kwin-effects-forceblur
cd kwin-effects-forceblur
mkdir build
cd build
cmake ../ -DCMAKE_INSTALL_PREFIX=/usr
make
sudo make install
```

Remove the *build* directory when rebuilding the effect.

# Usage
> [!NOTE]  
> If the effect stops working after a system upgrade, you will need to rebuild it.

Since kwin-effects-forceblur is a fork, you need to disable the stock blur effect and any other blur effects you may be using.

1. Install the plugin.
2. Open the *Desktop Effects* page in *System Settings*.
3. Disable any blur effects.
4. Enable the *Force Blur* effect.

For more detailed descriptions of some options, check out [this wiki page](https://github.com/taj-ny/kwin-effects-forceblur/wiki/Configuration).
   
### Window transparency
The window needs to be translucent in order for the blur to be visible. This can be done in multiple ways:
- Use a transparent theme for the program if it supports it
- Use a transparent color scheme, such as [Alpha](https://store.kde.org/p/1972214)
- Create a window rule that reduces the window opacity

### Obtaining window classes
The classes of windows to blur can be specified in the effect settings. You can obtain them in two ways:
  - Run ``qdbus org.kde.KWin /KWin org.kde.KWin.queryWindowInfo`` and click on the window. You can use either *resourceClass* or *resourceName*.
  - Right click on the titlebar, go to *More Options* and *Configure Special Window/Application Settings*. The class can be found at *Window class (application)*. If there is a space, for example *Navigator firefox*, you can use either *Navigator* or *firefox*.

# High cursor latency or stuttering on Wayland
This effect can be very resource-intensive if you have a lot of windows open. On Wayland, high GPU load may result in higher cursor latency or even stuttering. If that bothers you, set the following environment variable: ``KWIN_DRM_NO_AMS=1``. If that's not enough, try enabling or disabling the software cursor by also setting ``KWIN_FORCE_SW_CURSOR=0`` or ``KWIN_FORCE_SW_CURSOR=1``.

Intel GPUs use software cursor by default due to [this bug](https://gitlab.freedesktop.org/drm/intel/-/issues/9571), however it doesn't seem to affect all GPUs.

# Credits
- [a-parhom/LightlyShaders](https://github.com/a-parhom/LightlyShaders) - CMakeLists.txt files
- [Alban-Boissard/kwin-effects-blur-respect-rounded-decorations](https://github.com/Alban-Boissard/kwin-effects-blur-respect-rounded-decorations) - Rounded corners
- [thegeekywanderer/kwin-effects-blur-respect-rounded-decorations](https://github.com/thegeekywanderer/kwin-effects-blur-respect-rounded-decorations) - Fix for bottom corners
