#include <cstdio>
#include <stdlib.h>

extern char **environ;

int main(int, char **, char **envp)
{
	puts("env-test:");

	// They should be the same, since we haven't added a new entry to environ yet.
	printf(" envp == environ: %d\n", envp == environ);

	puts(" initial environ:");
	for (auto s{environ}; *s; ++s)
		printf("  '%s'\n", *s);

	// Overwrite Y with "67".
	setenv("Y", "67", 1);

	// Delete Z from the environment.
	unsetenv("Z");

	// Add a new entry.
	setenv("W", "42", 0);

	// Call setenv with an existing key, but with overwrite=0.
	setenv("Y", "42", 0);

	// Since setenv("W", "42", 0) causes a realloc() on environ, this should print 0!
	printf(" envp == environ: %d\n", envp == environ);

	// Given that the initial environment contained some value for Y and Z,
	// the final environment should contain Y=67 W=42, and no entry for Z.
	// Any other entries should be unmodified.
	puts(" final environ:");
	for (auto s{environ}; *s; ++s)
		printf("  '%s'\n", *s);
}
