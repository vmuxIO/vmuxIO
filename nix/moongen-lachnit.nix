{ self
, linux-firmware-pinned
, pkgs
, linux
}:
let 
  srcpack = {
    moongen = self.inputs.moongen-lachnit-src;
    libmoon = self.inputs.libmoon-lachnit-src;
    dpdk = self.inputs.dpdk-lachnit-src;
  };
in
pkgs.stdenv.mkDerivation {
  pname = "moongen-lachnit";
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

  nativeBuildInputs = with pkgs; [
    pkg-config
    cmake
    ninja
    meson
    openssl
    python3Packages.pyelftools
    (writeScriptBin "git" ''
        echo ignoring git command
    '')
  ];
  buildInputs = with pkgs; [
    openssl
    libbsd
    numactl
    luajit
    libpcap
  ];
  RTE_KERNELDIR = "${linux.dev}/lib/modules/${linux.modDirVersion}/build";
  CXXFLAGS = "-std=gnu++14"; # libmoon->highwayhash->tbb needs <c++17

  dontConfigure = true;

  postPatch = ''
    ls -la ./libmoon
    patchShebangs ./libmoon/build.sh ./build.sh
    substituteInPlace ./libmoon/build.sh \
      --replace "./bind-interfaces.sh \''${FLAGS}" "echo skipping bind-interfaces.sh"
    substituteInPlace ./libmoon/deps/dpdk/drivers/net/ice/ice_ethdev.h \
      --replace '#define ICE_PKG_FILE_DEFAULT "/lib/firmware/intel/ice/ddp/ice.pkg"' \
      '#define ICE_PKG_FILE_DEFAULT "${linux-firmware-pinned}/lib/firmware/intel/ice/ddp/ice-1.3.26.0.pkg"'
    substituteInPlace ./libmoon/deps/dpdk/drivers/net/ice/ice_ethdev.h \
      --replace '#define ICE_PKG_FILE_SEARCH_PATH_DEFAULT "/lib/firmware/intel/ice/ddp/"' \
      '#define ICE_PKG_FILE_SEARCH_PATH_DEFAULT "${linux-firmware-pinned}/lib/firmware/intel/ice/ddp/"'
  '';


  buildPhase = ''
    ./build.sh
  '';

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
    cp -r libmoon/deps/dpdk/x86_64-native-linux-gcc/lib $out/lib/dpdk
    mkdir -p $out/lib/luajit
    cp -r libmoon/deps/luajit/usr/local/lib $out/lib/luajit

    # autopatchelfHook?
    patchelf --shrink-rpath --allowed-rpath-prefixes /nix/store $out/bin/MoonGen
    patchelf --add-rpath $out/lib/libmoon $out/bin/MoonGen
    patchelf --add-rpath $out/lib/libmoon/tbb_cmake_build/tbb_cmake_build_subdir_release $out/bin/MoonGen
    patchelf --add-rpath $out/lib/dpdk/lib $out/bin/MoonGen
    patchelf --add-rpath $out/lib/luajit/usr/local/lib $out/bin/MoonGen
  '';

  dontFixup = true;
}
