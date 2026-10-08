#!/bin/bash
# Build bttest.ext.so (x86_64 only) and bintools_test.smx.
#   ./build.sh                     build into ./out
#   ./build.sh install             also copy to the tftest1 tree (~/tf/tf/addons/sourcemod)
#   ./build.sh standalone [ref]    build + run the direct suite outside the game against
#                                  bintools from sourcemod git <ref> (default: working tree)
set -euo pipefail
cd "$(dirname "$0")"
# Env overrides: SM (this repo), MMS (metamod source), SMBUILD (build dir with spcomp64), TARGET (server tree)
SM=${SM:-$(cd ../.. && pwd)}
MMS=${MMS:-$SM/../mmsource}
SMBUILD=${SMBUILD:-$SM/../sourcemod-build}
TARGET=${TARGET:-$HOME/tf/tf/addons/sourcemod}
mkdir -p out

# Prototypes for the driver, generated from the callee definitions
python3 - <<'PY'
import re
src = open('callees.cpp').read()
out = ['// Generated from callees.cpp by build.sh; do not edit.', '#pragma once', '#include "bttest.h"', 'extern "C" {']
for m in re.finditer(r'^NI ([^{]*?\))\s*\n?\s*\{', src, re.M | re.S):
    out.append(' '.join(m.group(1).split()) + ';')
for t in re.findall(r'OBJ1\((\w+)\)', src.split('#define OBJ1')[1]):
    out.append(f'int32_t c_obj_{t}(int32_t pre, {t} v, int32_t post);')
for t in re.findall(r'^RET1\((\w+),', src, re.M):
    out.append(f'{t} c_ret_{t}(int32_t k);')
out.append('}')
open('callees.h', 'w').write('\n'.join(out) + '\n')
PY

INC="-I. -I$SM/public -I$SM/public/extensions -I$SM/sourcepawn/include -I$SM/public/amtl -I$SM/public/amtl/amtl"
DEF="-DPOSIX -D_LINUX -DLINUX -DGNUC -DPLATFORM_X64 -DSE_TF2=1 -DSOURCE_ENGINE=SE_TF2"
CXX="g++ -m64 -fPIC -std=c++17 -g -fvisibility=hidden -fno-strict-aliasing -Wno-attributes"
# Callees: GCC like the game, frame pointer for the alignment check, no IPA tricks
$CXX -O1 -fno-omit-frame-pointer -fno-ipa-sra -fno-ipa-cp $INC -c callees.cpp -o out/callees.o
$CXX -O2 $DEF $INC -c direct.cpp -o out/direct.o

if [ "${1:-}" = standalone ]; then
  ref="${2:-}"
  src="out/bintools-${ref:-worktree}"
  rm -rf "$src"; mkdir -p "$src"
  for f in CallMaker.cpp CallMaker.h CallWrapper.cpp CallWrapper.h jit_call_x64.cpp jit_compile.h x64_macros.h; do
    if [ -n "$ref" ]; then git -C "$SM" show "$ref:extensions/bintools/$f" > "$src/$f"; else cp "$SM/extensions/bintools/$f" "$src/"; fi
  done
  # same flags bintools builds with
  BINC="-Istandalone -I$src -I$SM/public -I$SM/public/extensions -I$SM/sourcepawn/include -I$SM/public/amtl/amtl -I$SM/public/amtl -I$MMS/core/sourcehook -I$SM/public/jit -I$SM/public/jit/x86"
  BDEF="-DNDEBUG -DPOSIX -DLINUX -D_LINUX -DGNUC -D_GNU_SOURCE -DHAVE_STDINT_H -Dstricmp=strcasecmp -D_stricmp=strcasecmp -D_snprintf=snprintf -D_vsnprintf=vsnprintf"
  for f in CallMaker CallWrapper jit_call_x64; do
    clang++ -m64 -O2 -std=c++20 -fno-exceptions -Wno-everything $BDEF $BINC -c "$src/$f.cpp" -o "$src/$f.o"
  done
  clang++ -m64 -O2 -std=c++20 -fno-exceptions -Wno-everything $BDEF $BINC -I. -c standalone/main.cpp -o "$src/main.o"
  gcc -m64 -c tramp.S -o out/tramp.o
  g++ -m64 -o "$src/bttest_standalone" "$src/main.o" out/direct.o out/callees.o out/tramp.o "$src"/CallMaker.o "$src"/CallWrapper.o "$src"/jit_call_x64.o
  echo "== bintools ${ref:-working tree} =="
  exec "$src/bttest_standalone" "${@:3}"
fi

$CXX -O2 $DEF $INC -c extension.cpp -o out/extension.o
$CXX -O2 $DEF $INC -c "$SM/public/smsdk_ext.cpp" -o out/smsdk_ext.o
gcc -m64 -c tramp.S -o out/tramp.o
g++ -m64 -shared -static-libstdc++ -static-libgcc -Wl,--no-undefined -Wl,--version-script=version_script.lds \
    -o out/bttest.ext.so out/extension.o out/direct.o out/smsdk_ext.o out/callees.o out/tramp.o
echo "built out/bttest.ext.so"

"$SMBUILD/package/addons/sourcemod/scripting/spcomp64" -i"$SMBUILD/package/addons/sourcemod/scripting/include" -i. \
    bintools_test.sp -o out/bintools_test.smx
cp bttest.inc out/

if [ "${1:-}" = install ]; then
  cp out/bttest.ext.so "$TARGET/extensions/x64/bttest.ext.so"
  mkdir -p "$TARGET/scripting/include"
  cp bttest.inc "$TARGET/scripting/include/"
  echo "installed bttest.ext.so -> $TARGET/extensions/x64/ (copy out/bintools_test.smx into a plugins dir to run it)"
fi
