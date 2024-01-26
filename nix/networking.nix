{ config, lib, pkgs, ... }: { 
  options = {
    networking.vm_number = lib.mkOption {
      default = 0;
      example = 13;
      description = "Number/ID of the VM. Used to set IP etc.";
    };
  };

  config = 
  let
    number = config.networking.vm_number;
    number-str = if number == 0 then "" else builtins.toString (number + 1);
    ip = "192.168.56.${builtins.toString (20 + number)}";
  in {
    networking.useDHCP = false;
    networking.interfaces.eth0.useDHCP = false;
    networking.interfaces.eth0.ipv4.addresses = [ {
      address = ip;
      prefixLength = 24;
    } ];
    networking.interfaces.eth1.useDHCP = false;
    networking.defaultGateway = "192.168.56.1";
    networking.nameservers = [ "10.156.33.53" ];
    networking.hostName = "guest${number-str}";
    networking.domain = "vmux${number-str}.dse.in.tum.de";
  };
}
