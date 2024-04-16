{ lib
, stdenv
, cmake
, extra-cmake-modules
, kwin
, wrapQtAppsHook
, qttools
}:

stdenv.mkDerivation rec {
  pname = "kwin-effects-forceblur";
  version = "1.2.0";

  src = ./.;

  nativeBuildInputs = [
    cmake
    extra-cmake-modules
    wrapQtAppsHook
  ];

  buildInputs = [
    kwin
    qttools
  ];

  meta = with lib; {
    description = "A fork of the KWin Blur effect for KDE Plasma 6 with the ability to blur any window on Wayland and X11";
    license = licenses.gpl3;
    homepage = "https://github.com/taj-ny/kwin-effects-forceblur";
  };
}
