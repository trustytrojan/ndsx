#include "process_manager.hpp"

#include <signal.h>

static constexpr bool is_unblockable_signal(const int sig)
{
	return sig == SIGKILL || sig == SIGSTOP;
}

static constexpr bool is_default_ignored_signal(const int sig)
{
	return sig == SIGCHLD;
}

static void sanitize_signal_mask(sigset_t &mask)
{
	// POSIX requires SIGKILL and SIGSTOP to be unblocked silently.
	sigdelset(&mask, SIGKILL);
	sigdelset(&mask, SIGSTOP);
}

static bool signal_is_blocked(const Process &process, const int sig)
{
	return sigismember(&process.signal_mask, sig) == 1;
}

static struct sigaction default_sigaction()
{
	struct sigaction action{};
	action.sa_handler = SIG_DFL;
	sigemptyset(&action.sa_mask);
	action.sa_flags = 0;
	return action;
}

static struct sigaction get_installed_sigaction(Process &process, const int sig)
{
	auto action = process.signal_actions[sig];
	if (action.sa_handler == nullptr)
		action = default_sigaction();
	return action;
}

static void set_default_sigaction(Process &process, const int sig)
{
	process.signal_actions[sig] = default_sigaction();
}

enum class SignalDeliveryResult
{
	None,
	Caught,
	Terminated,
};

static SignalDeliveryResult deliver_signal(Process &process, const int sig)
{
	sigdelset(&process.pending_signals, sig);
	const auto action = get_installed_sigaction(process, sig);

	if (action.sa_handler == SIG_IGN)
		return SignalDeliveryResult::None;

	if (action.sa_handler == SIG_DFL)
	{
		if (is_default_ignored_signal(sig))
			return SignalDeliveryResult::None;

		process.exit_code = 128 + sig;
		process.status = sig & 0x7f;

		for (const auto thread : process.threads)
		{
			if (thread == cothread_get_current())
				continue;
			cothread_delete(thread);
		}

		return SignalDeliveryResult::Terminated;
	}

	sigset_t old_mask = process.signal_mask;
	sigset_t handler_mask = old_mask;

	for (int blocked_sig = 1; blocked_sig < NSIG; ++blocked_sig)
		if (sigismember(&action.sa_mask, blocked_sig) == 1)
			sigaddset(&handler_mask, blocked_sig);

	if ((action.sa_flags & SA_NODEFER) == 0)
		sigaddset(&handler_mask, sig);

	sanitize_signal_mask(handler_mask);
	process.signal_mask = handler_mask;

	if (action.sa_flags & SA_RESETHAND)
		set_default_sigaction(process, sig);

	if (action.sa_flags & SA_SIGINFO)
		action.sa_sigaction(sig, nullptr, nullptr);
	else
		action.sa_handler(sig);

	process.signal_mask = old_mask;
	return SignalDeliveryResult::Caught;
}

bool deliver_pending_signals(Process &process, bool *caught_signal)
{
	if (caught_signal)
		*caught_signal = false;

	for (int sig = 1; sig < NSIG; ++sig)
	{
		if (sigismember(&process.pending_signals, sig) != 1)
			continue;
		if (signal_is_blocked(process, sig))
			continue;

		switch (deliver_signal(process, sig))
		{
		case SignalDeliveryResult::None:
			continue;
		case SignalDeliveryResult::Caught:
			if (caught_signal)
				*caught_signal = true;
			return true;
		case SignalDeliveryResult::Terminated:
			return true;
		}
	}

	return false;
}

extern "C"
{
int sigaction(int sig, const struct sigaction *act, struct sigaction *oact)
{
	if (!is_valid_signal_number(sig))
	{
		errno = EINVAL;
		return -1;
	}

	auto &process = get_current_process();
	if (oact)
		*oact = get_installed_sigaction(process, sig);

	if (!act)
		return 0;

	if (is_unblockable_signal(sig))
	{
		errno = EINVAL;
		return -1;
	}

	process.signal_actions[sig] = *act;
	sanitize_signal_mask(process.signal_actions[sig].sa_mask);
	return 0;
}

int sigsuspend(const sigset_t *mask)
{
	auto &process = get_current_process();
	const auto old_mask = process.signal_mask;

	process.signal_mask = *mask;
	sanitize_signal_mask(process.signal_mask);

	for (;;)
	{
		bool caught = false;
		if (deliver_pending_signals(process, &caught))
		{
			process.signal_mask = old_mask;
			if (caught)
			{
				errno = EINTR;
				return -1;
			}
		}

		cothread_yield();
	}
}
}
