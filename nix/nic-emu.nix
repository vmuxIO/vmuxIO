{pkgs, ...}: pkgs.rustPlatform.buildRustPackage {
  name = "libnic-emu";
  src = pkgs.fetchFromGitHub {
    owner = "vmuxIO";
    repo = "nic-emu";
    # rev = "05aeaaaa9bdb05b16c03ed3e96e486f670e78bf4"; # dev/peter
    # sha256 = "sha256-tEL96zK+cYvr69mMLunku4kXESWadl0GdOJnDc8umbo=";
    rev = "82bb3b1432bc62511eb43127e535e85154ce1053"; # main
    sha256 = "sha256-iXizCJ/yKTdnUjObfXTi44+1DL9/GteN6h9kbBSwe14=";
  };

  cargoLock = {
    lockFile = ./nic-emu.cargo.lock;
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
