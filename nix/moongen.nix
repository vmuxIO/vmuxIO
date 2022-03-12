{nixpkgs, stdenv, fetchurl, writeShellApplication, linux, openssl, tbb, libbsd, numactl, luajit, hello}:

stdenv.mkDerivation {
  pname = "moongen";
  version = "2021.07.17";

  src = /home/peter/dev/phd/MoonGen2;
  #src = nixpkgs.fetchgit {
  #  url = "https://github.com/emmericp/MoonGen.git";
  #  rev = "25c61ee76b9ca30b83ecdeef8af2c7f89625cb4e";
  #  fetchSubmodules = true;
  #  #deepClone = true;
  #  sha256 = "sha256-UVM98DYupE1BU2+VExJpv47nOZ8Ieno1E+YIe5p73Zo=";
  #};

  nativeBuildInputs = with nixpkgs; [
    bashInteractive
    cmake
    ninja
    meson
    openssl
    bash
    strace
    #(writeShellApplication {
      #name = "git";
      #text = ''
        #echo ignoring git command
      #'';
    #})
    git
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
    pushd /build/MoonGen2/libmoon/deps/dpdk
    git describe
    popd
    env
    #export RTE_KERNELDIR="${linux.dev}/lib/modules/${linux.modDirVersion}/build"
    echo $RTE_KERNELDIR
    ls /build/MoonGen2/libmoon/deps/dpdk/x86_64-native-linuxapp-gcc/include || true

    substituteInPlace ./libmoon/build.sh \
      --replace "#!/bin/bash" "#!${nixpkgs.bash}/bin/bash
      echo $RTE_KERNELDIR"
    substituteInPlace ./libmoon/build.sh \
      --replace "./bind-interfaces.sh \$\{FLAGS\}" "echo skipping bind-interfaces.sh"

    #${nixpkgs.bash}/bin/bash ./build.sh || true
    #cp -r /build/MoonGen2/libmoon/deps/dpdk/x86_64-native-linuxapp-gcc/include /build/MoonGen2/libmoon/src
    ${nixpkgs.bash}/bin/bash ./build.sh
    #ls /build/MoonGen2/libmoon/deps/dpdk/x86_64-native-linuxapp-gcc/include
  '';

  installPhase = ''
    mkdir -p $out/bin
    cp ${hello}/bin/hello $out/bin
  '';
}
