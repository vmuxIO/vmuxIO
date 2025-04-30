{ fetchFromGitHub
, pkgs
, flakepkgs
}:
let 
  srcpack = {
    vmux = ../.;
    libvfio-user = fetchFromGitHub {
      owner = "vmuxIO";
      repo = "libvfio-user";
      rev = "7d48e69b29c46a682d069efc1c74bfaa981f39c0"; # vmux branch 14.09.22
      #fetchSubmodules = true;
      sha256 = "sha256-47Jm8fvixg3Hw3c/qpaWr0qGPXfET4LrTQOD8ccUByI=";
    };
    # make this the same version as for moongen21?!
    dpdk = fetchFromGitHub {
      owner = "vmuxIO";
      repo = "dpdk";
      rev = "220a3ff526a43567cc24e74ef3d7b0a776aa9b6a"; # 21.11-moon-mux
      fetchSubmodules = true;
      sha256 = "sha256-9bT1eQvjw87JjVc05Eia8CRVACEfcQf9a3JDrMy4GUg=";
    };
    nic-emu = flakepkgs.libnic-emu.src;
  };
in
pkgs.clangStdenv.mkDerivation {
  pname = "vmux"; # TODO moongen -> vmux everything below this line
  version = "2022.06.27";

  src = srcpack.vmux;

  postUnpack = ''
    rm -r $sourceRoot/subprojects/libvfio-user || true
    cp -r ${srcpack.libvfio-user} $sourceRoot/subprojects/libvfio-user
    chmod -R u+w $sourceRoot/subprojects/libvfio-user

    # not actually used, but meson will complain if its not there
    rm -r $sourceRoot/subprojects/nic-emu || true
    cp -r ${srcpack.nic-emu} $sourceRoot/subprojects/nic-emu
    chmod -R u+w $sourceRoot/subprojects/nic-emu

    # we build libnic-emu artifacts in another package and use dont_build_libnic_emu=true
    cp ${flakepkgs.libnic-emu}/lib/libnic_emu.a $sourceRoot/
    cp ${flakepkgs.libnic-emu}/lib/include/* $sourceRoot/src
  '';

  nativeBuildInputs = with pkgs; [
    cmake
    ninja
    meson
    openssl
    python3Packages.pyelftools
    (writeShellApplication {
      name = "git";
      text = ''
        echo ignoring git command
      '';
    })

    pkg-config

    # dependencies for nic-emu
    rustc
    cargo
  ];
  buildInputs = with pkgs; [
    openssl
    tbb
    libbsd
    numactl
    luajit
    libpcap
    (boost.override { enableStatic = true; enableShared = false; })
    flakepkgs.dpdk23

    json_c
    cmocka
    flakepkgs.libnic-emu
  ];

  hardeningDisable = [ "all" ];

  configurePhase = ''
    meson build -Ddont_build_libnic_emu=true -Dbuildtype=release
  '';
  buildPhase = ''
    meson compile -C build
  '';

  installPhase = ''
    mkdir -p $out/bin
    cp build/vmux $out/bin/

    # we now link libvfio-user statically
    # mkdir -p $out/lib
    # cp build/subprojects/libvfio-user/lib/libvfio-user.so $out/lib
    # cp build/subprojects/libvfio-user/lib/libvfio-user.so.0 $out/lib
    # cp build/subprojects/libvfio-user/lib/libvfio-user.so.0.0.1 $out/lib

    patchelf --shrink-rpath --allowed-rpath-prefixes /nix/store $out/bin/vmux
    patchelf --add-rpath $out/lib $out/bin/vmux
    '';

    dontFixup = true;
}
