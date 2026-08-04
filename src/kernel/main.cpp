#include <dlfcn.h>
#include <fat.h>
#include <nds.h>

#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdio>

#include "process_manager.hpp"

int start_init()
{
	char *argv[] = {(char *)"busybox.dsl", (char *)"hush", nullptr};
	char *envp[] = {(char *)"", nullptr};

	pid_t pid;
	if (posix_spawn(&pid, "busybox.dsl", {}, {}, argv, envp) == -1)
	{
		perror("posix_spawn");
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

	const auto kb = keyboardDemoInit();
	kb->scrollSpeed = 0;

	// Echo all keypresses as if tcsetattr() was called with termios.c_lflags & ECHO
	kb->OnKeyPressed = [](const auto kc)
	{
		const auto consolePrintStr = [](const std::string_view s)
		{
			for (const auto c : s)
			{
				if (!c)
					break;
				consolePrintChar(c);
			}
		};

		switch (kc)
		{
			// clang-format off
		case DVK_FOLD:  consolePrintStr("^["); break;
		case DVK_UP:    consolePrintStr("^[[A"); break;
		case DVK_DOWN:  consolePrintStr("^[[B"); break;
		case DVK_RIGHT: consolePrintStr("^[[C"); break;
		case DVK_LEFT:  consolePrintStr("^[[D"); break;
		default: if (kc > 0) consolePrintChar(kc); break;
			// clang-format on
		}
	};

	// Emulate ANSI escape sequences for arrow/Esc keys
	kb->OnKeyPutc = [](const int kc)
	{
		const auto keyboardFifoPuts = [](const std::string_view s)
		{
			for (const auto c : s)
			{
				if (!c)
					break;
				keyboardFifoPutc(c);
			}
		};

		switch (kc)
		{
			// clang-format off
		case DVK_FOLD:  keyboardFifoPutc('\e'); break;
		case DVK_UP:    keyboardFifoPuts("\e[A"); break;
		case DVK_DOWN:  keyboardFifoPuts("\e[B"); break;
		case DVK_RIGHT: keyboardFifoPuts("\e[C"); break;
		case DVK_LEFT:  keyboardFifoPuts("\e[D"); break;
			// clang-format on
		}

		// Tell libnds to continue its normal behavior of keyboardFifoUpdate().
		// That way, we don't have to manually handle backspace or other keys.
		return true;
	};

	keyboardShow();
}

// Prevent processes from calling libnds functions that we override.
bool my_sym_resolver(const char *const name, uint32_t *const value, const uint32_t attributes)
{
	// Prevent DSLs from accessing our renamed libnds functions!
	if ((attributes & DSL_SYMBOL_MAIN_BINARY) && strstr(name, "libnds_") == name) // equivalent of `starts_with()`
	{
		fprintf(stderr, "kernel: blocked access to symbol '%s'\n", name);
		return false;
	}

	// Not unresolved, we can return true.
	if (!(attributes & DSL_SYMBOL_UNRESOLVED))
		return true;

	fprintf(stderr, "kernel: failed to resolve symbol: '%s'\n", name);

	// We aren't doing any symbol resolution yet.
	return false;
}

int main()
{
	defaultExceptionHandler();
	init_console();
	set_kernel_process();
	dsl_set_symbol_resolver(my_sym_resolver);

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
