{ stdenv
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
, moonmux-src
, libmoon-src
, dpdk-src
}:
let 
  srcpack = {
    moongen = moonmux-src;
    libmoon = libmoon-src;
    dpdk = dpdk-src;
  };
in
stdenv.mkDerivation {
  pname = "moongen";
  version = "2021.07.17-21";

  src = srcpack.moongen;

  postUnpack = ''
    rm -r $sourceRoot/libmoon
    cp -r ${srcpack.libmoon} $sourceRoot/libmoon
    chmod -R u+w $sourceRoot/libmoon

    rm -r $sourceRoot/libmoon/deps/dpdk
    cp -r ${srcpack.dpdk} $sourceRoot/libmoon/deps/dpdk
    chmod -R u+w $sourceRoot/libmoon/deps/dpdk
  '';

  nativeBuildInputs = [
    cmake
    ninja
    meson
    openssl
    python3Packages.pyelftools
    (writeScriptBin "git" ''
        echo ignoring git command
    '')
  ];
  buildInputs = [
    openssl
    #tbb
    libbsd
    numactl
    luajit
    libpcap
  ];
  RTE_KERNELDIR = "${linux.dev}/lib/modules/${linux.modDirVersion}/build";
  NIX_CFLAGS_COMPILE = "-Wno-error=maybe-uninitialized";
  CFLAGS_COMPILE = "-Wno-error=maybe-uninitialized";
  CXXFLAGS = "-std=gnu++11";
  NIX_DEBUG = 1;

  dontConfigure = true;

  postPatch = ''
    ls -la ./libmoon
    patchShebangs ./libmoon/build.sh ./build.sh
    substituteInPlace ./libmoon/build.sh \
      --replace "./bind-interfaces.sh \''${FLAGS}" "echo skipping bind-interfaces.sh"
    substituteInPlace ./libmoon/deps/dpdk/drivers/net/ice/ice_ethdev.h \
      --replace '#define ICE_PKG_FILE_DEFAULT "/lib/firmware/intel/ice/ddp/ice.pkg"' \
      '#define ICE_PKG_FILE_DEFAULT "/scratch/okelmann/linux-firmware/intel/ice/ddp/ice-1.3.26.0.pkg"'
    substituteInPlace ./libmoon/deps/dpdk/drivers/net/ice/ice_ethdev.h \
      --replace '#define ICE_PKG_FILE_SEARCH_PATH_DEFAULT "/lib/firmware/intel/ice/ddp/"' \
      '#define ICE_PKG_FILE_SEARCH_PATH_DEFAULT "/scratch/okelmann/linux-firmware/intel/ice/ddp/"'


    ## use nixos tbb instead
    #substituteInPlace ./libmoon/CMakeLists.txt \
    #  --replace 'SET(HIGHWAYHASH_LIBS' 'SET(HIGHWAYHASH_LIBS libtbb.so.2 libtbbmalloc_proxy.so.2 libtbbmalloc.so.2'
    ##substituteInPlace ./libmoon/CMakeLists.txt \
    ##  --replace '# add tbb' ' '
    #substituteInPlace ./libmoon/CMakeLists.txt \
    #  --replace 'include(''${CMAKE_CURRENT_SOURCE_DIR}/deps/tbb/cmake/TBBBuild.cmake)' ' '
    #substituteInPlace ./libmoon/CMakeLists.txt \
    #  --replace 'tbb_build(TBB_ROOT ''${CMAKE_CURRENT_SOURCE_DIR}/deps/tbb CONFIG_DIR TBB_DIR)' ' '
    ##substituteInPlace ./libmoon/CMakeLists.txt \
    ##  --replace 'find_package(TBB)' ' '
    ##substituteInPlace ./libmoon/CMakeLists.txt \
    ##  --replace ''\'''${TBB_IMPORTED_TARGETS}' ' '
    #substituteInPlace ./libmoon/CMakeLists.txt \
    #  --replace ''\'''${CMAKE_CURRENT_SOURCE_DIR}/deps/tbb/include' ' '
    #substituteInPlace ./CMakeLists.txt \
    #  --replace ''\'''${CMAKE_CURRENT_SOURCE_DIR}/libmoon/deps/tbb/include' ' '
  '';

  buildPhase = "NIX_DEBUG=1 ./build.sh";

  installPhase = ''
    mkdir -p $out/bin

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
    cp -r libmoon/deps/dpdk/build/lib $out/lib/dpdk
    cp -r libmoon/deps/dpdk/build/drivers $out/lib/dpdk
    mkdir -p $out/lib/luajit
    cp -r libmoon/deps/luajit/usr/local/lib $out/lib/luajit
    mkdir -p $out/lib/highwayhash
    cp -r libmoon/deps/highwayhash/lib $out/lib/highwayhash

    # autopatchelfHook?
    patchelf --shrink-rpath --allowed-rpath-prefixes /nix/store $out/bin/MoonGen
    patchelf --add-rpath $out/lib/libmoon $out/bin/MoonGen
    patchelf --add-rpath $out/lib/libmoon/tbb_cmake_build/tbb_cmake_build_subdir_release $out/bin/MoonGen
    patchelf --add-rpath $out/lib/dpdk/lib $out/bin/MoonGen
    patchelf --add-rpath $out/lib/dpdk/drivers $out/bin/MoonGen
    patchelf --add-rpath $out/lib/luajit/usr/local/lib $out/bin/MoonGen
    patchelf --add-rpath $out/lib/highwayhash/lib $out/bin/MoonGen
  '';

  dontFixup = true;
}
