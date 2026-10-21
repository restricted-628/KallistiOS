# "System" libraries.
export KOS_LIB_PATHS="-L${KOS_BASE}/lib/${KOS_ARCH} -L${KOS_BASE}/addons/lib/${KOS_ARCH} -L${KOS_PORTS}/lib"
export KOS_LIBS="-Wl,--start-group -lkallisti -lsh4zam -lm -lc -lgcc -Wl,--end-group"

# Main arch compiler paths.
export KOS_CC="${KOS_CC_BASE}/bin/${KOS_CC_PREFIX}-gcc"
export KOS_CCPLUS="${KOS_CC_BASE}/bin/${KOS_CC_PREFIX}-g++"
export KOS_AS="${KOS_CC_BASE}/bin/${KOS_CC_PREFIX}-as"
export KOS_AR="${KOS_CC_BASE}/bin/${KOS_CC_PREFIX}-gcc-ar"
export KOS_OBJCOPY="${KOS_CC_BASE}/bin/${KOS_CC_PREFIX}-objcopy"
export KOS_OBJDUMP="${KOS_CC_BASE}/bin/${KOS_CC_PREFIX}-objdump"
export KOS_ADDR2LINE="${KOS_CC_BASE}/bin/${KOS_CC_PREFIX}-addr2line"
export KOS_GCOV="${KOS_CC_BASE}/bin/${KOS_CC_PREFIX}-gcov"
export KOS_GPROF="${KOS_CC_BASE}/bin/${KOS_CC_PREFIX}-gprof"
export KOS_LD="${KOS_CC_BASE}/bin/${KOS_CC_PREFIX}-ld"
export KOS_RANLIB="${KOS_CC_BASE}/bin/${KOS_CC_PREFIX}-gcc-ranlib"
export KOS_STRIP="${KOS_CC_BASE}/bin/${KOS_CC_PREFIX}-strip"
export KOS_SIZE="${KOS_CC_BASE}/bin/${KOS_CC_PREFIX}-size"

# Which standards modes we want to compile for.
# Note that this only covers KOS itself, not necessarily anything else compiled
# with kos-cc or kos-c++.
export KOS_CSTD="-std=gnu17"
export KOS_CPPSTD="-std=gnu++17"

export KOS_GCCVER="`kos-cc -dumpversion`"
