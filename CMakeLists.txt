cmake_minimum_required(VERSION 3.21)
project(ndsx LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
add_compile_definitions(MSPACES=1)

# Setup build target to create modified libnds with renamed symbols
set(OBJCOPY_COMMAND "$ENV{WONDERFUL_TOOLCHAIN}/toolchain/gcc-arm-none-eabi/bin/arm-none-eabi-objcopy")
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
	set(LIBNDS9_FILENAME "libnds9d.a")
else()
	set(LIBNDS9_FILENAME "libnds9.a")
endif()
add_custom_command(
	OUTPUT "${CMAKE_BINARY_DIR}/libnds9-prefixed.a"
	COMMAND "${OBJCOPY_COMMAND}"
		"--redefine-syms=${CMAKE_SOURCE_DIR}/libnds_sym_renames.txt"
		"$ENV{BLOCKSDS}/libs/libnds/lib/${LIBNDS9_FILENAME}"
		"${CMAKE_BINARY_DIR}/libnds9-prefixed.a"
	DEPENDS
		"${CMAKE_SOURCE_DIR}/libnds_sym_renames.txt"
		"$ENV{BLOCKSDS}/libs/libnds/lib/${LIBNDS9_FILENAME}"
)
add_custom_target(nds9-prefixed-build DEPENDS "${CMAKE_BINARY_DIR}/libnds9-prefixed.a")
add_library(nds9-prefixed STATIC IMPORTED)
add_dependencies(nds9-prefixed nds9-prefixed-build)
set_target_properties(nds9-prefixed PROPERTIES IMPORTED_LOCATION "${CMAKE_BINARY_DIR}/libnds9-prefixed.a")

# remove `-Wl,-lnds9` so we can link to our modified libnds instead
set(CMAKE_CXX_STANDARD_LIBRARIES "-Wl,--start-group -Wl,-lstdc++ -Wl,-lc -Wl,--end-group")

set(SYMBOLS
	gettimeofday fputs getpid getppid setenv unsetenv libnds_close keyboardSetCanonicalMode

	# dash symbols
	__aeabi_d2iz __aeabi_ddiv __aeabi_dmul _ctype_b atoi bsearch closedir dup dup2 execve fcntl fork fstat getcwd getegid geteuid getgid getgroups getpgrp getuid isatty iswblank iswctype kill killpg lstat mbrlen mbrtowc mbsrtowcs mempcpy memrchr opendir pipe putenv qsort readdir realloc setlocale setpgid sigaction signal sigprocmask sigsuspend stat stpncpy strcasecmp strchrnul strcoll strcspn strpbrk strsignal strspn strtod strtoimax strtok strtoumax sysconf tcgetattr tcgetpgrp tcsetpgrp times umask vfork vsnprintf wcschr wctype

	# busybox symbols
	fwrite __bswapdi2 __bswapsi2 access alarm bind chmod chown chroot connect endgrent execv execvp fchdir fgets fileno fnmatch freeaddrinfo freopen getaddrinfo getgrgid getgrnam gethostbyname getline getnameinfo getopt getpeername getpwnam getpwuid getrlimit getsockname glob globfree h_errno hstrerror inet_aton inet_ntoa initgroups ioctl lchown link listen mallopt memchr mkdir mknod mkstemp mmap nanosleep optarg opterr optind optopt poll ppoll rand readlink realpath regcomp regexec regfree rename rmdir sched_getaffinity sendto setegid seteuid setgid setpgrp setrlimit setsid setsockopt settimeofday setuid sleep snprintf socket sprintf srand stdin strftime strncasecmp strndup strrchr strtoll strtoull symlink tcflush tcsetattr ttyname_r uname unlink utimes vasprintf wait
)

file(GLOB_RECURSE NDSX_SOURCES src/kernel/*.cpp)
add_executable(ndsx ${NDSX_SOURCES})
target_include_directories(ndsx PRIVATE include)
target_link_libraries(ndsx PRIVATE nds9-prefixed dswifi9)
foreach(SYMBOL IN LISTS SYMBOLS)
	# Force this symbol to be included in the binary
	target_link_options(ndsx PRIVATE "-Wl,--undefined=${SYMBOL}")
endforeach()
nds_create_rom(ndsx)

function(create_ndsx_program_from_library TARGET)
	blocksds_create_dsl(
		${TARGET}
		MAIN_TARGET ndsx
		${ARGN}
	)
	if(NOT "$ENV{CI}")
		make_directory("${CMAKE_SOURCE_DIR}/sdcard")
		add_custom_command(
			OUTPUT "${CMAKE_SOURCE_DIR}/sdcard/${TARGET}.dsl"
			COMMAND ${CMAKE_COMMAND} -E create_symlink
				"${${TARGET}_DSL}" # blocksds_create_dsl() returns ABSOLUTE paths to the generated DSLs!
				"${CMAKE_SOURCE_DIR}/sdcard/${TARGET}.dsl"
			MAIN_DEPENDENCY "${${TARGET}_DSL}"
		)
		add_custom_target(link_${TARGET}_into_sdcard ALL DEPENDS "${CMAKE_SOURCE_DIR}/sdcard/${TARGET}.dsl")
	endif()
endfunction()

function(create_ndsx_program_from_sources TARGET SOURCES)
	add_library(${TARGET} STATIC ${SOURCES})
	create_ndsx_program_from_library(${TARGET} ${ARGN})
endfunction()

create_ndsx_program_from_sources(init_process src/programs/init.cpp)
create_ndsx_program_from_sources(my_prog src/programs/my_prog.cpp)
create_ndsx_program_from_sources(env-test src/programs/env-test.cpp)
create_ndsx_program_from_sources(argv-test src/programs/argv-test.cpp)
create_ndsx_program_from_sources(jailbreak-test src/programs/jailbreak-test.cpp)
create_ndsx_program_from_sources(arrow-keys src/programs/arrow-keys.cpp)
create_ndsx_program_from_sources(canonical-mode-test src/programs/canonical-mode-test.cpp)

add_library(dash STATIC IMPORTED GLOBAL)
set_target_properties(dash PROPERTIES IMPORTED_LOCATION "${CMAKE_SOURCE_DIR}/../nds-dash/src/libdash.a")
create_ndsx_program_from_library(dash)

add_library(busybox STATIC IMPORTED GLOBAL)
set_target_properties(busybox PROPERTIES IMPORTED_LOCATION "${CMAKE_SOURCE_DIR}/../busybox/libbusybox.a")
create_ndsx_program_from_library(busybox)
