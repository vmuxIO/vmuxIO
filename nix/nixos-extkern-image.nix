{ flakepkgs, pkgs, ... }:
import (pkgs.path + "/nixos/lib/make-disk-image.nix") {
  config = (import (pkgs.path + "/nixos/lib/eval-config.nix") {
    inherit (pkgs) system;
    specialArgs = {
      inherit flakepkgs;
      extkern = true;
      nested = false;
      noiommu = false;
    };
    modules = [{
      imports = [ ./nixos-config-extkern.nix ];
    }];
  }).config;
  inherit pkgs;
  inherit (pkgs) lib;
  diskSize = 8 * 1024;
  partitionTableType = "none";
  # for a different format
  format = "qcow2";
}
