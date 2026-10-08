// Runs the direct suite outside the game against bintools sources from any
// commit (see build.sh standalone). Exit status is the failure count (capped).
#include "extension.h"
#include "CallMaker.h"
#include "direct.h"
#include <sys/mman.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>

CallMaker g_CallMaker;
CallMaker2 g_CallMaker2;

// JIT code pages also end at a guard page
void *FakeSPEngine::AllocatePageMemory(size_t size)
{
	size_t pages = (size + 4095) / 4096 + 1;
	char *base = (char *)mmap(nullptr, pages * 4096, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	mprotect(base + (pages - 1) * 4096, 4096, PROT_NONE);
	return base + (pages - 1) * 4096 - size;
}
static FakeSPEngine s_engine;
FakeSPEngine *g_SPEngine = &s_engine;

void bt_print(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	putchar('\n');
}

int main(int argc, char **argv)
{
	setvbuf(stdout, nullptr, _IONBF, 0);
	bool verbose = argc > 1 && !strcmp(argv[1], "-v");
	g_pBinTools = &g_CallMaker;
	int passed;
	int failed = BtRunDirect(verbose, &passed);
	printf("[bttest] direct: %d passed, %d failed\n", passed, failed);
	return failed > 100 ? 100 : failed;
}
