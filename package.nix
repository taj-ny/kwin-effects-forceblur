{ lib
, stdenv
, cmake
, extra-cmake-modules
, kwin
, wrapQtAppsHook
, qttools
}:

stdenv.mkDerivation rec {
  pname = "kwin-better-blur";
  version = "1.3.4";

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
    description = "Fork of the KWin Blur effect for KDE Plasma 6 with additional features (including force blur) and bug fixes";
    license = licenses.gpl3;
    homepage = "https://github.com/taj-ny/kwin-effects-forceblur";
  };
}
