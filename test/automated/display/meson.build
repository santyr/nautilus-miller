tests = {
  'test-miller-view': {'is_parallel': false, 'env': ['GTK_A11Y=test']},
  'test-miller-state': {'is_parallel': false, 'env': ['GTK_A11Y=test']},
  'test-miller-preview': {'is_parallel': false, 'env': ['GTK_A11Y=test']},
  'test-miller-input': {'is_parallel': false, 'env': ['GTK_A11Y=test']},
  'test-miller-typeahead-engine': {'is_parallel': false, 'env': ['GTK_A11Y=test']},
  'test-miller-typeahead-ui': {'is_parallel': false, 'env': ['GTK_A11Y=test', 'GTK_IM_MODULE=simple']},
  'test-nautilus-directory-async': {},
  'test-file-operations-create': {},
  'test-files-view': {
    'suite': ['tracker'],
    'tracker': true,
    'is_parallel' : false,
  },
  'test-sidebar': {
    'is_parallel' : false,
  },
  'test-portal-file-chooser': {
    'is_parallel' : false,
  },
}

foreach test_name, extra_args : tests
  source = test_name + '.c'
  suite = ['display'] + extra_args.get('suite', [])
  test_exe = executable(test_name, source, dependencies: [libnautilus_dep, libtestutils_dep])
  args = []
  exe = []
  is_parallel = extra_args.get('is_parallel', true)

  if extra_args.get('tracker', false)
    # Tests that read and write from the Tracker index are run using 'tracker-sandbox'
    # script to use a temporary instance of tracker-miner-fs instead of the session one.
    exe = tracker_sandbox
    args = ['test-sandbox', '--store-tmpdir', '--index-recursive-tmpdir', test_exe]
  else
    exe = test_exe
  endif

  test(
    test_name,
    exe,
    args: args,
    depends: gsettings_schemas,
    env: [
      test_env,
      extra_args.get('env', []),
      'G_TEST_BUILDDIR=@0@'.format(meson.current_build_dir()),
      'G_TEST_SRCDIR=@0@'.format(meson.current_source_dir())
    ],
    is_parallel: is_parallel,
    timeout: 480,
    suite: suite,
  )
endforeach
