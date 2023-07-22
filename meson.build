project('vmux', ['c' ,'cpp'],
  version : '0.1',
  default_options : ['warning_level=3', 'cpp_std=c++20'])

#incdir = include_directories('deps/libvfio-user/include')
incdir = include_directories('src')

libvfio_user_cppflags = [
  '-Wno-gnu',
  '-Wno-c11-extensions',
  '-Wno-c99-extensions',
  '-Wno-nested-anon-types',
  '-Wno-flexible-array-extensions'
]

libvfio_user = subproject('libvfio-user')
libvfio_user_dep = libvfio_user.get_variable('libvfio_user_dep')

sources = files()
subdir('src')
# cxx = meson.get_compiler('cpp')
boost_dep = dependency('boost')

exe = executable('vmux', 'src/main.cpp',
  sources,
  include_directories : incdir,
  cpp_args : libvfio_user_cppflags,
  # dependencies : [libvfio_user_dep, cxx.find_library('boost_fiber')],
  # link_args : '-lboost',
  link_args : ['-lboost_fiber', '-lboost_context'],
  dependencies : [libvfio_user_dep, boost_dep],
  install : true)

test('basic', exe)
