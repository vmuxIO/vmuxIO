{ self
, pkgs
, lib
, selfpkgs
, ...
}:
let
  dpdk = selfpkgs.dpdk-vdpdk;
  debug = false;
in
pkgs.stdenv.mkDerivation {
  pname = "dpdk-tap-fwd";
  version = "1.0";

  src = ../subprojects/dpdk-tap-fwd/.;

  nativeBuildInputs = with pkgs; [
    pkg-config
    libbsd
  ];
  buildInputs = with pkgs; [
    dpdk
  ];

  buildPhase = ''
    gcc -O3 -g $(pkg-config --cflags libdpdk) main.c $(pkg-config --libs libdpdk) -o dpdk-tap-fwd
  '';

  installPhase = ''
    mkdir -p $out/bin
    cp dpdk-tap-fwd $out/bin/
  '';

  dontFixup = debug;
  dontStrip = debug;
}
