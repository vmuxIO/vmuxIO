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
  # postPatch = ''
  #   pwd
  #   substituteInPlace ../cargo-vendor-dir/libvfio-user-0.0.1/build.rs --replace \
  #     "bindgen::Builder::default()" "bindgen::Builder::default()::clang_arg('-x')::clang_arg('c++')"
  #
  # '';
  preBuild = ''
    pwd
    ls ..
    ls ../cargo-vendor-dir/libvfio-user-sys-0.0.1/libvfio-user
    # "bindgen::Builder::default()" "bindgen::Builder::default()::clang_arg('-x')::clang_arg('c++')"
    # substituteInPlace ../cargo-vendor-dir/libvfio-user-sys-0.0.1/build.rs --replace \
    #   'bindgen::Builder::default()' \
    #   'bindgen::Builder::default()::clang_arg("-x")::clang_arg("c++")'
    substituteInPlace ../cargo-vendor-dir/libvfio-user-sys-0.0.1/build.rs --replace \
      '.allowlist_file(header_path_str)' \
      ' '
    sed -i '126i\println!("foobar {:?}", env::var("LIBCLANG_PATH"));' ../cargo-vendor-dir/libvfio-user-sys-0.0.1/build.rs
    sed -i '134i\.clang_arg("-x").clang_arg("c++")' ../cargo-vendor-dir/libvfio-user-sys-0.0.1/build.rs
    sed -i '134i\.detect_include_paths(false)' ../cargo-vendor-dir/libvfio-user-sys-0.0.1/build.rs
    substituteInPlace Cargo.toml --replace \
      'default = ["build-binary"]' \
      'default = ["generate-bindings"]'
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
  postInstall = ''
    cp -r target/*/include $out/lib/
  '';
  hardeningDisable = [ "all" ];
  dontUseNinjaBuild = true;
  dontUseNinjaInstall = true;
  dontUseNinjaCheck = true;
  cargoSha256 = "";
  LIBCLANG_PATH = "${pkgs.libclang.lib}/lib";
  RUST_BACKTRACE= "1";
  # does nothing i think:
  # default = false;
  # generate-bindings = true;
}
