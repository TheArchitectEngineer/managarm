# other architectures would need different flags in the subproject and
# support in the bindings, but at least aarch64 should be doable
# there is an open PR for risc64 support, but apparently nvidia code would need
# cleanups because they assume riscv64 means running on GSP :^)
# https://github.com/NVIDIA/open-gpu-kernel-modules/pull/152

if host_machine.cpu_family() != 'x86_64'
	subdir_done()
endif

driver_src = files(
	'src/drm.cpp',
	'src/main.cpp',
	'src/utils.cpp',
	'src/sysdeps/nvkms.cpp',
	'src/sysdeps/nvlink.cpp',
	'src/sysdeps/os-interface.cpp',
	'src/sysdeps/libspdm.c',
)

import = subproject('nvidia-open')

executable('gfx_nvidia_open', driver_src,
	dependencies : [drm_core_dep, svrctl_proto_dep, import.get_variable('nvidia_open_lib_dep')],
	include_directories : ['src'],
	cpp_args : import.get_variable('nvidia_open_cpp_args'),
	install : true
)

custom_target('gfx-nvidia-open-server',
	command : [bakesvr, '-o', '@OUTPUT@', '@INPUT@'],
	output : 'gfx-nvidia-open.bin',
	input : 'gfx-nvidia-open.yml',
	install : true,
	install_dir : server
)
