{ pkgs, lib, ... }: pkgs.maven.buildMavenPackage rec {
  pname = "ycsb";
  version = "0.17.0";

  src = pkgs.fetchFromGitHub {
    owner = "brianfrankcooper";
    repo = "YCSB";
    rev = "${version}";
    hash = "sha256-STp6sTeaYrGPe8cr0UJshy9ARrzKwmv6AzEP5aOl1bQ=";
  };

  mvnHash = "";

  nativeBuildInputs = with pkgs; [ makeWrapper maven ];

  mvnParameters = "-Dmaven.test.skip=true";

  installPhase = ''
    mkdir -p $out/bin $out/share/jd-cli
    install -Dm644 jd-cli/target/jd-cli.jar $out/share/jd-cli

    makeWrapper ${pkgs.jre}/bin/java $out/bin/jd-cli \
      --add-flags "-jar $out/share/jd-cli/jd-cli.jar"
  '';

  meta = with lib; {
    description = "Simple command line wrapper around JD Core Java Decompiler project";
    homepage = "https://github.com/intoolswetrust/jd-cli";
    license = licenses.gpl3Plus;
    maintainers = with maintainers; [ majiir ];
  };
}
