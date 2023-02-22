{ pkgs2211 }:
with pkgs2211;
qemu_full.overrideAttrs ( new: old: {
  src = fetchFromGitHub {
    owner = "oracle";
    repo = "qemu";
    rev = "b3b53245edbd399eb3ba1655d509478c76d37a8e";
    hash = "sha256-kCX2ByuJxERLY2nHjPndVoo7TQm1j4qrpLjRcs42HU4=";
    fetchSubmodules = true;
  };
  version = "7.1.5";
  buildInputs = [ libndctl ] ++ old.buildInputs;
  nativeBuildInputs = [ json_c cmocka ] ++ old.nativeBuildInputs;
  configureFlags = old.configureFlags ++ [
    "--enable-vfio-user-server"
    "--disable-gtk"
  ];
})
