{
  description = "A very basic flake";

  inputs.nixpkgs.url = github:NixOS/nixpkgs/nixos-21.11;

  outputs = { self, nixpkgs }: {
    packages.x86_64-linux.hello = nixpkgs.legacyPackages.x86_64-linux.hello;
    packages.x86_64-linux.moongen = nixpkgs.legacyPackages.x86_64-linux.callPackage ./nix/moongen.nix {
      nixpkgs = nixpkgs.legacyPackages.x86_64-linux;
      linux = nixpkgs.legacyPackages.x86_64-linux.linuxPackages_5_10.kernel;
    };

    defaultPackage.x86_64-linux = self.packages.x86_64-linux.moongen;

    devShell = nixpkgs.legacyPackages.x86_64-linux.mkShell {
      buildInputs = with nixpkgs.legacyPackages.x86_64-linux; [
        self.packages.x86_64-linux.moongen
      ];
    };

  };
}
