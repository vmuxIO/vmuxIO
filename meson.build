project('vmux', 'c',
  version : '0.1',
  default_options : ['warning_level=3'])

#incdir = include_directories('deps/libvfio-user/include')

libvfio_user = subproject('libvfio-user')
libvfio_user_dep = libvfio_user.get_variable('libvfio_user_dep')

exe = executable('vmux', 'src/main.c',
  #include_directories : incdir,
  dependencies : libvfio_user_dep,
  install : true)

test('basic', exe)
