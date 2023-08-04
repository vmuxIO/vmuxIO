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
  
  #when updating qemu, sync these with what is specified in qemu/subprojects/*.wrap
  libvfio-user-src = pkgs.fetchFromGitLab {
    owner = "qemu-project";
    repo = "libvfio-user";
    rev = "0b28d205572c80b568a1003db2c8f37ca333e4d7";
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

  version = "8.0.50";
  buildInputs = [ libndctl ] ++ old.buildInputs;
  nativeBuildInputs = [ json_c cmocka ] ++ old.nativeBuildInputs;
  hardeningDisable = [ "all" ];

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

  configureFlags = # old.configureFlags ++ 
  [ # old.configureFlags
  "--disable-dependency-tracking"
  # "--prefix=${out}"
  # "--bindir=${out}/bin"
  # "--sbindir=${out}/sbin"
  # "--includedir=${out}/include"
  # "--oldincludedir=${out}/include"
  # "--mandir=${out}/share/man"
  # "--infodir=${out}/share/info"
  # "--docdir=${out}/share/doc/qemu"
  # "--libdir=${out}/lib"
  # "--libexecdir=${out}/libexec"
  # "--localedir=${out}/share/locale"
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
  ] ++ [ "--enable-kvm" "--enable-vfio-user-server"];
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

  # preInstall = ''
  #   pwd
  #   ls
  #   echo $sourceRoot
  #   echo "stub" > qemu-kvm
  # '';
  preInstall = ''
    echo foobar $out
    mkdir -p $out
  '';
  postInstall = ''
    echo foobar
    pwd
    ls
    echo $out
    mkdir -p $out/bin
    ls $out
  '';
})
