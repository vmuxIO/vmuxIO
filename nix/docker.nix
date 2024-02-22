{ pkgs, ... }: {
  virtualisation.docker.enable = true; # socket activated only
  environment.systemPackages = with pkgs; [
    docker-compose
  ];
}
