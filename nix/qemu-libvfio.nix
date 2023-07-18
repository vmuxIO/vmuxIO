{ pkgs, pkgs2211, nixpkgs }:
# with pkgs2211;
with pkgs;
qemu_full.overrideAttrs ( new: old: rec {
  src = fetchFromGitHub {
    owner = "oracle";
    repo = "qemu";
    rev = "ed8ad9728a9c0eec34db9dff61dfa2f1dd625637"; # main 18.07.2023
    hash = "sha256-kGNJ1yt44L6y/kw9YFJjbyQQ1/hzoI+tuun8p7ObnqM=";
    fetchSubmodules = true;
  };
  version = "8.0.50";
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
  patchPath = "${nixpkgs.outPath}/pkgs/applications/virtualization/qemu";
  patches = # old.patches ++ 
    [
      "${patchPath}/fix-qemu-ga.patch"
      # we omit macos patches and one fetchpatch for nested virt
    ] ++
    lib.optionals (lib.versionOlder version "8.0.0") [
      ./print.patch
      ./0001-qemu-hva2gpa.patch
      ./0001-qemu-dma_read.patch
    ];
})
