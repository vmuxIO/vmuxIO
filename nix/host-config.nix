{ flakepkgs, lib, pkgs, 
extkern ? false, # whether to use externally, manually built kernel
nested ? false, # whether this is the config variant for the nested guest
noiommu ? false, # whether to disable iommu via cmdline and kernel config
... }:
lib.attrsets.recursiveUpdate ({
  #networking.useDHCP = false;
  #networking.interfaces.eth0.useDHCP = false;
  #networking.defaultGateway = "192.168.56.1";
  #networking.nameservers = [ "10.156.33.53" ];
  networking.hostName = (if !nested then "host" else "guest");
  #networking.domain = "gierens.de";
  #networking.bridges = {
  #  "br0" = {
  #    interfaces = [ "eth0" ];
  #  };
  #};
  #networking.interfaces.br0.useDHCP = false;
  #networking.interfaces.br0.ipv4.addresses = [ {
  #  address = "192.168.56.10";
  #  prefixLength = 24;
  #} ];

  imports = [
    ./docker.nix
    ./gpio.nix # enable gpio sysfs
    ./desperately-fixing-overlayfs-for-extkern.nix
    ({ config, ...}: {
      boot.extraModulePackages = [ config.boot.kernelPackages.dpdk-kmods ];
      boot.kernelModules = lib.lists.optionals nested [ "igb_uio" ];
    })
  ];

  services.sshd.enable = true;
  programs.direnv.enable = true;

  networking.firewall.enable = false;
  # networking.firewall.allowedTCPPorts = [22];

  users.users.root.password = "password";
  services.openssh.settings.PermitRootLogin = lib.mkDefault "yes";
  users.users.root.openssh.authorizedKeys.keys = [
    (builtins.readFile ./ssh_key.pub)
  ];
  services.getty.autologinUser = lib.mkDefault "root";

  fileSystems."/root" = {
    device = "home";
    fsType = "9p";
    options = [ "trans=virtio" "nofail" "msize=104857600" ];
  };

  # mount host nix store, but use overlay fs to make it writeable
  fileSystems."/nix/.ro-store-vmux" = {
    device = "myNixStore";
    fsType = "9p";
    options = [ "ro" "trans=virtio" "nofail" "msize=104857600" ];
    neededForBoot = true;
  };
  fileSystems."/nix/store" = {
    device = "overlay";
    fsType = "overlay";
    options = [ 
      "lowerdir=/nix/.ro-store-vmux"
      "upperdir=/nix/.rw-store/store"
      "workdir=/nix/.rw-store/work"
    ];
    neededForBoot = true;
    depends = [ 
      "/nix/.ro-store-vmux" 
      "/nix/.rw-store/store"
      "/nix/.rw-store/work"
    ];
  };
  boot.initrd.availableKernelModules = [ "overlay" ];

  time.timeZone = "Europe/Berlin";
  i18n.defaultLocale = "en_US.UTF-8";
  console = {
    font = "Lat2-Terminus16";
    keyMap = "us";
  };

  system.activationScripts = {
    linkHome = {
      text = ''
        # dectivate, because it can fail if link exists:
        #ln -s /mnt /home/gierens
      '';
      deps = [];
    };
  };

  system.stateVersion = "22.05";

  nix.extraOptions = ''
    experimental-features = nix-command flakes
  '';
  nix.package = pkgs.nixFlakes;
  environment.systemPackages = with pkgs; [
    kmod
    git
    gnumake
    # pixman
    # glib
    # libepoxy
    # epoxy
    # snappy
    # spice
    # SDL2
    # virglrenderer
    # vde2
    # liburing
    # ninja
    # pkgconfig
    qemu
    htop
    tmux
    tunctl
    bridge-utils
    killall
    gdb
    iperf
    fio
    pciutils
    just
    python3
    ioport # access port io (pio) via inb and outw commands
    busybox # for devmem to access physical memory
    (writeScriptBin "devmem" ''
      ${busybox}/bin/devmem $@
    '')
    ethtool
    bpftrace
    flakepkgs.devShellGcRoot
  ];

  hardware.firmware = [ flakepkgs.linux-firmware-pinned ];

  # this breaks make/insmod kmods though:
  boot.extraModprobeConfig = ''
    blacklist ice
    blacklist ixgbe
    blacklist e1000
    blacklist e1000e
  '';
  boot.kernelPatches = [
    {
      name = "enable-debug-symbols";
      patch = null;
      extraConfig = ''
        DEBUG_INFO y
      '';
    }
    {
      name = "enable-iommu-debugfs";
      patch = null;
      extraConfig = ''
        IOMMU_DEBUGFS y
        INTEL_IOMMU_DEBUGFS y
      '';
    }
    {
      name = "fix-bpf-tools";
      patch = null;
      extraConfig = ''
        IKHEADERS y
      '';
    }
    #{
    #  name = "ixgbe-use-vmux-capability-offset-instead-of-hardware";
    #  patch = ./0001-ixgbe-vmux-capa.patch;
    #}
  ] ++ lib.lists.optionals noiommu [
    {
      name = "enable-vfio-noiommu";
      patch = null;
      extraConfig = ''
        VFIO_NOIOMMU y
      '';
    }
  ];

  #boot.kernelPackages = let
  #  linux_ioregfd_pkg = { fetchurl, buildLinux, ... } @ args:

  #    buildLinux (args // rec {
  #      version = "5.12.14-ioregionfd";
  #      modDirVersion = "5.12.14";

  #      #src = ./linux;
  #      src = fetchurl {
  #        url = "https://github.com/vmuxIO/linux/archive/refs/tags/v5.12.14-ioregionfd.tar.gz";
  #        sha256 = "3fe587a240c8d29a1bae73d27ccfb7dc332f7bf716e48dbdbabffd05f090481c";
  #      };
  #      kernelPatches = [{
  #        name = "enable-debug-symbols";
  #        patch = null;
  #        extraConfig = ''
  #          DEBUG_INFO y
  #        '';
  #      } {
  #        name = "build-kvm-into-base-kernel";
  #        patch = null;
  #        extraConfig = ''
  #          KVM y
  #        '';
  #      } {
  #        name = "enable-kvm-ioregionfd";
  #        patch = null;
  #        extraConfig = ''
  #          KVM_IOREGION y
  #        '';
  #      # } {
  #      #   name = "remove-useless-stuff";
  #      #   patch = null;
  #      #   extraConfig = ''
  #      #     USB n
  #      #     WLAN n
  #      #   '';
  #      } ];

  #      extraMeta.branch = "5.12";
  #      ignoreConfigErrors = true;
  #    } // (args.argsOverride or {}));
  #  linux_ioregfd = pkgs.callPackage linux_ioregfd_pkg{};
  #in
  #  pkgs.recurseIntoAttrs (pkgs.linuxPackagesFor linux_ioregfd);

  boot.kernelParams = [ 
    "nokaslr"
    "debug"
  ] ++ lib.lists.optionals (!noiommu) [
    "intel_iommu=on"
    "iommu=pt"
  ] ++ lib.lists.optionals (nested && noiommu) [
    "intel_iommu=off"
    "vfio.enable_unsafe_noiommu_mode=1"
    "vfio-pci.ids=8086:100e"
  ] ++ lib.lists.optionals (!nested) [
    "default_hugepagesz=1G"
    "hugepagez=1G"
    "hugepages=8"
  ];

  boot.kernelModules = lib.lists.optionals nested ["vfio" "vfio-pci"];

})
# merge the following with the previous. See recursiveUpdate above. 
( lib.optionalAttrs extkern {
  # incremental build section
  boot.loader.grub.enable = false;
  boot.initrd.enable = false;
  boot.isContainer = true;
  boot.loader.initScript.enable = true;

  # fix qemu serial console
  console.enable = true;
  systemd.services."serial-getty@ttyS0".enable = true;
  #boot.growPartition = true; # doesnt seem to do anything
})
