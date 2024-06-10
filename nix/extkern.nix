{...}:
{
  boot.loader.grub.enable = false;
  boot.initrd.enable = false;
  boot.isContainer = true;
  boot.loader.initScript.enable = true;

  # fix qemu serial console
  console.enable = true;
  systemd.services."serial-getty@ttyS0".enable = true;
  #boot.growPartition = true; # doesnt seem to do anything
}
