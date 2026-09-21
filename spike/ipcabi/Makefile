# Cross-ABI IPC wire-format proof. Builds the same source four ways: {i386, x86_64} x {patched,
# unpatched}. "Patched" includes wtf/ArgumentCoder.h from the tiger-ipc-abi worktree and uses its
# wireAlignmentOf; "unpatched" uses alignof(T), which is what that header said before the fix.
WKT  := /Users/shg/Developer/WebKitTiger
SRC  := $(WKT)/WebKit-ipc
CC32 := $(WKT)/toolchain/bin/tiger-clang++
CC64 := $(WKT)/toolchain/bin/tiger-clang64++
OVL  := -isystem $(WKT)/compat/sdk-overlay/usr/include -isystem $(WKT)/compat/include
INC  := -I $(SRC)/Source/WTF -I $(SRC)/Source/bmalloc
STD32 := -nostdinc++ -isystem $(WKT)/toolchain/sysroot-i386/usr/include/c++/v1
STD64 := -nostdinc++ -isystem $(WKT)/toolchain/sysroot-x86_64/usr/include/c++/v1
FLAGS := -std=c++2b -O1 -g -Wall

all: abitest32 abitest64 abitest32-unpatched abitest64-unpatched spancheck32 spancheck64

abitest32: abitest.cpp abiwire.h
	$(CC32) $(FLAGS) $(STD32) $(OVL) $(INC) -DIPCABI_PATCHED=1 abitest.cpp -o $@
abitest64: abitest.cpp abiwire.h
	$(CC64) $(FLAGS) $(STD64) $(OVL) $(INC) -DIPCABI_PATCHED=1 abitest.cpp -o $@
abitest32-unpatched: abitest.cpp abiwire.h
	$(CC32) $(FLAGS) $(STD32) $(OVL) $(INC) -DIPCABI_PATCHED=0 abitest.cpp -o $@
abitest64-unpatched: abitest.cpp abiwire.h
	$(CC64) $(FLAGS) $(STD64) $(OVL) $(INC) -DIPCABI_PATCHED=0 abitest.cpp -o $@

spancheck32: spancheck.cpp
	$(CC32) $(FLAGS) $(STD32) $(OVL) $(INC) -DIPCABI_PATCHED=1 spancheck.cpp -o $@
spancheck64: spancheck.cpp
	$(CC64) $(FLAGS) $(STD64) $(OVL) $(INC) -DIPCABI_PATCHED=1 spancheck.cpp -o $@

# The rejection case: this target must FAIL to compile for x86_64. i386 cannot see the hazard.
spanreject:
	@echo "--- i386 (cannot detect it: alignof is 4 either way) ---"
	@$(CC32) $(FLAGS) $(STD32) $(OVL) $(INC) -DIPCABI_PATCHED=1 -DSPANCHECK_EXPECT_REJECT=1 \
	  -fsyntax-only spancheck.cpp 2>&1 | grep -c "static assertion failed" || true
	@echo "--- x86_64 (must report 2 rejections) ---"
	@$(CC64) $(FLAGS) $(STD64) $(OVL) $(INC) -DIPCABI_PATCHED=1 -DSPANCHECK_EXPECT_REJECT=1 \
	  -fsyntax-only spancheck.cpp 2>&1 | grep -c "static assertion failed" || true

run: all
	ssh tiger mkdir -p /tmp/ipcabi
	scp -qO abitest32 abitest64 abitest32-unpatched abitest64-unpatched tiger:/tmp/ipcabi/
	@echo "=== unpatched ==="
	ssh tiger '/tmp/ipcabi/abitest32-unpatched /tmp/ipcabi/abitest64-unpatched'
	ssh tiger '/tmp/ipcabi/abitest64-unpatched /tmp/ipcabi/abitest32-unpatched'
	@echo "=== patched ==="
	ssh tiger '/tmp/ipcabi/abitest32 /tmp/ipcabi/abitest64'
	ssh tiger '/tmp/ipcabi/abitest64 /tmp/ipcabi/abitest32'
	@echo "=== span element sizes ==="
	scp -qO spancheck32 spancheck64 tiger:/tmp/ipcabi/
	ssh tiger '/tmp/ipcabi/spancheck32; /tmp/ipcabi/spancheck64'
	$(MAKE) spanreject

clean:
	rm -f abitest32 abitest64 abitest32-unpatched abitest64-unpatched spancheck32 spancheck64
	rm -rf *.dSYM
.PHONY: all run clean spanreject
