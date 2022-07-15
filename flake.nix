{
  description = "A very basic flake";

  inputs = {
    nixpkgs.url = github:NixOS/nixpkgs/nixos-22.05;

    flake-utils.url = "github:numtide/flake-utils";

    nixos-generators = {
      url = "github:nix-community/nixos-generators";
      inputs.nixpkgs.follows = "nixpkgs";
    };

    # on flake submodules https://github.com/NixOS/nix/pull/5434
    moonmux-src = {
      url = "git+https://github.com/vmuxIO/MoonGen?ref=dpdk-21.11&submodules=1";
      flake = false;
    };
    libmoon-src = {
      url = "git+https://github.com/vmuxIO/libmoon?ref=dev/ice&submodules=1";
      flake = false;
    };
    dpdk-src = {
      url = "git+https://github.com/vmuxIO/dpdk?ref=21.11-moon-vmux&submodules=1";
      flake = false;
    };
  };

  outputs = { 
    self, 
    nixpkgs, 
    flake-utils, 
    nixos-generators,
    moonmux-src, 
    libmoon-src,
    dpdk-src,
  }: 
  (flake-utils.lib.eachSystem ["x86_64-linux"] (system:
  let
    pkgs = nixpkgs.legacyPackages.${system};
    mydpdk = pkgs.callPackage ./nix/dpdk.nix {
      kernel = pkgs.linuxPackages_5_10.kernel;
    };
  in  {
    packages = {
      default = self.packages.${system}.moongen;

      # moongen/dpdk
      moongen = pkgs.callPackage ./nix/moongen.nix {
        linux = pkgs.linuxPackages_5_10.kernel;
      };
      moongen21 = pkgs.callPackage ./nix/moongen21.nix {
        linux = pkgs.linuxPackages_5_10.kernel;
        inherit moonmux-src libmoon-src dpdk-src;
      };
      dpdk = mydpdk;
      pktgen = pkgs.callPackage ./nix/pktgen.nix {
        dpdk = mydpdk;
      };

      # qemu/kernel (ioregionfd)
      host-image = nixos-generators.nixosGenerate {
        inherit pkgs;
        modules = [ (import ./nix/host-config.nix { 
          inherit pkgs;
          inherit (pkgs) lib; 
          inherit (self) config;
          extkern = false; 
        }) ];
        format = "qcow";
      };
      host-extkern-image = nixos-generators.nixosGenerate {
        inherit pkgs;
        modules = [ (import ./nix/host-config.nix { 
          inherit pkgs;
          inherit (pkgs) lib; 
          inherit (self) config;
          extkern = true; 
        }) ];
        format = "qcow";
      };
      guest-image = nixos-generators.nixosGenerate {
        inherit pkgs;
        modules = [ ./nix/guest-config.nix ];
        format = "qcow";
      };
    };

    devShells = {
      default = pkgs.mkShell {
        buildInputs = with pkgs; [
          just
          iperf2
          nixos-generators.packages.${system}.nixos-generators
        ];
      };
      # nix develop .#qemu
      qemu = pkgs.qemu.overrideAttrs (old: {
        buildInputs = [ pkgs.libndctl pkgs.libtasn1 ] ++ old.buildInputs;
        nativeBuildInputs = [ pkgs.meson pkgs.ninja ] ++ old.nativeBuildInputs;
        hardeningDisable = [ "stackprotector" ];
        shellHook = ''
          unset CPP # intereferes with dependency calculation
        '';
      });
    };
  })) // {
    nixosConfigurations = let
      pkgs = nixpkgs.legacyPackages.x86_64-linux;
    in {
      host-extkern = nixpkgs.lib.nixosSystem {
        system = "x86_64-linux";
        modules = [ (import ./nix/host-config.nix { 
          inherit pkgs;
          inherit (pkgs) lib; 
          inherit (self) config;
          extkern = true; 
        }) ];
      };
      # not bootable per se:
      #guest = nixpkgs.lib.nixosSystem {
      #  system = "x86_64-linux";
      #  modules = [
      #    ./nix/guest-config.nix
      #  ];
      #};
    };

  };
}
