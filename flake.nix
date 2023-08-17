{
  description = "vMux - flexible and fast software-multiplexing of devices for VMs";

  nixConfig.extra-substituters = [
    "https://cache.garnix.io"
  ];

  nixConfig.extra-trusted-public-keys = [
    "cache.garnix.io:CTFPyKSLcx5RMJKfLo5EEPUObbA78b0YQ2DTCJXqr9g="
  ];

  inputs = {
    nixpkgs.url = github:NixOS/nixpkgs/nixos-unstable;
    nixpkgs-2211.url = github:NixOS/nixpkgs/nixos-22.11;
    nixpkgs-2111.url = github:NixOS/nixpkgs/nixos-21.11;

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

    xdp-reflector-src = {
      url = "git+https://github.com/gierens/xdp-reflector?ref=main&submodules=1";
      flake = false;
    };
    qemu-ioregionfd-src = {
      url = "git+https://github.com/vmuxIO/qemu.git?ref=ioregionfd&submodules=1";
      flake = false;
    };
  };

  outputs = {
    self,
    nixpkgs,
    flake-utils,
    nixos-generators,
    ...
  } @ args: let
  in
  (flake-utils.lib.eachSystem ["x86_64-linux"] (system:
  let
    pkgs = nixpkgs.legacyPackages.${system};
    pkgs2211 = args.nixpkgs-2211.legacyPackages.${system};
    pkgs2111 = args.nixpkgs-2111.legacyPackages.${system};
    flakepkgs = self.packages.${system};
    mydpdk = pkgs.callPackage ./nix/dpdk.nix {
      kernel = pkgs.linuxPackages_5_10.kernel;
    };
    selfpkgs = self.packages.${system};
    # make-disk-image = import (pkgs.path + "/nixos/lib/make-disk-image.nix");
    make-disk-image = import (./nix/make-disk-image.nix);
    # eval-config-config = args: (import (pkgs.path + "/nixos/lib/eval-config.nix") args).config;
  in  {
    packages = {
      default = selfpkgs.vmux;

      vmux = pkgs.callPackage ./nix/vmux.nix {};

      # moongen/dpdk
      moongen = pkgs.callPackage ./nix/moongen.nix {
        linux = pkgs.linuxPackages_5_10.kernel;
        inherit (flakepkgs) linux-firmware-pinned;
      };
      moongen21 = pkgs.callPackage ./nix/moongen21.nix {
        linux = pkgs.linuxPackages_5_10.kernel;
        inherit (flakepkgs) linux-firmware-pinned;
        inherit self;
      };
      moongen-lachnit = pkgs.callPackage ./nix/moongen-lachnit.nix {
        linux = pkgs.linuxPackages_5_10.kernel;
        inherit (flakepkgs) linux-firmware-pinned;
        inherit self;
      };
      dpdk = mydpdk;
      pktgen = pkgs.callPackage ./nix/pktgen.nix {
        dpdk = mydpdk;
      };

      # util
      xdp-reflector = pkgs.callPackage ./nix/xdp-reflector.nix {
        inherit pkgs;
        inherit (self.inputs) xdp-reflector-src;
      };
      linux-firmware-pinned = (pkgs.linux-firmware.overrideAttrs (old: new: {
        src = fetchGit {
          url = "git://git.kernel.org/pub/scm/linux/kernel/git/firmware/linux-firmware.git";
          ref = "main";
          rev = "8a2d811764e7fcc9e2862549f91487770b70563b";
        };
        version = "8a2d81";
        outputHash = "sha256-dVvfwgto9Pgpkukf/IoJ298MUYzcsV1G/0jTxVcdFGw=";
      }));
      kmod-tools = pkgs.callPackage ./nix/kmod-tools.nix {
        inherit pkgs;
      };

      #patched qemu
      qemu = pkgs.callPackage ./nix/qemu-libvfio.nix { 
        # needs a nixpkgs with qemu ~7.1.0 for patches to apply.
        inherit pkgs2211;
      };

      qemu-ioregionfd = pkgs2211.qemu.overrideAttrs ( new: old: {
        src = self.inputs.qemu-ioregionfd-src;
        version = "6.2.0-rc4+";
        # minimal needed: ./configure --meson=meson --enable-multiprocess --enable-ioregionfd --target-list=x86_64-softmmu
        configureFlags = old.configureFlags ++ [
          "--enable-ioregionfd"
          "--target-list=x86_64-softmmu"
          "--disable-virtiofsd"
          "--disable-gtk"
          "--disable-sdl"
          "--disable-sdl-image"
        ];
        # use patches from old nixpkgs that used similar qemu version (6.1)
        # only pick first 3 patches. Others are CVE fixes which fail to apply
        patches = pkgs.lib.lists.sublist 0 3 pkgs2111.qemu.patches;
      });

      devShellGcRoot = pkgs.writeShellApplication {
        name = "stub_app";
        runtimeInputs = self.outputs.devShells.${system}.default.buildInputs;
        text = ''
          echo "This app does not actually do anything. It just makes sure all packages for the dev shell are already loaded in the store."
          # Actually we should not do this workaround but place an actual garbage
          # collection root for the dev shell (also including compilers etc), but
          # i don't know how to do so. 
        '';
      };

      # linux kernel build shells:
      kernel-deps = pkgs.callPackage ./nix/kernel-deps.nix {};
      kernel-deps-shell = (pkgs.callPackage ./nix/kernel-deps.nix {
        runScript = "bash";
      });

      # qemu/kernel (ioregionfd)
      nesting-host-image = make-disk-image {
        config = self.nixosConfigurations.host.config;
        inherit (pkgs) lib;
        inherit pkgs;
        format = "qcow2";
      };
      nesting-host-extkern-image = make-disk-image {
        config = self.nixosConfigurations.host-extkern.config;
        inherit (pkgs) lib;
        inherit pkgs;
        partitionTableType = "none";
        format = "qcow2";
        additionalSpace = "8G";
      };
      nesting-guest-image = make-disk-image {
        config = self.nixosConfigurations.guest.config;
        inherit (pkgs) lib;
        inherit pkgs;
        format = "qcow2";
      };
      nesting-guest-image-noiommu = make-disk-image {
        config = self.nixosConfigurations.guest-noiommu.config;
        inherit (pkgs) lib;
        inherit pkgs;
        format = "qcow2";
      };
      # used by autotest
      guest-image = nixos-generators.nixosGenerate {
        inherit pkgs;
        modules = [ ./nix/guest-config.nix ];
        specialArgs = {
          inherit (flakepkgs) linux-firmware-pinned;
        };
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
        (writeScriptBin "devmem" ''
          ${busybox}/bin/devmem $@
        '')
        bridge-utils
        self.packages.x86_64-linux.kmod-tools

        # dependencies for hosts/prepare.py
        python310.pkgs.pyyaml
        yq
        # not available in 22.05 yet
        # python310.pkgs.types-pyyaml
        ethtool

        # deps for tests/autotest
        python310.pkgs.colorlog
      ] ++ (with self.packages; [
        dpdk
        #self.packages.x86_64-linux.qemu
        qemu # nixpkgs vanilla qemu
      ]);
    in {
      # use clang over gcc because it has __builtin_dump_struct()
      default = pkgs.clangStdenv.mkDerivation {
        name = "clang-devshell";
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

        # prevent clangStdenv from overriding the fixed clang-tools binaries from nixos
        shellHook = ''
          PATH="${pkgs.clang-tools}/bin:$PATH"
        '';

        # stub stuff to statisfy the build
        src = ./LICENSE; 
        dontUnpack = true;
        dontPatch = true;
        dontConfigure = true;
        dontBuild = true;
        installPhase = ''
          mkdir $out
          touch $out/keepdir
        '';
        dontFixup = true;
      };
      # nix develop .#default_old
      default_old = pkgs.mkShell {
        buildInputs = with pkgs; [
        ] ++ common_deps;
        CXXFLAGS = "-std=gnu++14"; # libmoon->highwayhash->tbb needs <c++17
      };
      # nix develop .#qemu-dev
      qemu-dev = pkgs2211.qemu.overrideAttrs (old: {
        buildInputs = [ pkgs.libndctl pkgs.libtasn1 ] ++ old.buildInputs;
        nativeBuildInputs = [ pkgs.meson pkgs.ninja ] ++ old.nativeBuildInputs;
        hardeningDisable = [ "all" ]; # [ "stackprotector" ];
        shellHook = ''
          unset CPP # intereferes with dependency calculation
        '';
      });
      host-kernel = self.nixosConfigurations.host.config.boot.kernelPackages.kernel.overrideAttrs (finalAttrs: previousAttrs: {
        KERNELDIR="${self.nixosConfigurations.host.config.boot.kernelPackages.kernel.dev}";
      });
    };
  })) // {
    nixosConfigurations = let
      pkgs = nixpkgs.legacyPackages.x86_64-linux;
      selfpkgs = self.packages.x86_64-linux;
      flakepkgs = self.packages.x86_64-linux;
    in {
      host = nixpkgs.lib.nixosSystem {
        system = "x86_64-linux";
        modules = [ (import ./nix/host-config.nix {
            inherit pkgs;
            inherit (pkgs) lib;
            inherit flakepkgs;
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
          inherit flakepkgs;
          extkern = true;
        }) ];
      };
      guest = nixpkgs.lib.nixosSystem {
        system = "x86_64-linux";
        modules = [ (import ./nix/host-config.nix {
            inherit pkgs;
            inherit (pkgs) lib;
            inherit flakepkgs;
            nested = true;
          }) 
          ./nix/nixos-generators-qcow.nix
        ];
      };
      guest-noiommu = nixpkgs.lib.nixosSystem {
        system = "x86_64-linux";
        modules = [ (import ./nix/host-config.nix {
            inherit pkgs;
            inherit (pkgs) lib;
            inherit flakepkgs;
            nested = true;
            noiommu = true;
          }) 
          ./nix/nixos-generators-qcow.nix
        ];
      };
    };
  };
}
