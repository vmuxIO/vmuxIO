{
  description = "A very basic flake";

  inputs.nixpkgs.url = github:NixOS/nixpkgs/nixos-21.11;

  outputs = { self, nixpkgs }: let
    pkgs = nixpkgs.legacyPackages.x86_64-linux;
  in  {
    packages.x86_64-linux.moongen = pkgs.callPackage ./nix/moongen.nix {
      linux = pkgs.linuxPackages_5_10.kernel;
    };

    defaultPackage.x86_64-linux = self.packages.x86_64-linux.moongen;

    devShell.x86_64-linux = pkgs.mkShell {
      buildInputs = [
        self.packages.x86_64-linux.moongen
      ];
    };

    # nix develop .#qemu
    devShells.x86_64-linux.qemu = pkgs.qemu.overrideAttrs (old: {
      buildInputs = [ pkgs.libndctl pkgs.libtasn1 ] ++ old.buildInputs;
      nativeBuildInputs = [ pkgs.meson pkgs.ninja ] ++ old.nativeBuildInputs;
      hardeningDisable = [ "stackprotector" ];
      shellHook = ''
        unset CPP # intereferes with dependency calculation
      '';
    });
  };
}
