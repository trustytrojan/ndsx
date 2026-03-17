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
)
nds_create_rom(ndsx)

add_library(init_process STATIC
	src/programs/init.cpp
)
blocksds_create_dsl(
	init_process
	MAIN_TARGET ndsx
)

add_library(my_prog STATIC
	src/programs/my_prog.cpp
)
blocksds_create_dsl(
	my_prog
	MAIN_TARGET ndsx
)

foreach(_dsl IN ITEMS init_process my_prog)
	execute_process(
		COMMAND ${CMAKE_COMMAND} -E create_symlink
			"${${_dsl}_DSL}" # blocksds_create_dsl() returns ABSOLUTE paths to the generated DSLs!
			"${CMAKE_SOURCE_DIR}/sdcard/${_dsl}.dsl"
		COMMAND_ERROR_IS_FATAL ANY
	)
endforeach()
