{ lib, ...}:
{
  boot.loader.grub.enable = false;
  boot.initrd.enable = false;
  # boot.isContainer = true;
  boot.loader.initScript.enable = true;
  boot.bootspec.enable = false;
  boot.kernel.enable = false;

  # fix qemu serial console
  console.enable = true;
  systemd.services."serial-getty@ttyS0".enable = true;
  #boot.growPartition = true; # doesnt seem to do anything

  # docker is broken with this
  virtualisation.docker.enable = lib.mkForce false;
}
