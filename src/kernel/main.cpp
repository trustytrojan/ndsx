#include <dlfcn.h>
#include <fat.h>
#include <nds.h>

#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdio>

#include "process_manager.hpp"

bool my_sym_resolver(const char *const name, uint32_t *const value, const uint32_t attributes)
{
	if (!(attributes & DSL_SYMBOL_UNRESOLVED))
		return true;

	printf("sym_resolver: failed to resolve '%s'\n", name);
	return false;
}

int start_init()
{
	pid_t pid;
	if (posix_spawn(&pid, "init_process.dsl", {}, {}, {}, {}) == -1)
	{
		puts("failed to spawn init! crashing");
		return -67;
	}

	int status;
	const auto rc = waitpid(pid, &status, 0);
	if (rc == -1)
	{
		perror("waitpid");
		return -67;
	}

	if (rc != pid)
	{
		printf("start_init: waitpid returned wrong child: %d\n", rc);
		return -67;
	}

	if (!WIFEXITED(status))
	{
		puts("start_init: init process did not exit normally");
		return -67;
	}

	return WEXITSTATUS(status);
}

void init_console()
{
	// Video initialization - We want to use both screens
	videoSetMode(MODE_0_2D);
	videoSetModeSub(MODE_0_2D);
	vramSetBankA(VRAM_A_MAIN_BG);
	vramSetBankC(VRAM_C_SUB_BG);

	constexpr auto layer{0};
	constexpr auto type{BgType_Text4bpp};
	constexpr auto size{BgSize_T_256x256};
	constexpr auto mapBase{20}; // 22 is recommended, but as found below, 20 is fine
	constexpr auto tileBase{3};

	static PrintConsole console;
	consoleInit(&console, layer, type, size, mapBase, tileBase, true, true);

	keyboardDemoInit()->scrollSpeed = 0;
	keyboardShow();
}

int main()
{
	defaultExceptionHandler();
	init_console();

	dsl_set_symbol_resolver(my_sym_resolver);
	set_kernel_process(); // initializes process 0 with main() thread

	printf("ndsx 0.0.1\n\n");

	if (!fatInitDefault())
	{
		perror("fatInitDefault");
		goto end;
	}

	{
		int rc = start_init();
		printf("init exited with %d\n", rc);
		puts("now looping");
	}

end:
	while (true)
		cothread_yield_irq(IRQ_VBLANK);
}
