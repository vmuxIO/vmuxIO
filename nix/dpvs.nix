{ self
, system
, pkgs
, ...}:
let 
  dpdk = self.outputs.packages.${system}.dpdk-dpvs;
in
pkgs.stdenv.mkDerivation {
  pname = "dpvs";
  version = "2023-09-21";

  src = pkgs.fetchFromGitHub {
    owner = "iqiyi";
    repo = "dpvs";
    rev = "v1.9.4";
    sha256 = "sha256-PSBApG2Ix/peh2+FZxz7PjK1f+JbNmL1l6aNpEhLslY=";
  };
  
  nativeBuildInputs = with pkgs; [
    openssl
    python3Packages.pyelftools
    pkgconfig
    coreutils
    (writeScriptBin "git" ''
        echo ignoring git command
    '')
  ];
  buildInputs = with pkgs; [
    luajit

    # dpvs uses this as a dev package to recompile its own dpdk
    dpdk

    # dpdk libs
    libnl
    jansson
    libbpf
    libbsd
    libelf
    libpcap
    numactl
    openssl.dev
    zlib

    # dpvs deps
    autoconf
    automake
    popt
  ];
  CFLAGS = "-Wno-deprecated-declarations -Wno-address -ggdb -Og";
  PKG_CONFIG_PATH = "${dpdk}/lib/pkgconfig/";
  hardeningDisable = [ "all" ];

  dontConfigure = true;

  postPatch = ''
    substituteInPlace ./Makefile \
      --replace "/bin/uname" "${pkgs.coreutils}/bin/uname"
    substituteInPlace ./tools/ipvsadm/ipvsadm.c \
      --replace '#include "popt.h"' '#include <popt.h>'
    substituteInPlace ./Makefile \
      --replace 'INSDIR  =' 'INSDIR  ?='
    substituteInPlace ./src/netif.c \
      --replace "return rss_value;" "return rss_value & (!ETH_RSS_IPV6_EX); // ignore this one feature not supported by E810"
  '';

  buildPhase = ''
    export PKG_CONFIG_PATH="$PKG_CONFIG_PATH:${dpdk}/lib/pkgconfig"
    make DEBUG=1
  '';

  installPhase = ''
    export INSDIR=$out/bin
    make install
    mkdir -p $out/share/conf
    cp -r /build/$sourceRoot/conf $out/share
  '';

  dontStrip = true;
}
