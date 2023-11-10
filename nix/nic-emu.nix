{pkgs, ...}: pkgs.rustPlatform.buildRustPackage rec {
  name = "libnic-emu";
  src = pkgs.fetchFromGitHub {
    owner = "vmuxIO";
    repo = "nic-emu";
    # rev = "05aeaaaa9bdb05b16c03ed3e96e486f670e78bf4"; # dev/peter
    # sha256 = "sha256-tEL96zK+cYvr69mMLunku4kXESWadl0GdOJnDc8umbo=";
    rev = "cae125d7561c342c2d7d17dc5635f70ac7e4c7b7"; # main
    sha256 = "sha256-c3wjS4gwun2RkuOf548Uaaqapfz1pFGFs91flCxXiSc=";
  };

  cargoLock = {
    lockFile = src + "/Cargo.lock";
    outputHashes = {
      "libvfio-user-0.1.0" = "sha256-jxWy2/g3jVyGsdfEGxA+WoYMzQZwztSsDV+IwoJ82xk=";
    };
  };

  nativeBuildInputs = with pkgs; [
    ninja
    meson
    pkg-config
  ];
  buildInputs = with pkgs; [
    json_c
    cmocka

    libclang.lib
  ];

  # set features we want to build
  preBuild = ''
    substituteInPlace Cargo.toml --replace \
      'default = ["build-binary"]' \
      'default = ["generate-bindings"]'
  '';

  LIBCLANG_PATH = "${pkgs.libclang.lib}/lib";

  # *.a is installed automatically, but not include/
  postInstall = ''
    cp -r target/*/include $out/lib/
  '';

  hardeningDisable = [ "all" ];
  dontUseNinjaBuild = true;
  dontUseNinjaInstall = true;
  dontUseNinjaCheck = true;
}
