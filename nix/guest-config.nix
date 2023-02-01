{ config, lib, pkgs, modulesPath, ... }:
{
  networking.useDHCP = false;
  networking.interfaces.eth0.useDHCP = false;
  networking.interfaces.eth0.ipv4.addresses = [ {
    address = "192.168.56.20";
    prefixLength = 24;
  } ];
  networking.interfaces.eth1.useDHCP = false;
  networking.defaultGateway = "192.168.56.1";
  networking.nameservers = [ "10.156.33.53" ];
  networking.hostName = "guest";
  networking.domain = "vmux.dse.in.tum.de";

  services.sshd.enable = true;

  networking.firewall.enable = false;
  # networking.firewall.allowedTCPPorts = [22];

  users.users.root.password = "ach2Chai8muo";
  users.users.root.openssh.authorizedKeys.keys = [
    "ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIBevyJ5i0237DNoS29F9aii2AJwrSxXNz3hP61hWXfRl sandro@reaper.gierens.de"
  ];
  services.openssh.permitRootLogin = lib.mkDefault "yes";
  services.getty.autologinUser = lib.mkDefault "root";

  # users.extraUsers.gierens = {
  #   isNormalUser = true;
  #   home = "/home/gierens";
  #   extraGroups = [ "gierens" "sudo" ];
  # };

  fileSystems."/home/gierens" = {
    device = "home";
    fsType = "9p";
    options = [ "trans=virtio" "nofail" "msize=104857600" ];
  };

  fileSystems."/nix/store" = {
    device = "nixstore";
    fsType = "9p";
    options = [ "trans=virtio" "nofail" "msize=104857600" ];
  };

  time.timeZone = "Europe/Berlin";
  i18n.defaultLocale = "en_US.UTF-8";
  console = {
    font = "Lat2-Terminus16";
    keyMap = "us";
  };

  system.stateVersion = "22.05";

  nix.extraOptions = ''
    experimental-features = nix-command flakes
  '';
  nix.package = pkgs.nixFlakes;
  environment.systemPackages = with pkgs; [
    git
    # epoxy
    # snappy
    # spice
    # SDL2
    # virglrenderer
    # vde2
    # liburing
    # ninja
    htop
    tmux
    busybox
    iperf
    dpdk
    ethtool
    stress
  ];

  # boot.kernelPackages = let
  #   linux_ioregfd_pkg = { fetchurl, buildLinux, ... } @ args:

  #     buildLinux (args // rec {
  #       version = "5.12.14-ioregionfd";
  #       modDirVersion = "5.12.14";

  #       src = fetchurl {
  #         url = "https://github.com/vmuxIO/linux/archive/refs/tags/v5.12.14-ioregionfd.tar.gz";
  #         sha256 = "3fe587a240c8d29a1bae73d27ccfb7dc332f7bf716e48dbdbabffd05f090481c";
  #       };
  #       kernelPatches = [{
  #         name = "enable-debug-symbols";
  #         patch = null;
  #         extraConfig = ''
  #           DEBUG_INFO y
  #         '';
  #       } {
  #         name = "build-kvm-into-base-kernel";
  #         patch = null;
  #         extraConfig = ''
  #           KVM y
  #         '';
  #       } {
  #         name = "enable-kvm-ioregionfd";
  #         patch = null;
  #         extraConfig = ''
  #           KVM_IOREGION y
  #         '';
  #       } {
  #         name = "remove-useless-stuff";
  #         patch = null;
  #         extraConfig = ''
  #           CONFIG_USB n
  #           CONFIG_WLAN n
  #         '';
  #       } ];

  #       extraMeta.branch = "5.12";
  #       ignoreConfigErrors = true;
  #     } // (args.argsOverride or {}));
  #   linux_ioregfd = pkgs.callPackage linux_ioregfd_pkg{};
  # in
  #   pkgs.recurseIntoAttrs (pkgs.linuxPackagesFor linux_ioregfd);
  # boot.kernelPatches = [ {
  #   name = "devmem-config";
  #   patch = null;
  #   extraConfig = ''
  #     STRICT_DEVMEM n
  #   '';
  # } {
  #   name = "remove-useless-stuff";
  #   patch = null;
  #   extraConfig = ''
  #     USB n
  #     WLAN n
  #   '';
  # } ];

  boot.kernelParams = [
    "nokaslr"
    "iomem=relaxed"
    # spdk/dpdk hugepages
    "default_hugepagesz=2MB"
    "hugepagesz=2MB"
    "hugepages=1000"
  ];
  boot.extraModulePackages = [
    config.boot.kernelPackages.dpdk-kmods
  ];
  boot.kernelModules = ["igb_uio"];

  # system.activationScripts = {
  #   linkHome = {
  #     text = ''
  #       ln -s /mnt /home/gierens
  #     '';
  #     deps = [];
  #   };
  # };
}
