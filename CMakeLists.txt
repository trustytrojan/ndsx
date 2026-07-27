cmake_minimum_required(VERSION 3.21)
project(ndsx LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
add_compile_definitions(MSPACES=1)

# prefix the libnds syscall symbols
# TODO: make this a custom command/target so that it depends on libnds_sym_renames.txt
set(OBJCOPY_COMMAND "$ENV{WONDERFUL_TOOLCHAIN}/toolchain/gcc-arm-none-eabi/bin/arm-none-eabi-objcopy")

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
	set(LIBNDS9_FILENAME "libnds9d.a")
else()
	set(LIBNDS9_FILENAME "libnds9.a")
endif()
message("LIBNDS9_FILENAME: '${LIBNDS9_FILENAME}'")

execute_process(
	COMMAND "${OBJCOPY_COMMAND}"
		"--redefine-syms=${CMAKE_SOURCE_DIR}/libnds_sym_renames.txt"
		"$ENV{BLOCKSDS}/libs/libnds/lib/${LIBNDS9_FILENAME}"
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
	__aeabi_d2iz __aeabi_ddiv __aeabi_dmul _ctype_b atoi bsearch closedir dup dup2 execve fcntl fork fstat getcwd getegid geteuid getgid getgroups getpgrp getuid isatty iswblank iswctype kill killpg lstat mbrlen mbrtowc mbsrtowcs mempcpy memrchr opendir pipe putenv qsort readdir realloc setlocale setpgid sigaction signal sigprocmask sigsuspend stat stpncpy strcasecmp strchrnul strcoll strcspn strpbrk strsignal strspn strtod strtoimax strtok strtoumax sysconf tcgetattr tcgetpgrp tcsetpgrp times umask vfork vsnprintf wcschr wctype
)

file(GLOB_RECURSE NDSX_SOURCES src/kernel/*.cpp)
add_executable(ndsx ${NDSX_SOURCES})
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
create_ndsx_program(arrow-keys src/programs/arrow-keys.cpp)

# 1. Define an IMPORTED static library target
add_library(dash STATIC IMPORTED GLOBAL)

# 2. Point CMake to the actual location of the pre-built static file
set_target_properties(dash PROPERTIES
    IMPORTED_LOCATION "${CMAKE_SOURCE_DIR}/../nds-dash/libdash.a"
)

blocksds_create_dsl(dash MAIN_TARGET ndsx IGNORE_UNRESOLVED_SYMBOLS)
add_custom_command(
	OUTPUT "${CMAKE_SOURCE_DIR}/sdcard/dash.dsl"
	COMMAND ${CMAKE_COMMAND} -E create_symlink
		"${CMAKE_BINARY_DIR}/dash.dsl"
		"${CMAKE_SOURCE_DIR}/sdcard/dash.dsl"
)
add_custom_target(link_dash_into_sdcard ALL
	DEPENDS "${CMAKE_SOURCE_DIR}/sdcard/dash.dsl")
