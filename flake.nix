{
  description = "A very basic flake";

  inputs = {
    nixpkgs.url = github:NixOS/nixpkgs/nixos-unstable;
    nixpkgs-stable.url = github:NixOS/nixpkgs/nixos-22.11;

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

    moongen-lachnit-src = {
      url = "git+https://public-access:glpat-G8kFYA45GcDP-oR-oyDj@gitlab.lrz.de/okelmann/moongen-lachnit.git?ref=master&submodules=1";
      flake = false;
    };
    libmoon-lachnit-src = {
      #url = "git+file:///home/okelmann/idp-lachnit/moongen/libmoon";
      url = "git+https://public-access:glpat-xnmZ-yizTjswVRBsjtDS@gitlab.lrz.de/okelmann/libmoon-lachnit.git?ref=dpdk-21.11&submodules=1";
      flake = false;
    };
    dpdk-lachnit-src = {
      url = "git+https://public-access:glpat-ye-ZjvZJzssBRhYmoemC@gitlab.lrz.de/okelmann/dpdk-lachnit.git?ref=v21.11-libmoon&submodules=1";
      flake = false;
    };

    xdp-reflector = {
      url = "git+https://github.com/gierens/xdp-reflector?ref=main&submodules=1";
      flake = false;
    };
  };

  outputs = {
    self,
    nixpkgs,
    nixpkgs-stable,
    flake-utils,
    nixos-generators,
    ...
  }: let
  in
  (flake-utils.lib.eachSystem ["x86_64-linux"] (system:
  let
    pkgs = nixpkgs.legacyPackages.${system};
    pkgs-stable = nixpkgs-stable.legacyPackages.${system};
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
        inherit self;
      };
      moongen-lachnit = pkgs.callPackage ./nix/moongen-lachnit.nix {
        linux = pkgs.linuxPackages_5_10.kernel;
        inherit self;
      };
      # broken build
      dpdk = mydpdk;
      # broken build
      pktgen = pkgs.callPackage ./nix/pktgen.nix {
        dpdk = mydpdk;
      };

      # util
      xdp-reflector = pkgs.callPackage ./nix/xdp-reflector.nix {
        inherit self pkgs;
      };

      #patched qemu
      qemu = pkgs.callPackage ./nix/qemu-libvfio.nix { 
        # needs a nixpkgs with qemu ~7.1.0 for patches to apply.
        pkgs2211 = pkgs-stable;
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

    devShells = let 
      common_deps = with pkgs; [
        just
        iperf2
        nixos-generators.packages.${system}.nixos-generate
        ccls # c lang serv
        meson
        ninja
        python310.pkgs.mypy # python static typing
        gdb

        # dependencies for hosts/prepare.py
        python310.pkgs.pyyaml
        yq
        # not available in 22.05 yet
        # python310.pkgs.types-pyyaml
        ethtool
      ] ++ (with self.packages; [
        dpdk
        qemu
      ]);
    in {
      # use clang over gcc because it has __builtin_dump_struct()
      default = pkgs.clangStdenv.mkDerivation {
        name = "clang-devshell";
        src = ./LICENSE; # stub file to statisfy the build
        dontUnpack = true;
        dontPatch = true;
        dontConfigure = true;
        dontBuild = true;
        installPhase = ''
          mkdir $out
          touch $out/keepdir
        '';
        dontFixup = true;
        buildInputs = with pkgs; [
          # dependencies for libvfio-user
          meson
          ninja
          cmake
          json_c
          cmocka
          pkg-config
        ] ++ common_deps;
        hardeningDisable = [ "all" ];
      };
      # nix develop .#default_old
      default_old = pkgs.mkShell {
        buildInputs = with pkgs; [
        ] ++ common_deps;
        CXXFLAGS = "-std=gnu++14"; # libmoon->highwayhash->tbb needs <c++17
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
      host = nixpkgs.lib.nixosSystem {
        system = "x86_64-linux";
        modules = [ (import ./nix/host-config.nix {
            inherit pkgs;
            inherit (pkgs) lib;
            inherit (self) config;
            extkern = false;
          })
          ./nix/nixos-generators-qcow.nix
        ];
      };
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
