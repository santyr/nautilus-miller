# Track individual UI outputs; directory mtimes do not identify rebuilt files.
subdir('ui')

gresource_file = configure_file(
  input: 'nautilus.gresource.xml.in',
  output: 'nautilus.gresource.xml',
  configuration: file_conf
)

resources = gnome.compile_resources(
  'nautilus-resources',
  gresource_file,
  dependencies: [appdata, blueprints, gresource_file],
  source_dir: 'resources',
  c_name: 'nautilus',
  extra_args: '--manual-register'
)
