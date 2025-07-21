# Better Blur
Better Blur is a fork of the Plasma 6 blur effect with additional features and bug fixes.

![image](https://github.com/user-attachments/assets/f8a7c618-89b4-485a-b0f8-29dd5f77e3ca)

### Features
- X11 and Wayland support
- Force blur
- Rounded corners with anti-aliasing
- Static blur for much lower GPU usage
- Adjust blur brightness, contrast and saturation
- Refraction (by [@DaddelZeit](https://github.com/DaddelZeit) and [@iGerman00](https://github.com/iGerman00))

### Bug fixes
Fixes for blur-related Plasma bugs that haven't been patched yet.

- Blur may sometimes disappear during animations
- [Transparent color schemes don't work properly with the Breeze application style](https://github.com/taj-ny/kwin-effects-forceblur/pull/38)

### Support for previous Plasma releases
Better Blur will usually support at least one previous Plasma release (second number in version - 6.x). Exceptions may be made if there is a large amount of breaking 
changes.

Currently supported versions: **6.4**

Latest Better Blur versions for previous Plasma releases:
- **6.0.0 - 6.3.5**: [v1.3.6](https://github.com/taj-ny/kwin-effects-forceblur/releases/tag/v1.3.6),
[fea9f80f27389aa8a62befb5babf40b28fed328d](https://github.com/taj-ny/kwin-effects-forceblur/tree/fea9f80f27389aa8a62befb5babf40b28fed328d)

# Installation
> [!IMPORTANT]
> If the effect stops working after a system upgrade, you will need to rebuild it or reinstall the package.

## Packages
<details>
  <summary>Arch Linux (AUR)*</summary>
  <br>

  **Choose *cleanBuild* when reinstalling the package.**

  https://aur.archlinux.org/packages/kwin-effects-forceblur
  ```
  yay -S kwin-effects-forceblur
  ```
</details>
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
      inputs.kwin-effects-forceblur.packages.${pkgs.system}.default # Wayland
      inputs.kwin-effects-forceblur.packages.${pkgs.system}.x11 # X11
    ];
  }
  ```
</details>
<details>
  <summary>Fedora (COPR)*</summary>
  <br>

  **This package is usually built against the latest version of KWin available in Fedora's official repositories, with a delay of up to 24 hours due to Fedora's update mechanism using bodhi. If you use a beta/testing/copr/advisory version of KWin, the effect may not work. In that case, you need to either recompile the effect using the instructions below, or rebuild the SRPM using `rpmbuild --rebuild /path/to/srpm.src.rpm`. Uninstall the rpm of the effect before attempting your build.**

  [Repository](https://copr.fedorainfracloud.org/coprs/hazel-bunny/ricing/package/kwin-effects-forceblur/)
  ```
  sudo dnf copr enable hazel-bunny/ricing
  sudo dnf install --refresh kwin-effects-forceblur
  ```
</details>

**\* Unofficial package, use at your own risk.**

## Manual
> [!NOTE]
> On Fedora Kinoite and other distributions based on it, the effect must be built in a container.

### Dependencies
- CMake
- Extra CMake Modules
- Plasma 6
- Qt 6
- KF6
- KWin development packages

<details>
  <summary>Arch Linux</summary>
  <br>

  Wayland:
  ```
  sudo pacman -S base-devel git extra-cmake-modules qt6-tools kwin
  ```
  
  X11:
  ```
  sudo pacman -S base-devel git extra-cmake-modules qt6-tools kwin-x11
  ```
</details>

<details>
  <summary>Debian-based (KDE Neon, Kubuntu, Ubuntu)</summary>
  <br>

  Wayland:
  ```
  sudo apt install -y git cmake g++ extra-cmake-modules qt6-tools-dev kwin-dev libkf6configwidgets-dev gettext libkf6crash-dev libkf6globalaccel-dev libkf6kio-dev libkf6service-dev libkf6notifications-dev libkf6kcmutils-dev libkdecorations3-dev libxcb-composite0-dev libxcb-randr0-dev libxcb-shm0-dev
  ```
  
  X11:
  ```
  sudo apt install -y git cmake g++ extra-cmake-modules qt6-tools-dev kwin-x11-dev libkf6configwidgets-dev gettext libkf6crash-dev libkf6globalaccel-dev libkf6kio-dev libkf6service-dev libkf6notifications-dev libkf6kcmutils-dev libkdecorations3-dev libxcb-composite0-dev libxcb-randr0-dev libxcb-shm0-dev
  ```
</details>

<details>
  <summary>Fedora 41, 42</summary>
  <br>

  Wayland:
  ```
  sudo dnf -y install git cmake extra-cmake-modules gcc-g++ kf6-kwindowsystem-devel plasma-workspace-devel libplasma-devel qt6-qtbase-private-devel qt6-qtbase-devel cmake kwin-devel extra-cmake-modules kwin-devel kf6-knotifications-devel kf6-kio-devel kf6-kcrash-devel kf6-ki18n-devel kf6-kguiaddons-devel libepoxy-devel kf6-kglobalaccel-devel kf6-kcmutils-devel kf6-kconfigwidgets-devel kf6-kdeclarative-devel kdecoration-devel kf6-kglobalaccel kf6-kdeclarative libplasma kf6-kio qt6-qtbase kf6-kguiaddons kf6-ki18n wayland-devel libdrm-devel
  ```
  
  X11:
  ```
  sudo dnf -y install git cmake extra-cmake-modules gcc-g++ kf6-kwindowsystem-devel plasma-workspace-devel libplasma-devel qt6-qtbase-private-devel qt6-qtbase-devel cmake extra-cmake-modules kf6-knotifications-devel kf6-kio-devel kf6-kcrash-devel kf6-ki18n-devel kf6-kguiaddons-devel libepoxy-devel kf6-kglobalaccel-devel kf6-kcmutils-devel kf6-kconfigwidgets-devel kf6-kdeclarative-devel kdecoration-devel kf6-kglobalaccel kf6-kdeclarative libplasma kf6-kio qt6-qtbase kf6-kguiaddons kf6-ki18n wayland-devel libdrm-devel kwin-x11-devel
  ```
</details>

<details>
  <summary>openSUSE</summary>
  <br>

  Wayland:
  ```
  sudo zypper in -y git cmake-full gcc-c++ kf6-extra-cmake-modules kcoreaddons-devel kguiaddons-devel kconfigwidgets-devel kwindowsystem-devel ki18n-devel kiconthemes-devel kpackage-devel frameworkintegration-devel kcmutils-devel kirigami2-devel "cmake(KF6Config)" "cmake(KF6CoreAddons)" "cmake(KF6FrameworkIntegration)" "cmake(KF6GuiAddons)" "cmake(KF6I18n)" "cmake(KF6KCMUtils)" "cmake(KF6KirigamiPlatform)" "cmake(KF6WindowSystem)" "cmake(Qt6Core)" "cmake(Qt6DBus)" "cmake(Qt6Quick)" "cmake(Qt6Svg)" "cmake(Qt6Widgets)" "cmake(Qt6Xml)" "cmake(Qt6UiTools)" "cmake(KF6Crash)" "cmake(KF6GlobalAccel)" "cmake(KF6KIO)" "cmake(KF6Service)" "cmake(KF6Notifications)" libepoxy-devel kwin6-devel
  ```
  
  X11:
  ```
  sudo zypper in -y git cmake-full gcc-c++ kf6-extra-cmake-modules kcoreaddons-devel kguiaddons-devel kconfigwidgets-devel kwindowsystem-devel ki18n-devel kiconthemes-devel kpackage-devel frameworkintegration-devel kcmutils-devel kirigami2-devel "cmake(KF6Config)" "cmake(KF6CoreAddons)" "cmake(KF6FrameworkIntegration)" "cmake(KF6GuiAddons)" "cmake(KF6I18n)" "cmake(KF6KCMUtils)" "cmake(KF6KirigamiPlatform)" "cmake(KF6WindowSystem)" "cmake(Qt6Core)" "cmake(Qt6DBus)" "cmake(Qt6Quick)" "cmake(Qt6Svg)" "cmake(Qt6Widgets)" "cmake(Qt6Xml)" "cmake(Qt6UiTools)" "cmake(KF6Crash)" "cmake(KF6GlobalAccel)" "cmake(KF6KIO)" "cmake(KF6Service)" "cmake(KF6Notifications)" libepoxy-devel kwin6-x11-devel
  ```
</details>

### Building
```sh
git clone https://github.com/taj-ny/kwin-effects-forceblur
cd kwin-effects-forceblur
mkdir build
cd build
cmake .. -DCMAKE_INSTALL_PREFIX=/usr
make -j$(nproc)
sudo make install
```

<details>
  <summary>Building on Fedora Kinoite</summary>
  <br>

  ```sh
  # enter container
  git clone https://github.com/taj-ny/kwin-effects-forceblur
  cd kwin-effects-forceblur
  mkdir build
  cd build
  cmake .. -DCMAKE_INSTALL_PREFIX=/usr
  make -j$(nproc)
  cpack -V -G RPM
  exit # exit container
  sudo rpm-ostree install kwin-effects-forceblur/build/kwin-better-blur.rpm
  ```
</details>

**Remove the *build* directory when rebuilding the effect.**

# Usage
This effect will conflict with the stock blur effect and any other forks of it.

1. Install the plugin.
2. Open the *Desktop Effects* page in *System Settings*.
3. Disable any blur effects.
4. Enable the *Better Blur* effect.

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
