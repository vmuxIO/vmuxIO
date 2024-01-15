{ self
, pkgs
, selfpkgs
, ...
}:
let 
  srcpack = {
    moongen = self.inputs.moonmux-src;
    libmoon = self.inputs.libmoon-src;
    dpdk = self.inputs.dpdk-src;
    fastclick = self.inputs.fastclick-src;
  };
in
pkgs.stdenv.mkDerivation {
  pname = "fastclick";
  version = "2024.01.15-21";

  src = srcpack.fastclick;

  # postUnpack = ''
  #   rm -r $sourceRoot/libmoon
  #   cp -r ${srcpack.libmoon} $sourceRoot/libmoon
  #   chmod -R u+w $sourceRoot/libmoon
  #
  #   rm -r $sourceRoot/libmoon/deps/dpdk
  #   cp -r ${srcpack.dpdk} $sourceRoot/libmoon/deps/dpdk
  #   chmod -R u+w $sourceRoot/libmoon/deps/dpdk
  # '';

  nativeBuildInputs = with pkgs; [
    # automake
    # gnumake
    coreutils
    perl
    python3Packages.pyelftools
    openssl
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
  # RTE_KERNELDIR = "${pkgs.linux.dev}/lib/modules/${pkgs.linux.modDirVersion}/build";
  # CXXFLAGS = "-std=gnu++14"; # libmoon->highwayhash->tbb needs <c++17

  # dontConfigure = true;

  postPatch = ''
    # sln /bin/echo ${pkgs.coreutils}/bin/echo
    find . -type f -exec sed -i 's/\/bin\/echo/echo/g' {} \;
    find . -type f -exec sed -i 's/\/bin\/rm/rm/g' {} \;
    find . -type f -exec sed -i 's/\/bin\/ln/ln/g' {} \;
    # substituteInPlace ./userlevel/Makefile.in \
    #   --replace "/bin/echo" "echo"
  '';
  # postPatch = ''
  #   ls -la ./libmoon
  #   patchShebangs ./libmoon/build.sh ./build.sh
  #   substituteInPlace ./libmoon/build.sh \
  #     --replace "./bind-interfaces.sh \''${FLAGS}" "echo skipping bind-interfaces.sh"
  #   substituteInPlace ./libmoon/deps/dpdk/drivers/net/ice/ice_ethdev.h \
  #     --replace '#define ICE_PKG_FILE_DEFAULT "/lib/firmware/intel/ice/ddp/ice.pkg"' \
  #     '#define ICE_PKG_FILE_DEFAULT "${selfpkgs.linux-firmware-pinned}/lib/firmware/intel/ice/ddp/ice-1.3.26.0.pkg"'
  #   substituteInPlace ./libmoon/deps/dpdk/drivers/net/ice/ice_ethdev.h \
  #     --replace '#define ICE_PKG_FILE_SEARCH_PATH_DEFAULT "/lib/firmware/intel/ice/ddp/"' \
  #     '#define ICE_PKG_FILE_SEARCH_PATH_DEFAULT "${selfpkgs.linux-firmware-pinned}/lib/firmware/intel/ice/ddp/"'
  # '';
  #
  # buildPhase = "./build.sh";
  # buildPhase = ''
  #     runHook preBuild
  #
  #   if [[ -z "$${makeFlags-}" && -z "$${makefile:-}" && ! ( -e Makefile || -e makefile || -e GNUmakefile ) ]]; then
  #       echo "no Makefile or custom buildPhase, doing nothing"
  #   else
  #       foundMakefile=1
  #
  #       # shellcheck disable=SC2086
  #       local flagsArray=(
  #           $${enableParallelBuilding:+-j$${NIX_BUILD_CORES}}
  #           SHELL=$SHELL
  #       )
  #       _accumFlagsArray makeFlags makeFlagsArray buildFlags buildFlagsArray
  #
  #       echoCmd 'build flags' "$${flagsArray[@]}"
  #       NIX_DEBUG=1 make $${makefile:+-f $makefile} "$${flagsArray[@]}"
  #       unset flagsArray
  #   fi
  #
  #   runHook postBuild
  # '';
  enableParallelBuilding = true;
  preBuild = ''
    echo foobar
    echo $enableParallelBuilding
    gzip --version
  '';
  NIX_DEBUG = "1";
  
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
  #   cp -r libmoon/deps/dpdk/build/lib $out/lib/dpdk
  #   cp -r libmoon/deps/dpdk/build/drivers $out/lib/dpdk
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
  #
  # dontFixup = true;
}
