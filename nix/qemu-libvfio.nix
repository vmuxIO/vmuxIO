{ pkgs2211 }:
with pkgs2211;
qemu_full.overrideAttrs ( new: old: {
  src = fetchFromGitHub {
    owner = "oracle";
    repo = "qemu";
    rev = "b3b53245edbd399eb3ba1655d509478c76d37a8e";
    hash = "sha256-kCX2ByuJxERLY2nHjPndVoo7TQm1j4qrpLjRcs42HU4=";
    fetchSubmodules = true;
  };
  version = "7.1.5";
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
    "--disable-virtiofsd"
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
    ./print.patch
    ./0001-qemu-hva2gpa.patch
    ./0001-qemu-dma_read.patch
  ];
})
