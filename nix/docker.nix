{ pkgs, lib, ... }: {
  virtualisation.docker.enable = lib.mkDefault true; # socket activated only
  environment.systemPackages = with pkgs; [
    docker-compose
  ];
}
