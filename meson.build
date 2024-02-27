project('vmux', ['c' ,'cpp'],
  version : '0.1',
  default_options : ['warning_level=3', 'cpp_std=c++20', 'default_library=static'])

dont_build_libnic_emu = get_option('dont_build_libnic_emu')

#incdir = include_directories('deps/libvfio-user/include')
incdir = include_directories('src')

libvfio_user_cppflags = [
  '-Wno-gnu',
  '-Wno-c11-extensions',
  '-Wno-c99-extensions',
  '-Wno-nested-anon-types',
  '-Wno-flexible-array-extensions'
]

sims_flags = [
  '-Wno-extra-semi',
  '-Wno-unused-parameter',
  '-Wno-unused-variable',
  '-Wno-pointer-arith'
]

dpdk_flags = [
  '-msse4.1',
  '-mavx'
]

dpdk_link_args = [
  '-lrte_eal',
  '-lrte_ring', 
  '-lrte_mempool', 
  '-lrte_ethdev', 
  '-lrte_mbuf', 
  '-lrte_net', 
  '-lrte_latencystats', 
  '-lrte_cmdline', 
  '-lrte_net_bond', 
  '-lrte_metrics', 
  '-lrte_gso', 
  '-lrte_gro', 
  '-lrte_net_ixgbe', 
  '-lrte_net_i40e', 
  '-lrte_net_bnxt', 
  '-lrte_net_dpaa', 
  '-lrte_bpf', 
  '-lrte_bitratestats', 
  ]

libvfio_user = subproject('libvfio-user')
libvfio_user_dep = libvfio_user.get_variable('libvfio_user_dep')

if dont_build_libnic_emu
  nic_emu_dep = declare_dependency(
    link_args : ['-L./libnic_emu.a', '-lnic_emu'],
    )
else 
  nic_emu = subproject('nic-emu')
  nic_emu_dep = nic_emu.get_variable('nic_emu_dep')
endif

sources = files()
subdir('src')
# cxx = meson.get_compiler('cpp')
boost_dep = dependency('boost')

exe = executable('vmux', 'src/main.cpp',
  sources,
  include_directories : incdir,
  cpp_args : libvfio_user_cppflags + sims_flags + dpdk_flags,
  c_args : sims_flags,
  # dependencies : [libvfio_user_dep, cxx.find_library('boost_fiber')],
  # link_args : '-lboost',
  link_args : ['-lboost_fiber', '-lboost_context', '-lboost_timer', '-lboost_chrono'] + dpdk_link_args,
  dependencies : [libvfio_user_dep, boost_dep, nic_emu_dep],
  install : true)

test('basic', exe)
