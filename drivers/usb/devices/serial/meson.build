posix_bragi = cxxbragi.process(protos/'posix/posix.bragi')

src = [
	'src/main.cpp',
	'src/ch341/ch341.cpp',
	'src/cp2102/cp2102.cpp',
	'src/ft232/ft232.cpp',
	posix_bragi,
]

executable('usb_serial', src,
	dependencies : [ mbus_proto_dep, usb_proto_dep, fs_proto_dep, svrctl_proto_dep, core_dep, kerncfg_proto_dep ],
	install : true,
)

custom_target('usb-serial-server',
	command : [bakesvr, '-o', '@OUTPUT@', '@INPUT@'],
	output : 'usb-serial.bin',
	input : 'usb-serial.yml',
	install : true,
	install_dir : server
)
