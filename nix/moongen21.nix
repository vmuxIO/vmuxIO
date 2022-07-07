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
, libpcap
, python3Packages
}:
let 
  srcpack = {
    moongen = fetchFromGitHub {
      owner = "vmuxIO";
      repo = "MoonGen";
      rev = "fdd0d97a8b883159d90adad0354a4ec54a1722af"; # dpdk-21.11
      fetchSubmodules = true;
      sha256 = "sha256-l3h58VU33AP1tWMHM2YMJMFP4A826Exkn+xyrxjJkdQ=";
    };
    libmoon = fetchFromGitHub {
      owner = "vmuxIO";
      repo = "libmoon";
      rev = "a42ed97f0a5aa81db45c29572b9ec44e4dfdadde"; # dev/ice
      fetchSubmodules = true;
      sha256 = "sha256-lbhIK6ktZdmL4yKE6/7uRL5x7Z/NFtOXSZB+hE5GpCg=";
    };
    dpdk = fetchFromGitHub {
      owner = "vmuxIO";
      repo = "dpdk";
      rev = "1717a5465aeda0cbaa393f66e4d3176cf6b3a6c0"; # 21.11-moon-mux
      fetchSubmodules = true;
      sha256 = "sha256-/Ixes+4cWSsKbLhxPTg8NM2IUKrYY+jh/LHFN9RS8vk=";
    };
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
    libpcap
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
    substituteInPlace ./libmoon/deps/dpdk/drivers/net/ice/ice_ethdev.h \
      --replace '#define ICE_PKG_FILE_DEFAULT "/lib/firmware/intel/ice/ddp/ice.pkg"' \
      '#define ICE_PKG_FILE_DEFAULT "/scratch/okelmann/linux-firmware/intel/ice/ddp/ice-1.3.26.0.pkg"'
    substituteInPlace ./libmoon/deps/dpdk/drivers/net/ice/ice_ethdev.h \
      --replace '#define ICE_PKG_FILE_SEARCH_PATH_DEFAULT "/lib/firmware/intel/ice/ddp/"' \
      '#define ICE_PKG_FILE_SEARCH_PATH_DEFAULT "/scratch/okelmann/linux-firmware/intel/ice/ddp/"'
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
