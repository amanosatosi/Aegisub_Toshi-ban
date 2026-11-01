i18n = import('i18n')
# Meson compiles and installs catalogs. Use make_pot.sh for the full template:
# the built-in pot target only sees the C++ files listed in POTFILES.
i18n.gettext('aegisub',
	         args: [
	            '-k_', '-kSTR_MENU', '-kSTR_DISP', '-kSTR_HELP',
	            '-kfmt_tl', '-kfmt_plural:2,3', '-kwxPLURAL:1,2', '-kwxTRANSLATE', '-kmotion_message',
	            '--sort-by-file'
	         ],
	         install_dir: localedir)
