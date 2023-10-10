{pkgs, ...}: pkgs.rustPlatform.buildRustPackage {
  name = "libnic-emu";
  src = pkgs.fetchFromGitHub {
    owner = "vmuxIO";
    repo = "nic-emu";
    rev = "05aeaaaa9bdb05b16c03ed3e96e486f670e78bf4"; # dev/peter
    sha256 = "sha256-tEL96zK+cYvr69mMLunku4kXESWadl0GdOJnDc8umbo=";
    # rev = "23292d00351d03c8d489a5a5a0d7bf324215e8f8"; # main
    # sha256 = "sha256-I9fk0WGpic+XqPo10syqzFa8Bs2nk870pprDTW9Evdk=";
  };

  cargoLock = {
    lockFile = ./nic-emu.cargo.lock;
    outputHashes = {
      "libvfio-user-0.0.1" = "sha256-svN/vMYeGt+WE4I3+J3HUbREug0udvqvglgp7kQoNHo=";
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
