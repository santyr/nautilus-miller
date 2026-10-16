test_utils = files('test-utilities.c', 'miller-test-input.c')

libtestutils = static_library(
  'testutils',
  test_utils,
  dependencies: libnautilus_dep,
  include_directories: include_directories('.')
)

libtestutils_dep = declare_dependency(
  link_with: libtestutils,
  include_directories: include_directories('.'),
  dependencies: libnautilus_dep,
  sources: resources
)

subdir('displayless')
if get_option('tests') == 'all'
  subdir('display')
endif
