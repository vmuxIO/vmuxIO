{}: {
  # enable gpio sysfs (breaks make/insmod kmods though because of CONFIG_EXPERT
  # for libvfio-user testing with simple gpio device
  boot.kernelModules = [
    "gpio-pci-idio-16"
  ];
  boot.kernelPatches = [
    {
      name = "enable gpio fs";
      patch = null;
      extraConfig = ''
        GPIOLIB y
        EXPERT y
        GPIO_SYSFS y
      '';
    }
  ];
}
