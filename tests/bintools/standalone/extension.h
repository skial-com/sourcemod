// Stand-in for bintools' extension.h: only the page-memory API the JIT uses.
#pragma once
#include <sm_platform.h>
#include <IBinTools.h>
#include <sourcehook.h>
#include <cstddef>
using namespace SourceMod;

struct FakeSPEngine
{
	void *AllocatePageMemory(size_t size);
	void SetReadWrite(void *) {}
	void SetReadExecute(void *) {}
	void FreePageMemory(void *) {}
};
extern FakeSPEngine *g_SPEngine;
