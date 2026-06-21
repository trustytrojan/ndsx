#include <process_manager.hpp>

extern "C" void _exit(int status)
{
	Process &current = get_current_process();

	// Check if we are a vfork child running on a borrowed thread
	auto parent = get_process(current.ppid);
	if (!parent)
	{
		// The only case in which this can happen is the kernel process itself calling _exit()...
		puts("_exit: current process has no parent");
		libndsCrash("current process has no parent");
	}

	if (parent->is_vfork_suspended)
	{
		current.exit_code = status;
		current.status = (current.exit_code << 8) | 0x00;

		{
			nds_critical_section cs;
			transfer_current_thread(current, *parent);
		}

		if (get_current_process() != *parent)
		{
			puts("_exit: get_current_process() != *parent");
			libndsCrash("_exit: get_current_process() != *parent");
		}

		puts("_exit: parent->is_vfork_suspended about to longjmp");

		// 4. Time travel! Warp the CPU back into the parent's vfork() call.
		// We pass current.pid, which causes setjmp in vfork() to return the child's PID.
		longjmp(parent->vfork_env, current.pid);
	}
	else
	{
		// --- STANDARD EXIT ---
		// Normal cleanup, cothread_delete(), etc.
		// current.cleanup();

		puts("_exit: about to longjmp");

		longjmp(current.exit_env, status);
	}

	printf("_exit is returning");
	libndsCrash("_exit is returning");
}
