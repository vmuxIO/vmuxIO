{ pkgs, stdenv, ... }: stdenv.mkDerivation rec {
  name = "kmod-tools";

  src = builtins.fetchGit {
    url = "git@github.com:cirosantilli/linux-kernel-module-cheat.git";
    rev = "25f9913e0c1c5b4a3d350ad14d1de9ac06bfd4be";
  };

  sourceRoot = "source/kernel_module/user";
  installPhase = ''
    mkdir -p $out/bin
    find *.out | cut -d. -f1 | xargs -I "{}" mv {}.out $out/bin/${name}-{}
  '';

  meta = {
    description = "some simple cli userspace apps for kernel development, most notably `kmod-tools-virt_to_phys_user`";
    homepage = "https://github.com/cirosantilli/linux-kernel-module-cheat/blob/25f9913e0c1c5b4a3d350ad14d1de9ac06bfd4be/kernel_module/user/README.md";
    license = pkgs.lib.licenses.gpl3;
  };
}
