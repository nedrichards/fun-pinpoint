desktop_conf = configuration_data()
desktop_conf.set('APPLICATION_ID', pinpoint_id)
desktop_conf.set('TAGLINE', pinpoint_tagline)

desktop_file = configure_file(
  input: 'com.nedrichards.pinpoint.desktop.in',
  output: '@BASENAME@',
  configuration: desktop_conf,
  install: true,
  install_dir: get_option('datadir') / 'applications',
)

metainfo_conf = configuration_data()
metainfo_conf.set('TAGLINE', pinpoint_tagline)
metainfo_conf.set('VERSION', pinpoint_version)

metainfo_file = configure_file(
  input: 'com.nedrichards.pinpoint.metainfo.xml.in',
  output: '@BASENAME@',
  configuration: metainfo_conf,
  install: true,
  install_dir: get_option('datadir') / 'metainfo',
)

install_data(
  'com.nedrichards.pinpoint.svg',
  install_dir: get_option('datadir') / 'icons/hicolor/scalable/apps',
)

install_data(
  'com.nedrichards.pinpoint.xml',
  install_dir: get_option('datadir') / 'mime/packages',
)

pinpoint_language = files('pinpoint.lang')

install_data(
  pinpoint_language,
  install_dir: get_option('datadir') / 'gtksourceview-5/language-specs',
)

desktop_file_validate = find_program('desktop-file-validate', required: false)
if desktop_file_validate.found()
  test('validate desktop file', desktop_file_validate, args: desktop_file)
endif

appstreamcli = find_program('appstreamcli', required: false)
if appstreamcli.found()
  test('validate metainfo', appstreamcli, args: ['validate', '--no-net', metainfo_file])
endif
