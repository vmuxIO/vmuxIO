{ self
, linux-firmware-pinned
, pkgs
, linux
}:
let
  srcpack = {
    moongen = self.inputs.moongen-lachnit-src;
    libmoon = self.inputs.libmoon-lachnit-src;
    dpdk = self.inputs.dpdk-lachnit-src;
  };
in
pkgs.stdenv.mkDerivation {
  pname = "moongen-lachnit";
  version = "2021.07.17-21";

  src = srcpack.moongen;

  postUnpack = ''
    rm -r $sourceRoot/libmoon
    cp -r ${srcpack.libmoon} $sourceRoot/libmoon
    chmod -R u+w $sourceRoot/libmoon

    rm -r $sourceRoot/libmoon/deps/dpdk
    cp -r ${srcpack.dpdk} $sourceRoot/libmoon/deps/dpdk
    chmod -R u+w $sourceRoot/libmoon/deps/dpdk
  '';

  nativeBuildInputs = with pkgs; [
    cmake
    ninja
    meson
    openssl
    python3Packages.pyelftools
    (writeScriptBin "git" ''
        echo ignoring git command
    '')
  ];
  buildInputs = with pkgs; [
    openssl
    libbsd
    numactl
    luajit
    libpcap
  ];
  RTE_KERNELDIR = "${linux.dev}/lib/modules/${linux.modDirVersion}/build";
  CXXFLAGS = "-std=gnu++14"; # libmoon->highwayhash->tbb needs <c++17

  dontConfigure = true;

  postPatch = ''
    ls -la ./libmoon
    patchShebangs ./libmoon/build.sh ./build.sh


    # patch libmoon's include dirs
    substituteInPlace ./libmoon/build.sh \
      --replace "./bind-interfaces.sh \''${FLAGS}" "echo skipping bind-interfaces.sh"
    substituteInPlace ./libmoon/CMakeLists.txt \
      --replace '{CMAKE_CURRENT_SOURCE_DIR}/deps/dpdk/x86_64-native-linux-gcc/include' \
      '{CMAKE_CURRENT_SOURCE_DIR}/deps/dpdk/x86_64-native-linux-gcc/include
      ''${CMAKE_CURRENT_SOURCE_DIR}/deps/dpdk/x86_64-native-linux-gcc
      ''${CMAKE_CURRENT_SOURCE_DIR}/deps/dpdk/config
      ''${CMAKE_CURRENT_SOURCE_DIR}/deps/dpdk/lib/eal/linux/include
      ''${CMAKE_CURRENT_SOURCE_DIR}/deps/dpdk/lib/eal/x86/include
      ''${CMAKE_CURRENT_SOURCE_DIR}/deps/dpdk/lib/net
      ''${CMAKE_CURRENT_SOURCE_DIR}/deps/dpdk/lib/mbuf
      ''${CMAKE_CURRENT_SOURCE_DIR}/deps/dpdk/lib/mempool
      ''${CMAKE_CURRENT_SOURCE_DIR}/deps/dpdk/lib/ring
      ''${CMAKE_CURRENT_SOURCE_DIR}/deps/dpdk/lib/meter
      ''${CMAKE_CURRENT_SOURCE_DIR}/deps/dpdk/lib/security
      ''${CMAKE_CURRENT_SOURCE_DIR}/deps/dpdk/lib/kvargs
      ''${CMAKE_CURRENT_SOURCE_DIR}/deps/dpdk/lib/cryptodev
      ''${CMAKE_CURRENT_SOURCE_DIR}/deps/dpdk/lib/rcu
      ''${CMAKE_CURRENT_SOURCE_DIR}/deps/dpdk/lib/hash
      ''${CMAKE_CURRENT_SOURCE_DIR}/deps/dpdk/lib/pci'

    # patch moongen's include dirs
    substituteInPlace ./CMakeLists.txt \
      --replace '{CMAKE_CURRENT_SOURCE_DIR}/libmoon/deps/dpdk/lib/librte_ethdev' \
      '{CMAKE_CURRENT_SOURCE_DIR}/libmoon/deps/dpdk/lib/librte_ethdev
      ''${CMAKE_CURRENT_SOURCE_DIR}/libmoon/deps/dpdk/x86_64-native-linux-gcc
      ''${CMAKE_CURRENT_SOURCE_DIR}/libmoon/deps/dpdk/x86_64-native-linux-gcc/include
      ''${CMAKE_CURRENT_SOURCE_DIR}/libmoon/deps/dpdk/lib/ethdev
      ''${CMAKE_CURRENT_SOURCE_DIR}/libmoon/deps/dpdk/config
      ''${CMAKE_CURRENT_SOURCE_DIR}/libmoon/deps/dpdk/lib/net
      ''${CMAKE_CURRENT_SOURCE_DIR}/libmoon/deps/dpdk/lib/mbuf
      ''${CMAKE_CURRENT_SOURCE_DIR}/libmoon/deps/dpdk/lib/mempool
      ''${CMAKE_CURRENT_SOURCE_DIR}/libmoon/deps/dpdk/lib/ring
      ''${CMAKE_CURRENT_SOURCE_DIR}/libmoon/deps/dpdk/lib/meter
      ''${CMAKE_CURRENT_SOURCE_DIR}/libmoon/deps/dpdk/lib/eal/include
      ''${CMAKE_CURRENT_SOURCE_DIR}/libmoon/deps/dpdk/lib/eal/linux/include
      ''${CMAKE_CURRENT_SOURCE_DIR}/libmoon/deps/dpdk/lib/eal/x86/include'

    # add libraries to link with moongen
    substituteInPlace ./CMakeLists.txt \
      --replace ' ''${dpdk_STATIC_LDFLAGS}' \
      ' rte_net      rte_ethdev       rte_distributor    rte_mbuf      rte_net_bond       rte_net_ring     rte_power
        rte_acl      rte_eal          rte_kvargs     rte_mempool   rte_mempool_ring   rte_net_e1000        rte_net_virtio   rte_ring
        rte_cfgfile  rte_hash         rte_lpm        rte_meter     rte_net_i40e   rte_net_ice    rte_net_iavf    rte_sched        rte_timer
        rte_cmdline  rte_ip_frag      rte_pipeline   rte_net_ixgbe rte_mempool_stack  rte_port             rte_table
        rte_stack          rte_bus_vdev  rte_bus_pci        rte_pci              rte_cryptodev '

    # patch moongen's library search path
    substituteInPlace ./CMakeLists.txt \
      --replace '{CMAKE_CURRENT_SOURCE_DIR}/libmoon/deps/dpdk/x86_64-native-linux-gcc/lib' \
      '{CMAKE_CURRENT_SOURCE_DIR}/libmoon/deps/dpdk/x86_64-native-linux-gcc/lib
      ''${CMAKE_CURRENT_SOURCE_DIR}/libmoon/deps/dpdk/x86_64-native-linux-gcc/drivers'


    # patch dpdk to export symbols used by libmoon

    substituteInPlace ./libmoon/deps/dpdk/drivers/net/ice/version.map \
      --replace 'local: *;' \
        'global:
    ice_is_vsi_valid;
    ice_fill_dflt_direct_cmd_desc;
    ice_aq_send_cmd;
    ice_get_vsi_ctx;
    ice_get_lan_q_ctx;
    ice_sched_find_node_by_teid;
    ice_logtype_driver;
    ice_ptp_init_time;
    ice_cfg_q_bw_lmt;
    ice_cfg_vsi_bw_lmt_per_tc;
         local: *;'

    substituteInPlace ./libmoon/deps/dpdk/drivers/net/iavf/version.map \
      --replace 'local: *;' \
        'global:
    iavf_config_bw_limit_queue;
    iavf_get_ieee1588_tmst;
    iavf_config_bw_limit_port;
         local: *;'

    substituteInPlace ./libmoon/deps/dpdk/drivers/net/i40e/version.map \
      --replace 'global:' \
        'global:
    i40e_aq_get_link_info;
         '

    # patch dpdk to use our pinned e810 firmware

    substituteInPlace ./libmoon/deps/dpdk/drivers/net/ice/ice_ethdev.h \
      --replace '#define ICE_PKG_FILE_DEFAULT "/lib/firmware/intel/ice/ddp/ice.pkg"' \
      '#define ICE_PKG_FILE_DEFAULT "${linux-firmware-pinned}/lib/firmware/intel/ice/ddp/ice-1.3.26.0.pkg"'
    substituteInPlace ./libmoon/deps/dpdk/drivers/net/ice/ice_ethdev.h \
      --replace '#define ICE_PKG_FILE_SEARCH_PATH_DEFAULT "/lib/firmware/intel/ice/ddp/"' \
      '#define ICE_PKG_FILE_SEARCH_PATH_DEFAULT "${linux-firmware-pinned}/lib/firmware/intel/ice/ddp/"'
  '';

  buildPhase = ''
    ./build.sh
  '';

  hardeningDisable = [ "all" ];

  installPhase = ''
    mkdir -p $out/bin

    cp build/MoonGen $out/bin
    mkdir -p $out/bin/lua
    cp -r examples $out/bin
    cp -r flows $out/bin
    cp -r interface $out/bin
    cp -r lua $out/bin
    cp -r rfc2544 $out/bin
    mkdir -p $out/bin/libmoon
    cp -r libmoon $out/bin
    mkdir -p $out/lib/libmoon
    cp -r build/libmoon $out/lib/
    mkdir -p $out/lib/dpdk
    cp -r libmoon/deps/dpdk/x86_64-native-linux-gcc/lib $out/lib/dpdk
    cp -r libmoon/deps/dpdk/x86_64-native-linux-gcc/drivers $out/lib/dpdk
    mkdir -p $out/lib/luajit
    cp -r libmoon/deps/luajit/usr/local/lib $out/lib/luajit

    # autopatchelfHook?
    patchelf --shrink-rpath --allowed-rpath-prefixes /nix/store $out/bin/MoonGen
    patchelf --add-rpath $out/lib/libmoon $out/bin/MoonGen
    patchelf --add-rpath $out/lib/libmoon/tbb_cmake_build/tbb_cmake_build_subdir_release $out/bin/MoonGen
    patchelf --add-rpath $out/lib/dpdk/lib $out/bin/MoonGen
    patchelf --add-rpath $out/lib/dpdk/drivers $out/bin/MoonGen
    patchelf --add-rpath $out/lib/luajit/usr/local/lib $out/bin/MoonGen
  '';

  dontFixup = true;
}
