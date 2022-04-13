{
  description = "A very basic flake";

  inputs = {
    nixpkgs.url = github:NixOS/nixpkgs/nixos-21.11;
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }: 
  (flake-utils.lib.eachSystem ["x86_64-linux"] (system:
  let
    pkgs = nixpkgs.legacyPackages.${system};
  in  {
    packages.moongen = pkgs.callPackage ./nix/moongen.nix {
      linux = pkgs.linuxPackages_5_10.kernel;
    };

    defaultPackage = self.packages.${system}.moongen;

    devShell = pkgs.mkShell {
      buildInputs = with pkgs; [
        self.packages.${system}.moongen
        just
        iperf2
      ];
    };

    # nix develop .#qemu
    devShells.qemu = pkgs.qemu.overrideAttrs (old: {
      buildInputs = [ pkgs.libndctl pkgs.libtasn1 ] ++ old.buildInputs;
      nativeBuildInputs = [ pkgs.meson pkgs.ninja ] ++ old.nativeBuildInputs;
      hardeningDisable = [ "stackprotector" ];
      shellHook = ''
        unset CPP # intereferes with dependency calculation
      '';
    });
  }));
}
