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
  configureFlags = # old.configureFlags ++ 
  [ # old.configureFlags
  "--disable-dependency-tracking"
  "--prefix=/nix/store/52x85sa3gcl481gxcbx27bgl22avimz4-qemu-8.0.50"
  "--bindir=/nix/store/52x85sa3gcl481gxcbx27bgl22avimz4-qemu-8.0.50/bin"
  "--sbindir=/nix/store/52x85sa3gcl481gxcbx27bgl22avimz4-qemu-8.0.50/sbin"
  "--includedir=/nix/store/52x85sa3gcl481gxcbx27bgl22avimz4-qemu-8.0.50/include"
  "--oldincludedir=/nix/store/52x85sa3gcl481gxcbx27bgl22avimz4-qemu-8.0.50/include"
  "--mandir=/nix/store/52x85sa3gcl481gxcbx27bgl22avimz4-qemu-8.0.50/share/man"
  "--infodir=/nix/store/52x85sa3gcl481gxcbx27bgl22avimz4-qemu-8.0.50/share/info"
  "--docdir=/nix/store/52x85sa3gcl481gxcbx27bgl22avimz4-qemu-8.0.50/share/doc/qemu"
  "--libdir=/nix/store/52x85sa3gcl481gxcbx27bgl22avimz4-qemu-8.0.50/lib"
  "--libexecdir=/nix/store/52x85sa3gcl481gxcbx27bgl22avimz4-qemu-8.0.50/libexec"
  "--localedir=/nix/store/52x85sa3gcl481gxcbx27bgl22avimz4-qemu-8.0.50/share/locale"
  "--disable-strip"
  "--enable-docs"
  "--enable-tools"
  "--localstatedir=/var"
  "--sysconfdir=/etc"
  # "--meson=meson" # no worky
  "--cross-prefix="
  "--enable-guest-agent"
  "--enable-numa"
  "--enable-seccomp"
  "--enable-smartcard"
  "--enable-spice"
  "--enable-usb-redir"
  "--enable-linux-aio"
  "--enable-gtk"
  "--enable-rbd"
  "--enable-glusterfs"
  "--enable-opengl"
  "--enable-virglrenderer"
  "--enable-tpm"
  "--enable-libiscsi"
  "--smbd=/nix/store/jlawj526gvigij42b4qc5iq0wjdjrj5d-samba-4.17.7/bin/smbd"
  "--enable-linux-io-uring"
  "--enable-capstone"
  ] ++
  [
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
    # "--disable-virtiofsd" # no worky
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
