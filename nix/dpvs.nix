{ self
,stdenv
, fetchFromGitHub
, fetchurl
, writeScriptBin
, linux
, openssl
, tbb
, libbsd
, numactl
, luajit
, hello
, cmake
, ninja
, meson
, bash
, gcc8Stdenv
, libpcap
, python3Packages
, linux-firmware-pinned
, system
, pkgs
}:
let 
  dpdkVersion = "21.11";
  srcpack = {
    dpvs = fetchFromGitHub {
      owner = "iqiyi";
      repo = "dpvs";
      rev = "v1.9.4";
      sha256 = "sha256-PSBApG2Ix/peh2+FZxz7PjK1f+JbNmL1l6aNpEhLslY=";
    };
    moongen = self.inputs.moongen-lachnit-src;
    libmoon = self.inputs.libmoon-lachnit-src;
    dpdk = self.inputs.dpdk-lachnit-src;
    # dpdk = fetchurl {
    #   url = "https://fast.dpdk.org/rel/dpdk-${dpdkVersion}.tar.xz";
    #   sha256 = "sha256-HhJJm0xfzbV8g+X+GE6mvs3ffPCSiTwsXvLvsO7BLws=";
    # };
    #
  };
  dpdk = self.outputs.packages.${system}.dpdk;
in
stdenv.mkDerivation {
  pname = "dpvs";
  version = "2023-09-21";

  src = srcpack.dpvs;
  
  postUnpack = ''
    cp -r ${dpdk} $sourceRoot/dpdk-result
    chmod -R u+w $sourceRoot/dpdk-result
  '';
  # postUnpack = ''
  #   rm -r $sourceRoot/libmoon
  #   cp -r ${srcpack.libmoon} $sourceRoot/libmoon
  #   chmod -R u+w $sourceRoot/libmoon
  #
  #   rm -r $sourceRoot/libmoon/deps/dpdk
  #   cp -r ${srcpack.dpdk} $sourceRoot/libmoon/deps/dpdk
  #   chmod -R u+w $sourceRoot/libmoon/deps/dpdk
  # '';

  nativeBuildInputs = [
    # cmake
    # ninja
    # meson
    openssl
    python3Packages.pyelftools
    pkgs.pkgconfig
    pkgs.coreutils
    (writeScriptBin "git" ''
        echo ignoring git command
    '')
  ];
  buildInputs = [
    numactl
    luajit

    # dpvs uses this as a dev package to recompile its own dpdk
    dpdk

    # dpdk libs
    pkgs.libnl
    pkgs.jansson
    pkgs.libbpf
    pkgs.libbsd
    pkgs.libelf
    pkgs.libpcap
    pkgs.numactl
    pkgs.openssl.dev
    pkgs.zlib

    # dpvs deps
    pkgs.autoconf
    pkgs.automake
    pkgs.popt
  ];
  RTE_KERNELDIR = "${linux.dev}/lib/modules/${linux.modDirVersion}/build";
  CXXFLAGS = "-std=gnu++14"; # libmoon->highwayhash->tbb needs <c++17
  CFLAGS = "-Wno-deprecated-declarations -Wno-address";
  PKG_CONFIG_PATH = "${dpdk}/lib/pkgconfig/";
  hardeningDisable = [ "all" ];

  dontConfigure = true;

  postPatch = ''
    substituteInPlace ./Makefile \
      --replace "/bin/uname" "${pkgs.coreutils}/bin/uname"
    substituteInPlace ./dpdk-result/include/rte_mbuf_dyn.h \
      --replace "#include <sys/types.h>
    " "#include <sys/types.h>
    #include <stdint.h>
    "
    substituteInPlace ./src/mbuf.c \
      --replace "#include <rte_mbuf_dyn.h>
    " "#include <stdint.h>
    #include <stdio.h>
    #include <rte_compat.h>
    #include <rte_mbuf_dyn.h>
    "
    substituteInPlace ./tools/ipvsadm/ipvsadm.c \
      --replace '#include "popt.h"' '#include <popt.h>'
    substituteInPlace ./Makefile \
      --replace 'INSDIR  =' 'INSDIR  ?='
    substituteInPlace ./dpdk-result/lib/pkgconfig/libdpdk.pc \
      --replace "${dpdk}" "$sourceRoot/dpdk-result"
  '';
  # postPatch = ''
  #   ls -la ./libmoon
  #   patchShebangs ./libmoon/build.sh ./build.sh
  #   substituteInPlace ./libmoon/build.sh \
  #     --replace "./bind-interfaces.sh \''${FLAGS}" "echo skipping bind-interfaces.sh"
  #   substituteInPlace ./libmoon/deps/dpdk/drivers/net/ice/ice_ethdev.h \
  #     --replace '#define ICE_PKG_FILE_DEFAULT "/lib/firmware/intel/ice/ddp/ice.pkg"' \
  #     '#define ICE_PKG_FILE_DEFAULT "${linux-firmware-pinned}/lib/firmware/intel/ice/ddp/ice-1.3.26.0.pkg"'
  #   substituteInPlace ./libmoon/deps/dpdk/drivers/net/ice/ice_ethdev.h \
  #     --replace '#define ICE_PKG_FILE_SEARCH_PATH_DEFAULT "/lib/firmware/intel/ice/ddp/"' \
  #     '#define ICE_PKG_FILE_SEARCH_PATH_DEFAULT "${linux-firmware-pinned}/lib/firmware/intel/ice/ddp/"'
  # '';

  buildPhase = ''
    export PKG_CONFIG_PATH="$PKG_CONFIG_PATH:$sourceRoot/dpdk-result/lib/pkgconfig"
    make
    # make install
  '';

  installPhase = ''
    export INSDIR=$out/bin
    make install
  '';
  # installPhase = ''
  #   mkdir -p $out/bin
  #
  #   cp build/MoonGen $out/bin
  #   mkdir -p $out/bin/lua
  #   cp -r examples $out/bin
  #   cp -r flows $out/bin
  #   cp -r interface $out/bin
  #   cp -r lua $out/bin
  #   cp -r rfc2544 $out/bin
  #   mkdir -p $out/bin/libmoon
  #   cp -r libmoon $out/bin
  #   mkdir -p $out/lib/libmoon
  #   cp -r build/libmoon $out/lib/
  #   mkdir -p $out/lib/dpdk
  #   cp -r libmoon/deps/dpdk/x86_64-native-linux-gcc/lib $out/lib/dpdk
  #   cp -r libmoon/deps/dpdk/x86_64-native-linux-gcc/drivers $out/lib/dpdk
  #   mkdir -p $out/lib/luajit
  #   cp -r libmoon/deps/luajit/usr/local/lib $out/lib/luajit
  #   mkdir -p $out/lib/highwayhash
  #   cp -r libmoon/deps/highwayhash/lib $out/lib/highwayhash
  #
  #   # autopatchelfHook?
  #   patchelf --shrink-rpath --allowed-rpath-prefixes /nix/store $out/bin/MoonGen
  #   patchelf --add-rpath $out/lib/libmoon $out/bin/MoonGen
  #   patchelf --add-rpath $out/lib/libmoon/tbb_cmake_build/tbb_cmake_build_subdir_release $out/bin/MoonGen
  #   patchelf --add-rpath $out/lib/dpdk/lib $out/bin/MoonGen
  #   patchelf --add-rpath $out/lib/dpdk/drivers $out/bin/MoonGen
  #   patchelf --add-rpath $out/lib/luajit/usr/local/lib $out/bin/MoonGen
  #   patchelf --add-rpath $out/lib/highwayhash/lib $out/bin/MoonGen
  # '';

  # dontFixup = true;
}
