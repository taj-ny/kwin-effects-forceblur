# kwin-effects-forceblur [![AUR Version](https://img.shields.io/aur/version/kwin-effects-forceblur)](https://aur.archlinux.org/packages/kwin-effects-forceblur)
A fork of the KWin Blur effect for KDE Plasma 6 with the ability to blur any window on Wayland and X11. It cannot be used along with the stock blur effect or any other fork of it.

Latest features are available on the ``develop`` branch.

![image](https://github.com/taj-ny/kwin-effects-forceblur/assets/79316397/5f466c9c-584f-4db3-9a15-57e590a591e0)
<sup>Window opacity has been set to 85% in the screenshot.</sup>

# Installation
## Arch Linux
https://aur.archlinux.org/packages/kwin-effects-forceblur

## NixOS
> [!NOTE]
> Plasma 6 is only available in nixpkgs unstable.

``flake.nix``:
```nix
{
  inputs = {
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


## Building from source
> [!NOTE]  
> It may be necessary to rebuild the effect after a system upgrade.

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

# Usage
> [!NOTE]  
> The window needs to be transparent in order for the blur to be visible.

1. Install the plugin.
2. Open the ``Desktop Effects`` page in ``System Settings``.
3. Disable the Blur effect and any other forks of it as well, such as the one provided by LightlyShaders.
4. Enable the ``Force Blur`` effect.

The classes of windows to blur can be specified in the effect settings. You can obtain them in two ways:
  - Run ``qdbus org.kde.KWin /KWin org.kde.KWin.queryWindowInfo`` and click on the window. You can use either ``resourceClass`` or ``resourceName``.
  - Right click on the titlebar, go to More Options and Configure Special Window/Application Settings. The class can be found at ``Window class (application)``. If there is a space, for example ``Navigator firefox``, you can use either ``Navigator`` or ``firefox``.

Window borders will be blurred only if decoration blur is enabled.

# Cursor input lag or stuttering on Wayland
If you're experiencing cursor input lag or stuttering when having many blurred windows open, launch KWin with ``KWIN_DRM_NO_AMS=1``. For Intel GPUs, ``KWIN_FORCE_SW_CURSOR=0`` is also necessary, however this may cause other issues.

# Credits
- [a-parhom/LightlyShaders](https://github.com/a-parhom/LightlyShaders) - CMakeLists.txt files
- [Alban-Boissard/kwin-effects-blur-respect-rounded-decorations](https://github.com/Alban-Boissard/kwin-effects-blur-respect-rounded-decorations) - Rounded corners
- [thegeekywanderer/kwin-effects-blur-respect-rounded-decorations](https://github.com/thegeekywanderer/kwin-effects-blur-respect-rounded-decorations) - Fix for bottom corners
