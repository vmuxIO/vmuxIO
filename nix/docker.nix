{ pkgs, lib, ... }: {
  virtualisation.docker.enable = lib.mkDefault true; # socket activated only
  networking.nftables.enable = true;
  environment.systemPackages = with pkgs; [
    docker-compose
  ];
}
