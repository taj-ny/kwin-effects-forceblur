{
  description = "A fork of the KWin Blur effect for KDE Plasma 6 with the ability to blur any window on Wayland and X11";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, ... }@inputs: inputs.utils.lib.eachSystem [
    "x86_64-linux" "aarch64-linux"
  ] (system: let
    pkgs = import nixpkgs {
      inherit system;
    };
  in {
    packages.default = pkgs.kdePackages.callPackage (
      { lib
      , stdenv
      , cmake
      , extra-cmake-modules
      , kwin
      , wrapQtAppsHook
      , qttools
      , fetchFromGitHub
      }:
        
      stdenv.mkDerivation rec {
        pname = "kwin-effects-forceblur";
        version = "1.1.2";

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
    ) { };
  });
}
