{ 
  boot.postBootCommands = ''
  echo foobar
  # this mkdir actually works
  mkdir -p /nix/.rw-store/store
  mkdir -p /nix/.rw-store/work
  mount -a
  '';

  system.userActivationScripts = {
    fixMounts = {
      text = ''
        mkdir -p /nix/.rw-store/store
        mkdir -p /nix/.rw-store/work
        mount overlay
      '';
      deps = [];
    };
  };
}

