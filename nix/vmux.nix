{ fetchFromGitHub
, pkgs
, libnic-emu
}:
let 
  srcpack = {
    vmux = ../.;
    libvfio-user = fetchFromGitHub {
      owner = "vmuxIO";
      repo = "libvfio-user";
      rev = "9a5124df49efed644f0db7e573ebd0367df480cd"; # vmux branch 14.09.22
      #fetchSubmodules = true;
      sha256 = "sha256-v48G4GUzdHwdzZcpAjbZG3rutYR9rLXBqvx4clgB7kw=";
    };
    # make this the same version as for moongen21?!
    dpdk = fetchFromGitHub {
      owner = "vmuxIO";
      repo = "dpdk";
      rev = "220a3ff526a43567cc24e74ef3d7b0a776aa9b6a"; # 21.11-moon-mux
      fetchSubmodules = true;
      sha256 = "sha256-9bT1eQvjw87JjVc05Eia8CRVACEfcQf9a3JDrMy4GUg=";
    };
    nic-emu = fetchFromGitHub {
      owner = "vmuxIO";
      repo = "nic-emu";
      rev = "b9f08e11a59e0cb289a7f401d1ccb089daa6a99c";
      sha256 = "sha256-6ZZ1eECixWHsxw3aTwVIb068jDBpAUNWhqc4vde3hfA=";
    };
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

    rm -r $sourceRoot/subprojects/nic-emu || true
    cp -r ${srcpack.nic-emu} $sourceRoot/subprojects/nic-emu
    chmod -R u+w $sourceRoot/subprojects/nic-emu

    mkdir -p $sourceRoot/build/subprojects/nic-emu/target/debug/build
    chmod -R u+w $sourceRoot/build/subprojects/nic-emu/target/debug/build
    cp ${libnic-emu}/lib/libnic_emu.a $sourceRoot/build/subprojects/nic-emu/target/debug/build

    cp ${libnic-emu}/lib/libnic_emu.a $sourceRoot/
    cp ${libnic-emu}/lib/include/* $sourceRoot/src
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
    boost

    json_c
    cmocka
    libnic-emu
  ];

  hardeningDisable = [ "all" ];

  configurePhase = ''
    pwd
    ls
    meson build
  '';
  buildPhase = ''
    meson compile -C build
  '';

  installPhase = ''
    mkdir -p $out/bin
    cp build/vmux $out/bin/

    mkdir -p $out/lib
    cp build/subprojects/libvfio-user/lib/libvfio-user.so $out/lib
    cp build/subprojects/libvfio-user/lib/libvfio-user.so.0 $out/lib
    cp build/subprojects/libvfio-user/lib/libvfio-user.so.0.0.1 $out/lib

    patchelf --shrink-rpath --allowed-rpath-prefixes /nix/store $out/bin/vmux
    patchelf --add-rpath $out/lib $out/bin/vmux
    '';

    dontFixup = true;
}
