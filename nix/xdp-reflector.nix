{xdp-reflector-src, pkgs, ...}: pkgs.clangStdenv.mkDerivation {
  pname = "xdp-reflector";
  version = "2022.08.03";
  LOCAL_MAC = "{ 0x55, 0x55, 0x55, 0x55, 0x55, 0x55 }";

  src = xdp-reflector-src;

  buildInputs = with pkgs; [
    llvm
    linuxHeaders
    pkg-config
    elfutils.dev
  ];

  hardeningDisable = [ "all" ];

  buildPhase = ''
    make OUR_MAC="$LOCAL_MAC"
  '';

  #dontInstall = true;
  installPhase = ''
    mkdir -p $out/lib
    ls -lah
    cp reflector.o $out/lib
    cp pure_reflector.o $out/lib
  '';

  # someone added some --gcc-toolchain arg which is of course not consumed by
  # clang.  Also, we overwrite this one default include by setting this
  # variable, so we have to hardcode this here.
  BPF_CFLAGS = "-I./libbpf/src/build/usr/include/ -Wno-unused-command-line-argument";
}
