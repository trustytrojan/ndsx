cmake_minimum_required(VERSION 3.21)
project(ndsx LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# prefix the libnds syscall symbols
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

add_executable(ndsx
	src/kernel/main.cpp
	src/kernel/process_manager.cpp
	src/kernel/syscalls/fd.cpp
	src/kernel/syscalls/process.cpp
	src/kernel/syscalls/cothread.cpp
	src/kernel/std_streams.c
)
target_include_directories(ndsx PRIVATE include)
target_link_libraries(ndsx PRIVATE nds9-prefixed)
target_link_options(ndsx PRIVATE
	-Wl,--undefined=fwrite
	-Wl,--undefined=fputs
	-Wl,--undefined=getppid
	-Wl,--undefined=wait
	-Wl,--undefined=gettimeofday
	-Wl,--undefined=getenv
	-Wl,--undefined=setenv
	-Wl,--undefined=unsetenv
)
nds_create_rom(ndsx)

set(NDSX_PROGRAMS)
function(create_ndsx_program PROGRAM SOURCES)
	add_library(${PROGRAM} STATIC ${SOURCES})
	blocksds_create_dsl(${PROGRAM} MAIN_TARGET ndsx)
	list(APPEND NDSX_PROGRAMS ${PROGRAM})
	execute_process(
		COMMAND ${CMAKE_COMMAND} -E create_symlink
			"${${PROGRAM}_DSL}" # blocksds_create_dsl() returns ABSOLUTE paths to the generated DSLs!
			"${CMAKE_SOURCE_DIR}/sdcard/${PROGRAM}.dsl"
		COMMAND_ERROR_IS_FATAL ANY
	)
endfunction()

create_ndsx_program(init_process src/programs/init.cpp)
create_ndsx_program(my_prog src/programs/my_prog.cpp)
create_ndsx_program(env-test src/programs/env-test.cpp)
create_ndsx_program(argv-test src/programs/argv-test.cpp)
create_ndsx_program(jailbreak-test src/programs/jailbreak-test.cpp)
