{ pkgs, lib, ... }: 
pkgs.stdenv.mkDerivation rec {
  pname = "ycsb";
  version = "0.17.0";

  src = builtins.fetchTarball {
    url = "https://github.com/brianfrankcooper/YCSB/releases/download/${version}/ycsb-${version}.tar.gz";
    sha256 = "sha256:08b6mqcv951s9h7bm6zzb9k9r0w8mqwnnkdkiig3y8q2x5l1vc8y";
  };

  nativeBuildInputs = with pkgs; [ makeWrapper ];

  dontConfigure = true;
  dontBuild = true;

  installPhase = ''
    mkdir -p $out/bin
    mkdir -p $out/lib
    mkdir -p $out/share
    cp -r ./bin/* $out/bin
    cp -r ./lib/* $out/lib
    cp -r ./workloads $out/share

    makeWrapper $out/bin/ycsb.sh $out/bin/ycsb-wrapped \
      --prefix PATH : ${lib.makeBinPath [ pkgs.jre ]}
  '';

  # meta = with lib; {
  #   description = "Simple command line wrapper around JD Core Java Decompiler project";
  #   homepage = "https://github.com/intoolswetrust/jd-cli";
  #   license = licenses.gpl3Plus;
  #   maintainers = with maintainers; [ majiir ];
  # };
}
