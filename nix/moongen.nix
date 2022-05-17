{ stdenv
, fetchFromGitHub
, fetchurl
, writeShellApplication
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
}:
let 
  libmoonsrc = fetchFromGitHub {
    owner = "vmuxIO";
    repo = "libmoon";
    rev = "73caf07402def6e0395bc0e158e2e637af0a72b5"; # dev/ice
    fetchSubmodules = true;
    sha256 = "sha256-/dW3MtszCXfbzK0UFTf5L8MTtDlfCq4bDzKLDlwkIvI=";
  };
  dpdksrc = fetchFromGitHub {
    owner = "vmuxIO";
    repo = "dpdk";
    rev = "54dbc2501a10b6b41abed37ff124579b15bd2871"; # v19.05-moon-vmux
    fetchSubmodules = true;
    sha256 = "sha256-LsvhuM9zG/2MCTqpYcYjLwMcJ8yHu/4SIGISv5pqEoo=";
  };
in
stdenv.mkDerivation {
  pname = "moongen";
  version = "2021.07.17-19";

  src = fetchFromGitHub {
    owner = "vmuxIO";
    repo = "MoonGen";
    rev = "a51cdf15004df6631b23a7fae69d5661978facd4"; # dpdk-19.05
    fetchSubmodules = true;
    sha256 = "sha256-QPQWDV5OL86B9BtUaKCVv0Tno5TdvoHfjvm47cNka+0=";
  };
  
  postUnpack = ''
    rm -r $sourceRoot/libmoon
    cp -r ${libmoonsrc} $sourceRoot/libmoon
    chmod -R u+w $sourceRoot/libmoon

    rm -r $sourceRoot/libmoon/deps/dpdk
    cp -r ${dpdksrc} $sourceRoot/libmoon/deps/dpdk
    chmod -R u+w $sourceRoot/libmoon/deps/dpdk
  '';

  nativeBuildInputs = [
    cmake
    ninja
    meson
    openssl
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
  NIX_CFLAGS_COMPILE = "-Wno-error=maybe-uninitialized";
  CFLAGS_COMPILE = "-Wno-error=maybe-uninitialized";

  dontConfigure = true;

  postPatch = ''
    ls -la ./libmoon
    patchShebangs ./libmoon/build.sh ./build.sh
    substituteInPlace ./libmoon/build.sh \
      --replace "./bind-interfaces.sh \''${FLAGS}" "echo skipping bind-interfaces.sh"
    substituteInPlace ./libmoon/deps/dpdk/drivers/net/ice/ice_ethdev.c \
      --replace '#define ICE_DFLT_PKG_FILE "/lib/firmware/intel/ice/ddp/ice.pkg"' \
      '#define ICE_DFLT_PKG_FILE "/scratch/okelmann/linux-firmware/intel/ice/ddp/ice-1.3.26.0.pkg"'
  '';

  buildPhase = "./build.sh";

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
    mkdir -p $out/lib/highwayhash
    cp -r libmoon/deps/highwayhash/lib $out/lib/highwayhash

    # autopatchelfHook?
    patchelf --shrink-rpath --allowed-rpath-prefixes /nix/store $out/bin/MoonGen
    patchelf --add-rpath $out/lib/libmoon $out/bin/MoonGen
    patchelf --add-rpath $out/lib/libmoon/tbb_cmake_build/tbb_cmake_build_subdir_release $out/bin/MoonGen
    patchelf --add-rpath $out/lib/dpdk/x86_64-native-linuxapp-gcc/lib $out/bin/MoonGen
    patchelf --add-rpath $out/lib/luajit/usr/local/lib $out/bin/MoonGen
    patchelf --add-rpath $out/lib/highwayhash/lib $out/bin/MoonGen
  '';

  dontFixup = true;
}
