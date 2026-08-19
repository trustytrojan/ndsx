#include <process_manager.hpp>

extern "C" void _exit(int status)
{
	Process &current = get_current_process();
	printf("_exit: called by pid %d\n", current.pid);

	// Check if we are a vfork child running on a borrowed thread
	auto parent = get_process(current.ppid);
	if (!parent)
	{
		// The only case in which this can happen is the kernel process itself calling _exit()...
		puts("_exit: called by pid 0\npress START to exit");
		while (true)
		{
			scanKeys();
			if (keysDown() & KEY_START)
			{
				typeof(_exit) libnds__exit;
				libnds__exit(status);
			}
		}
	}

	if (parent->is_vfork_suspended)
	{
		current.exit_code = status;
		current.status = (current.exit_code << 8) | 0x00;

		{ // This NEEDS to be here, otherwise process bookkeeping breaks.
			nds_critical_section cs;
			transfer_current_thread(current, *parent);
			// transfer_current_thread asserts that the current process is now *parent.
		}

		puts("_exit: parent->is_vfork_suspended about to longjmp");

		// Longjmp back to vfork() as the parent process.
		longjmp(parent->vfork_env, current.pid);
	}
	else
	{
		puts("_exit: about to longjmp");

		// Longjmp back to process_start_trampoline, simulating a return from main().
		longjmp(current.exit_env, status);
	}

	// Execution should never reach here. If it does, you really screwed up the memory.
	puts("_exit is returning");
	libndsCrash("_exit is returning");
}
