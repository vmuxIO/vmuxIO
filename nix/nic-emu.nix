{pkgs, ...}: pkgs.rustPlatform.buildRustPackage {
  name = "nic-emu";
  src = pkgs.fetchFromGitHub {
    owner = "vmuxIO";
    repo = "nic-emu";
    rev = "05aeaaaa9bdb05b16c03ed3e96e486f670e78bf4"; # dev/peter
    sha256 = "sha256-tEL96zK+cYvr69mMLunku4kXESWadl0GdOJnDc8umbo=";
    # rev = "23292d00351d03c8d489a5a5a0d7bf324215e8f8"; # main
    # sha256 = "sha256-I9fk0WGpic+XqPo10syqzFa8Bs2nk870pprDTW9Evdk=";
  };
  # postPatch = ''
  #   pwd
  #   substituteInPlace ../cargo-vendor-dir/libvfio-user-0.0.1/build.rs --replace \
  #     "bindgen::Builder::default()" "bindgen::Builder::default()::clang_arg('-x')::clang_arg('c++')"
  #
  # '';
  preBuild = ''
    sed -i '126i\println!("foobar {:?}", env::var("LIBCLANG_PATH"));' ../cargo-vendor-dir/libvfio-user-sys-0.0.1/build.rs
  '';
  cargoLock = {
    lockFile = ./nic-emu.cargo.lock;
    outputHashes = {
      "libvfio-user-0.0.1" = "sha256-svN/vMYeGt+WE4I3+J3HUbREug0udvqvglgp7kQoNHo=";
    };
  };
  nativeBuildInputs = with pkgs; [
    # cmake
    ninja
    meson
    pkg-config
  ];
  buildInputs = with pkgs; [
    # openssl
    # tbb
    # libbsd
    # numactl
    # luajit
    # libpcap
    # boost

    json_c
    cmocka

    libclang.lib
  ];
  hardeningDisable = [ "all" ];
  dontUseNinjaBuild = true;
  cargoSha256 = "";
  LIBCLANG_PATH = "${pkgs.libclang.lib}/lib";
  RUST_BACKTRACE= "1";

}
