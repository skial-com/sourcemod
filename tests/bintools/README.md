# bintools x86_64 test suite

Tests the x86_64 SysV call JIT in `extensions/bintools` (Linux x64 only).

- **Direct suite** (`direct.cpp`, 104 cases): each case calls a g++-built callee
  (`callees.cpp`) directly and through an IBinTools wrapper, then compares the
  arguments the callee logged and the returned value. It covers every
  parameter/return class, register spills, MEMORY and non-trivial objects,
  thiscall/vtable calls and varargs. Each call also checks stack alignment,
  callee-saved registers, and buffer over-reads/over-writes (guard pages).
- **Plugin** (`bintools_test.sp` + `bttest.ext.so`, 212 checks): every SDKCall
  type, pass method and call type, plus the shipped SDKTools/SDKHooks/TF2 natives
  that call through bintools, verified by their effect in game.

```
./build.sh standalone [git-ref]   # run the direct suite offline against bintools at <ref>
./build.sh install                # build bttest.ext.so + bintools_test.smx, install the extension
sm_bttest [all|direct|sdkcall|natives] [verbose]   # in game; last line is RESULT: ALL PASSED / FAILURES
```

The plugin spawns a fake client for the player natives if no one is playing.
