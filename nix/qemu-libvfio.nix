{ pkgs2411 }:
with pkgs2411;
qemu_full.overrideAttrs ( new: old: rec {
  # src = fetchFromGitHub {
  #   owner = "oracle";
  #   repo = "qemu";
  #   rev = "b3b53245edbd399eb3ba1655d509478c76d37a8e";
  #   hash = "sha256-kCX2ByuJxERLY2nHjPndVoo7TQm1j4qrpLjRcs42HU4=";
  #   fetchSubmodules = true;
  # };
  src = fetchFromGitHub {# based on jlevon/qemu master.vfio-user 23.01.2025
    owner = "vmuxio";
    repo = "qemu";
    rev = "cd7fccdd25f452d8b49d3e474a18425579dba4d8";
    hash = "sha256-GYW3teWlkPQRUMtYV1iVZbyjSUlwDln2XoWlsRlQJqw=";
    fetchSubmodules = true;
  };
  version = "7.1.5";

    #when updating qemu, sync these with what is specified in qemu/subprojects/*.wrap
  libvfio-user-src = pkgs.fetchFromGitLab {
    owner = "qemu-project";
    repo = "libvfio-user";
    rev = "0b28d205572c80b568a1003db2c8f37ca333e4d7"; # upstream master 09.06.2022
    hash = "sha256-V05nnJbz8Us28N7nXvQYbj66LO4WbVBm6EO+sCjhhG8=";
    fetchSubmodules = true;
  };
    dtc-src = pkgs.fetchFromGitLab {
    owner = "qemu-project";
    repo = "dtc";
    rev = "b6910bec11614980a21e46fbccc35934b671bd81";
    hash = "sha256-gx9LG3U9etWhPxm7Ox7rOu9X5272qGeHqZtOe68zFs4=";
    fetchSubmodules = true;
  };
  keycodemapdb-src = pkgs.fetchFromGitLab {
    owner = "qemu-project";
    repo = "keycodemapdb";
    rev = "f5772a62ec52591ff6870b7e8ef32482371f22c6";
    hash = "sha256-EQrnBAXQhllbVCHpOsgREzYGncMUPEIoWFGnjo+hrH4=";
    fetchSubmodules = true;
  };
  bsf3-src = pkgs.fetchFromGitLab {
    owner = "qemu-project";
    repo = "berkeley-softfloat-3";
    rev = "b64af41c3276f97f0e181920400ee056b9c88037";
    hash = "sha256-Yflpx+mjU8mD5biClNpdmon24EHg4aWBZszbOur5VEA=";
    fetchSubmodules = true;
  };
  btf3-src = pkgs.fetchFromGitLab {
    owner = "qemu-project";
    repo = "berkeley-testfloat-3";
    rev = "40619cbb3bf32872df8c53cc457039229428a263";
    hash = "sha256-EBz1uYnjehCtJqrSFzERH23N5ELZU3gGM26JnsGFcWg=";
    fetchSubmodules = true;
  };

  # emulate meson subproject dependency management
  postUnpack = ''
    cp -r ${libvfio-user-src} $sourceRoot/subprojects/libvfio-user
    chmod -R u+w $sourceRoot/subprojects/libvfio-user

    cp -r ${dtc-src} $sourceRoot/subprojects/dtc
    chmod -R u+w $sourceRoot/subprojects/dtc

    cp -r ${keycodemapdb-src} $sourceRoot/subprojects/keycodemapdb
    chmod -R u+w $sourceRoot/subprojects/keycodemapdb

    cp -r ${bsf3-src} $sourceRoot/subprojects/berkeley-softfloat-3
    chmod -R u+w $sourceRoot/subprojects/berkeley-softfloat-3
    cp $sourceRoot/subprojects/packagefiles/berkeley-softfloat-3/* $sourceRoot/subprojects/berkeley-softfloat-3

    cp -r ${btf3-src} $sourceRoot/subprojects/berkeley-testfloat-3
    chmod -R u+w $sourceRoot/subprojects/berkeley-testfloat-3
    cp $sourceRoot/subprojects/packagefiles/berkeley-testfloat-3/* $sourceRoot/subprojects/berkeley-testfloat-3
  '';


  buildInputs = [ libndctl ] ++ old.buildInputs;
  nativeBuildInputs = [ json_c cmocka ] ++ old.nativeBuildInputs;
  configureFlags = old.configureFlags ++ [
    "--enable-vfio-user-server"
    "--target-list=x86_64-softmmu"
    "--enable-debug"
    "--disable-alsa"
    "--disable-auth-pam"
    "--disable-bzip2"
    "--disable-cocoa"
    "--disable-coreaudio"
    "--disable-curses"
    "--disable-docs"
    "--disable-dsound"
    "--disable-gettext"
    "--disable-glusterfs"
    "--disable-gtk"
    "--disable-jack"
    "--disable-libusb"
    "--disable-lzfse"
    "--disable-oss"
    "--disable-pa"
    "--disable-rbd"
    "--disable-sdl"
    "--disable-sdl-image"
    "--disable-spice"
    "--disable-spice-protocol"
    "--disable-smartcard"
    "--disable-usb-redir"
    "--enable-virtfs"
    # "--disable-virtiofsd"
    "--disable-xen"
    "--disable-xen-pci-passthrough"
    "--disable-xkbcommon"
    "--disable-bsd-user"
    "--disable-libssh"
    "--disable-bochs"
    "--disable-cloop"
    "--disable-dmg"
    "--disable-qcow1"
    "--disable-vdi"
    "--disable-vvfat"
    "--disable-qed"
    "--disable-parallels"
  ] ++ [ "--enable-vfio-user-server"];
  patches = old.patches ++ [
    # ./print.patch
    # ./0001-qemu-hva2gpa.patch
    # ./0001-qemu-dma_read.patch
  ];
  NIX_CFLAGS_COMPILE = "-Wno-dangling-pointer"; # libvfio-user contains a dangling poitner
  hardeningDisable = [ "all" ];
})
