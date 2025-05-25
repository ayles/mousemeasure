{
  description = "Mouse Measure";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs =
    {
      self,
      nixpkgs,
      flake-utils,
      ...
    }:
    flake-utils.lib.eachDefaultSystem (
      system:
      let
        pkgs = nixpkgs.legacyPackages.${system};
      in
      {
        packages.default = pkgs.stdenv.mkDerivation {
          name = "mousemeasure";

          src = self;

          nativeBuildInputs = with pkgs; [
            cmake
            ninja
            clang-tools
          ];

          buildInputs = with pkgs; [
            glfw
            glew
            xorg.libSM
            xorg.libX11
            xorg.libXext
          ];
        };

        devShells.default = self.packages.${system}.default;
      }
    );
}
