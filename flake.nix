{
  description = "A very basic flake";

  inputs.nixpkgs.url = github:NixOS/nixpkgs/nixos-21.11;

  outputs = { self, nixpkgs }: let
    pkgs = nixpkgs.legacyPackages.x86_64-linux;
  in {
    packages.x86_64-linux.hello = pkgs.hello;
    packages.x86_64-linux.moongen = pkgs.callPackage ./nix/moongen.nix {
      linux = pkgs.linuxPackages_5_10.kernel;
    };

    defaultPackage.x86_64-linux = self.packages.x86_64-linux.moongen;

    devShell = pkgs.mkShell {
      buildInputs = [
        self.packages.x86_64-linux.moongen
      ];
    };

  };
}
