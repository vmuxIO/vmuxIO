{nixpkgs, stdenv, fetchurl, writeShellApplication, linux, openssl, tbb, libbsd, numactl, luajit, hello}:

stdenv.mkDerivation {
  pname = "moongen";
  version = "2021.07.17";

  #src = /home/peter/dev/phd/MoonGen2;
  src = nixpkgs.fetchgit {
    url = "https://github.com/emmericp/MoonGen.git";
    rev = "25c61ee76b9ca30b83ecdeef8af2c7f89625cb4e";
    fetchSubmodules = true;
    #deepClone = true;
    sha256 = "sha256-UVM98DYupE1BU2+VExJpv47nOZ8Ieno1E+YIe5p73Zo=";
  };

  nativeBuildInputs = with nixpkgs; [
    bashInteractive
    cmake
    ninja
    meson
    openssl
    bash
    strace
    (writeShellApplication {
      name = "git";
      text = ''
        echo ignoring git command
      '';
    })
  ];
  buildInputs = [
    openssl
    tbb
    libbsd
    numactl
    luajit
  ];
  RTE_KERNELDIR = "${linux.dev}/lib/modules/${linux.modDirVersion}/build";
  NIX_CFLAGS_COMPILE = "-Wno-error=maybe-uninitialized -Wno-dev";
  CFLAGS_COMPILE = "-Wno-error=maybe-uninitialized -Wno-dev";

  dontConfigure = true;

  buildPhase = ''
    substituteInPlace ./libmoon/build.sh \
      --replace "#!/bin/bash" "#!${nixpkgs.bash}/bin/bash
      echo $RTE_KERNELDIR"
    substituteInPlace ./libmoon/build.sh \
      --replace "./bind-interfaces.sh \''${FLAGS}" "echo skipping bind-interfaces.sh"

    ${nixpkgs.bash}/bin/bash ./build.sh
  '';

  installPhase = ''
    mkdir -p $out/bin
    cp ${hello}/bin/hello $out/bin

    cp build/MoonGen $out/bin
    mkdir -p $out/bin/lua
    cp -r examples $out/bin
    cp -r flows $out/bin
    cp -r interface $out/bin
    cp -r lua $out/bin
    cp -r rfc2544 $out/bin
    mkdir -p $out/bin/libmoon
    cp -r libmoon $out/bin
    mkdir -p $out/lib/libmoon
    cp -r build/libmoon $out/lib/
    mkdir -p $out/lib/dpdk
    cp -r libmoon/deps/dpdk/x86_64-native-linuxapp-gcc/lib $out/lib/dpdk
    mkdir -p $out/lib/luajit
    cp -r libmoon/deps/luajit/usr/local/lib $out/lib/luajit
    mkdir -p $out/lib/highwayhash
    cp -r libmoon/deps/highwayhash/lib $out/lib/highwayhash

    patchelf --shrink-rpath --allowed-rpath-prefixes /nix/store $out/bin/MoonGen
    patchelf --add-rpath $out/lib/libmoon $out/bin/MoonGen
    patchelf --add-rpath $out/lib/libmoon/tbb_cmake_build/tbb_cmake_build_subdir_release $out/bin/MoonGen
    patchelf --add-rpath $out/lib/dpdk/x86_64-native-linuxapp-gcc/lib $out/bin/MoonGen
    patchelf --add-rpath $out/lib/luajit/usr/local/lib $out/bin/MoonGen
    patchelf --add-rpath $out/lib/highwayhash/lib $out/bin/MoonGen
  '';

  dontFixup = true;
}
