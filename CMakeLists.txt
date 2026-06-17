cmake_minimum_required(VERSION 3.21)
project(ndsx LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
add_compile_definitions(MSPACES=1)

# prefix the libnds syscall symbols
# TODO: make this a custom command/target so that it depends on libnds_sym_renames.txt
set(OBJCOPY_COMMAND "$ENV{WONDERFUL_TOOLCHAIN}/toolchain/gcc-arm-none-eabi/bin/arm-none-eabi-objcopy")
execute_process(
	COMMAND "${OBJCOPY_COMMAND}"
		"--redefine-syms=${CMAKE_SOURCE_DIR}/libnds_sym_renames.txt"
		"$ENV{BLOCKSDS}/libs/libnds/lib/libnds9.a"
		"${CMAKE_BINARY_DIR}/libnds9-prefixed.a"
	COMMAND_ERROR_IS_FATAL ANY
)
add_library(nds9-prefixed STATIC IMPORTED)
set_target_properties(nds9-prefixed PROPERTIES
	IMPORTED_LOCATION "${CMAKE_BINARY_DIR}/libnds9-prefixed.a"
)
# remove `-Wl,-lnds9` so we can link to our modified libnds instead
set(CMAKE_CXX_STANDARD_LIBRARIES "-Wl,--start-group -Wl,-lstdc++ -Wl,-lc -Wl,--end-group")

set(SYMBOLS
	gettimeofday fputs getpid getppid setenv unsetenv libnds_close

	# dash symbols
	__aeabi_d2iz __aeabi_ddiv __aeabi_dmul _ctype_b atoi bsearch closedir dup dup2 execve fcntl fork fstat getcwd getegid geteuid getgid getgroups getuid isatty iswblank iswctype kill longjmp lstat mbrlen mbrtowc mbsrtowcs memmove mempcpy memrchr opendir pipe qsort readdir realloc setjmp setlocale sigaction signal sigprocmask sigsuspend stat stpncpy strcasecmp strchrnul strcoll strcspn strpbrk strsignal strspn strtod strtoimax strtok strtoumax sysconf tcgetattr times umask vfork vsnprintf wcschr wctype
)

add_executable(ndsx
	src/kernel/main.cpp
	src/kernel/process_manager.cpp
	src/kernel/syscalls/fd.cpp
	src/kernel/syscalls/process.cpp
	src/kernel/syscalls/cothread.cpp
	src/kernel/syscalls/misc.cpp
	src/kernel/std_streams.c
)
target_include_directories(ndsx PRIVATE include)
target_link_libraries(ndsx PRIVATE nds9-prefixed)
foreach(SYMBOL IN LISTS SYMBOLS)
	target_link_options(ndsx PRIVATE -Wl,--undefined=${SYMBOL})
endforeach()
nds_create_rom(ndsx)

function(create_ndsx_program PROGRAM SOURCES)
	add_library(${PROGRAM} STATIC ${SOURCES})
	blocksds_create_dsl(${PROGRAM} MAIN_TARGET ndsx)
	if(NOT "$ENV{CI}")
		make_directory("${CMAKE_SOURCE_DIR}/sdcard")
		add_custom_command(
			OUTPUT "${CMAKE_SOURCE_DIR}/sdcard/${PROGRAM}.dsl"
			COMMAND ${CMAKE_COMMAND} -E create_symlink
				"${${PROGRAM}_DSL}" # blocksds_create_dsl() returns ABSOLUTE paths to the generated DSLs!
				"${CMAKE_SOURCE_DIR}/sdcard/${PROGRAM}.dsl"
			MAIN_DEPENDENCY "${${PROGRAM}_DSL}"
		)
		add_custom_target(link_${PROGRAM}_into_sdcard ALL DEPENDS "${CMAKE_SOURCE_DIR}/sdcard/${PROGRAM}.dsl")
	endif()
endfunction()

create_ndsx_program(init_process src/programs/init.cpp)
create_ndsx_program(my_prog src/programs/my_prog.cpp)
create_ndsx_program(env-test src/programs/env-test.cpp)
create_ndsx_program(argv-test src/programs/argv-test.cpp)
create_ndsx_program(jailbreak-test src/programs/jailbreak-test.cpp)

# 1. Define an IMPORTED static library target
add_library(dash_lib STATIC IMPORTED GLOBAL)

# 2. Point CMake to the actual location of the pre-built static file
set_target_properties(dash_lib PROPERTIES
    IMPORTED_LOCATION "${CMAKE_CURRENT_SOURCE_DIR}/libdash.a"
)

blocksds_create_dsl(dash_lib MAIN_TARGET ndsx IGNORE_UNRESOLVED_SYMBOLS)
