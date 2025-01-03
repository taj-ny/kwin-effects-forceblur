{
  description = "Fork of the KWin Blur effect for KDE Plasma 6 with additional features (including force blur) and bug fixes";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/88195a94f390381c6afcdaa933c2f6ff93959cb4";
    utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, ... }@inputs: inputs.utils.lib.eachSystem [
    "x86_64-linux" "aarch64-linux"
  ] (system: let
    pkgs = import nixpkgs {
      inherit system;
    };
  in rec {
    packages.default = pkgs.kdePackages.callPackage ./package.nix { };

    devShells.default = pkgs.mkShell {
      inputsFrom = [ packages.default ];
    };
  });
}
