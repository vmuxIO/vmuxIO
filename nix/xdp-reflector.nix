{self, pkgs, ...}: pkgs.clangStdenv.mkDerivation {
  pname = "xdp-reflector";
  version = "2022.08.03";

  src = self.inputs.xdp-reflector;

  buildInputs = with pkgs; [
    llvm
    linuxHeaders
  ];

  hardeningDisable = [ "all" ];
  #dontInstall = true;
  installPhase = ''
    mkdir -p $out/lib
    ls -lah
    cp reflector.o $out/lib
  '';
}
