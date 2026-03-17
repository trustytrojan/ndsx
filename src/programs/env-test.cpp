#include <cstdio>
#include <cstdlib>

int main()
{
	printf("env-test: X=%s Y=%s Z=%s\n", getenv("X"), getenv("Y"), getenv("Z"));
}
