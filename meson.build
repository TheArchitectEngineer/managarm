project('managarm',
	license : 'MIT',
	meson_version : '>=1.4.0',
	default_options : [
		'cpp_std=gnu++23',
		'c_std=gnu11',
		'warning_level=2'
	]
)

add_project_arguments('-Wimplicit-fallthrough', '-Werror=misleading-indentation', '-Werror=switch', language: ['c', 'cpp'])
add_project_arguments('-Wno-missing-field-initializers', language: 'cpp')

# build documentation
if get_option('build_docs')
	subdir('docs')

	doxyfile = configure_file(
		input : 'hel/Doxyfile.in',
		output : 'Doxyfile',
		configuration : {
			'ROOTDIR': meson.current_source_dir()/'hel'
		}
	)

	doxygen = find_program('doxygen', required : false)
	if doxygen.found()
		custom_target('hel-doxygen',
			input : doxyfile,
			output : 'hel-api',
			depend_files : 'hel/include/hel.h',
			command : [ doxygen, doxyfile ]
		)
	endif
endif

build_kernel = get_option('build_kernel')
build_uefi = get_option('build_uefi')
build_drivers = get_option('build_drivers')
build_tools = get_option('build_tools')
build_testsuite = get_option('build_testsuite')
provide_deps = get_option('provide_deps')
any_userspace = build_drivers or build_tools

if build_kernel
	add_project_link_arguments(['-Wl,--orphan-handling=warn', '-Wl,-znoexecstack'], language: ['c', 'cpp'])
endif

if build_kernel or build_drivers
	add_languages('c', 'cpp', native: false)
endif

if build_tools or build_testsuite
	add_languages('c', 'cpp')
endif

if build_kernel and any_userspace
	error('Cannot build kernel and userspace components at the same time')
endif

# Set flavor = {'kernel', 'userspace', 'none'}.
# Use this variable below if we need conflicting settings in each of the cases.
if build_kernel
	flavor = 'kernel'
elif any_userspace
	flavor = 'userspace'
else
	flavor = 'none'
endif

summary('Build flavor', flavor, section : 'Configuration')

if flavor == 'kernel'
	if not meson.is_cross_build()
		error('Kernel can only be cross-compiled')
	endif
elif flavor == 'userspace'
	summary({'System' : build_drivers,
			'Tools' : build_tools},
		section : 'Userspace Components', bool_yn : true)
endif

if not build_kernel and not build_drivers and not build_tools and not provide_deps
	subdir_done()
endif

if provide_deps and (build_kernel or any_userspace)
	error('The \'provide_deps\' option cannot be used with any \'build_*\' option')
endif

# declare constants that subdirs are going to use
if not provide_deps
	c = meson.get_compiler('cpp')
	cxx = meson.get_compiler('cpp')
endif
arch = host_machine.cpu_family()
protos = meson.project_source_root()/'protocols'
server = get_option('libdir')/'managarm/server'
kasan = get_option('kernel_kasan')
ubsan = get_option('kernel_ubsan')
log_alloc = get_option('kernel_log_allocations')
frame_pointers = get_option('kernel_frame_pointers')

supported_archs = [
	'aarch64',
	'riscv64',
	'x86_64'
]
if not supported_archs.contains(arch)
	error('unknown architecture ' + arch)
endif

bragi = find_program('bragi')

frigg = dependency('frigg',
	default_options : [ 'frigg_no_install=true' ],
	fallback: ['frigg', 'frigg_dep'],
	required: (not provide_deps)
)

cxxbragi = generator(bragi,
	output : '@BASENAME@.bragi.hpp',
	arguments : [ '-o', '@OUTPUT@', '@INPUT@', 'cpp', '-l', 'stdc++', '--protobuf' ]
)

frgbragi = generator(bragi,
	output : '@BASENAME@.frigg_bragi.hpp',
	arguments : [ '-o', '@OUTPUT@', '@INPUT@', 'cpp', '-l', 'frigg', '--protobuf' ]
)

freestnd_c_hdrs_dep = dependency(
	'freestnd-c-hdrs-' + host_machine.cpu_family(),
	required: build_uefi,
	method: 'pkg-config'
)

freestnd_cxx_hdrs_dep = dependency(
	'freestnd-cxx-hdrs-' + host_machine.cpu_family(),
	required: build_uefi,
	method: 'pkg-config'
)

# declare dependencies that subdirs are going to use
bragi_dep = subproject('bragi', default_options: ['install_headers=false']).get_variable('bragi_dep')

# If we are building the kernel, we need the freestanding subset of libarch.
if flavor == 'kernel' or flavor == 'none'
	libarch_opts = [ 'install_headers=false', 'header_only=true' ]
elif flavor == 'userspace'
	libarch_opts = [ 'install_headers=false' ]
endif

if build_kernel or build_drivers
	libarch = subproject('libarch',
		default_options : libarch_opts
	).get_variable('libarch_dep')
endif

if build_kernel
	cralgo = subproject('cralgo')
	uacpi = subproject('uacpi')
	libasync = subproject('libasync', default_options : [ 'install_headers=false' ])
	cralgo_sources = cralgo.get_variable('sources')
	cralgo_includes = cralgo.get_variable('includes')
	uacpi_sources = uacpi.get_variable('sources')
	uacpi_includes = uacpi.get_variable('includes')
	libasync_dep = libasync.get_variable('libasync_dep')

	subdir('hel')
	subdir('kernel/eir')
	subdir('kernel/thor')
endif

if build_drivers
	bakesvr = find_program('bakesvr')

	libudev_dep = dependency('libudev')
	cli11_dep = dependency('CLI11')
endif

if build_drivers or provide_deps
	subdir('protocols/posix')

	# this produces the helix dependency which
	# all other protocols depend on
	subdir('hel')

	subdir('protocols/mbus')
	subdir('protocols/kerncfg')
	subdir('protocols/clock')

	if build_drivers
		subdir('core')
	endif

	protocols = [ 'ostrace', 'fs', 'hw', 'usb', 'svrctl', 'kernlet' ]
	core = [ 'core/drm', 'core/virtio', 'mbus' ]
	posix = [ 'subsystem', 'init' ]
	drivers = [
		# libraries
		'libblockfs', 'libevbackend',
		# storage
		'block/ata', 'block/virtio-blk', 'block/ahci', 'block/nvme',
		# net
		'nic/usb_net', 'nic/rtl8168', 'nic/freebsd-e1000',
		# gfx
		'gfx/bochs', 'gfx/intel', 'gfx/virtio', 'gfx/plainfb', 'gfx/vmware', 'gfx/nvidia-open',
		# io
		'kbd', 'tty/virtio-console', 'uart',
		# usb
		'usb/hcds/uhci', 'usb/hcds/ehci', 'usb/hcds/xhci',
		'usb/devices/hid', 'usb/devices/storage', 'usb/devices/serial',
		# misc
		'kernletcc'
	]
	utils = [ 'runsvr', 'lsmbus', 'wait-for-devices' ]

	# delay these dirs until last as they require other libs
	# to already be built
	delay = [ 'drivers/nic/virtio', 'servers/netserver', 'drivers/clocktracker' ]

	foreach dir : protocols
		subdir('protocols'/dir)
	endforeach
endif

if build_drivers
	foreach dir : core
		subdir(dir)
	endforeach

	foreach dir : posix
		subdir('posix'/dir)
	endforeach

	foreach dir : drivers
		subdir('drivers'/dir)
	endforeach

	foreach dir : utils
		subdir('utils'/dir)
	endforeach

	foreach dir : delay
		subdir(dir)
	endforeach

	rules = [
		'drivers/gfx/90-managarm-vga.rules',
		'drivers/nic/90-managarm-nic.rules',
		'drivers/usb/90-managarm-usb.rules',
		'drivers/block/90-managarm-block.rules'
	]

	install_data(rules, install_dir : 'lib/udev/rules.d')

	units = [
		'drivers/kbd/runsvr-atkbd.service',
		'drivers/usb/runsvr-usbhid.service',
	]

	install_data(units, install_dir : 'lib/systemd/system')
endif

if build_testsuite
	testsuites = ['posix-tests']

	if host_machine.system() == 'managarm'
		testsuites += ['kernel-bench', 'kernel-tests', 'posix-torture', 'virt-test']
	endif

	foreach dir : testsuites
		subdir('testsuites'/dir)
	endforeach
endif

# when building these tools make sure they stay below everything else
# as they depend on parts above
if build_tools
	cli11_dep = dependency('CLI11')

	foreach tool : [ 'ostrace', 'bakesvr' ]
		subdir('tools'/tool)
	endforeach
endif
