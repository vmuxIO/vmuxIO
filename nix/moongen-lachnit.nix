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

    substituteInPlace ./CMakeLists.txt \
      --replace ' ''${dpdk_STATIC_LDFLAGS}' \
      ' rte_net      rte_ethdev       rte_distributor    rte_mbuf      rte_net_bond       rte_net_ring     rte_power
  rte_acl      rte_eal          rte_kvargs     rte_mempool   rte_mempool_ring   rte_net_e1000        rte_net_virtio   rte_ring
  rte_cfgfile  rte_hash         rte_lpm        rte_meter     rte_net_i40e   rte_net_ice    rte_net_iavf    rte_sched        rte_timer
  rte_cmdline  rte_ip_frag      rte_pipeline   rte_net_ixgbe rte_mempool_stack  rte_port             rte_table
        rte_stack          rte_bus_vdev  rte_bus_pci        rte_pci              rte_cryptodev '
      # ' -lrte_net -lrte_ethdev -lrte_eal -lrte_ring -lrte_mempool -lrte_mbuf -lrte_latencystats -lrte_cmdline -lrte_metrics -lrte_gso -lrte_gro -lrte_bpf -lrte_bitratestats -lbsd -lrte_net_ice -lrte_net_iavf'

    substituteInPlace ./CMakeLists.txt \
      --replace '{CMAKE_CURRENT_SOURCE_DIR}/libmoon/deps/dpdk/x86_64-native-linux-gcc/lib' \
      '{CMAKE_CURRENT_SOURCE_DIR}/libmoon/deps/dpdk/x86_64-native-linux-gcc/lib
      ''${CMAKE_CURRENT_SOURCE_DIR}/libmoon/deps/dpdk/x86_64-native-linux-gcc/drivers'

    # substituteInPlace ./libmoon/CMakeLists.txt \
    #   --replace ' ''${dpdk_STATIC_LDFLAGS}' \
    #   ' -lrte_net_ice -lrte_net '
    substituteInPlace ./libmoon/CMakeLists.txt \
      --replace ' ''${dpdk_STATIC_LDFLAGS}' \
      ' rte_net      rte_ethdev       rte_distributor    rte_mbuf      rte_net_bond       rte_net_ring     rte_power
  rte_acl      rte_eal          rte_kvargs     rte_mempool   rte_mempool_ring   rte_net_e1000        rte_net_virtio   rte_ring
  rte_cfgfile  rte_hash         rte_lpm        rte_meter     rte_net_i40e   rte_net_ice    rte_net_iavf    rte_sched        rte_timer
  rte_cmdline  rte_ip_frag      rte_pipeline   rte_net_ixgbe rte_mempool_stack  rte_port             rte_table
        rte_stack          rte_bus_vdev  rte_bus_pci        rte_pci              rte_cryptodev '
    # substituteInPlace ./libmoon/CMakeLists.txt \
    #   --replace 'TARGET_LINK_LIBRARIES(libmoon' \
    #   'TARGET_LINK_LIBRARIES('
    # substituteInPlace ./libmoon/CMakeLists.txt \
    #   --replace 'rt)' \
    #   'libmoon rt)'
    # substituteInPlace ./libmoon/CMakeLists.txt \
    #   --replace 'ADD_EXECUTABLE(libmoon ''${FILES})' \
    #   ' '
    # substituteInPlace ./libmoon/CMakeLists.txt \
    #   --replace 'rt)' \
    #   'rt)
    #   ADD_EXECUTABLE(libmoon ''${FILES})'

    # substituteInPlace ./libmoon/CMakeLists.txt \
    #   --replace '{dpdk_STATIC_LINK_DIRS}' \
    #   '{dpdk_STATIC_LINK_DIRS}
    #   ''${CMAKE_CURRENT_SOURCE_DIR}/deps/dpdk/x86_64-native-linux-gcc/drivers'

    # substituteInPlace ./libmoon/build.sh \
    #   --replace 'NUM_CPUS=$(cat /proc/cpuinfo  | grep "processor\\s: " | wc -l)' \
    #   'NUM_CPUS=1'

    # substituteInPlace ./libmoon/deps/dpdk/drivers/net/ice/base/ice_switch.h \
    #   --replace 'bool ice_is_vsi_valid(struct' \
    #     'extern bool ice_is_vsi_valid(struct'

    # substituteInPlace ./libmoon/deps/dpdk/drivers/net/ice/version.map \
    #   --replace 'local: *;' \
    #     'global:
    # ice_is_vsi_valid;
    # ice_fill_dflt_direct_cmd_desc;
    # ice_aq_send_cmd;
    # ice_get_vsi_ctx;
    # ice_get_lan_q_ctx;
    # ice_sched_find_node_by_teid;
    # ice_logtype_driver;
    # ice_ptp_init_time;
    # ice_cfg_q_bw_lmt;
    # ice_cfg_vsi_bw_lmt_per_tc;
    #      local: *;'

    # substituteInPlace ./libmoon/deps/dpdk/drivers/net/iavf/version.map \
    #   --replace 'local: *;' \
    #     'global:
    # iavf_config_bw_limit_queue;
    # iavf_get_ieee1588_tmst;
    #      local: *;'
    #
    # substituteInPlace ./libmoon/deps/dpdk/drivers/net/i40e/version.map \
    #   --replace 'local: *;' \
    #     'global:
    # i40e_aq_get_link_info;
    #      local: *;'

    substituteInPlace ./libmoon/deps/dpdk/drivers/net/ice/ice_ethdev.h \
      --replace '#define ICE_PKG_FILE_DEFAULT "/lib/firmware/intel/ice/ddp/ice.pkg"' \
      '#define ICE_PKG_FILE_DEFAULT "${linux-firmware-pinned}/lib/firmware/intel/ice/ddp/ice-1.3.26.0.pkg"'
    substituteInPlace ./libmoon/deps/dpdk/drivers/net/ice/ice_ethdev.h \
      --replace '#define ICE_PKG_FILE_SEARCH_PATH_DEFAULT "/lib/firmware/intel/ice/ddp/"' \
      '#define ICE_PKG_FILE_SEARCH_PATH_DEFAULT "${linux-firmware-pinned}/lib/firmware/intel/ice/ddp/"'
  '';

  NIX_LDFLAGS = " -yice_is_vsi_valid --verbose ";

  buildPhase = ''
    # export dpdk_STATIC_LDFLAGS=" -lrte_eal -lrte_ring -lrte_mempool -lrte_ethdev -lrte_mbuf -lrte_net -lrte_latencystats -lrte_cmdline -lrte_net_bond -lrte_metrics -lrte_gso -lrte_gro -lrte_net_ixgbe -lrte_net_i40e -lrte_net_bnxt -lrte_net_dpaa -lrte_bpf -lrte_bitratestats -ljansson -lbsd"
    NIX_DEBUG=1 ./build.sh
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
    mkdir -p $out/lib/highwayhash
    cp -r libmoon/deps/highwayhash/lib $out/lib/highwayhash

    # autopatchelfHook?
    patchelf --shrink-rpath --allowed-rpath-prefixes /nix/store $out/bin/MoonGen
    patchelf --add-rpath $out/lib/libmoon $out/bin/MoonGen
    patchelf --add-rpath $out/lib/libmoon/tbb_cmake_build/tbb_cmake_build_subdir_release $out/bin/MoonGen
    patchelf --add-rpath $out/lib/dpdk/lib $out/bin/MoonGen
    patchelf --add-rpath $out/lib/dpdk/drivers $out/bin/MoonGen
    patchelf --add-rpath $out/lib/luajit/usr/local/lib $out/bin/MoonGen
    patchelf --add-rpath $out/lib/highwayhash/lib $out/bin/MoonGen
  '';

  dontFixup = true;
}
