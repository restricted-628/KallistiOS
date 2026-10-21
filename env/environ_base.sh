# KallistiOS environment variable settings. These are the shared pieces
# that are generated from the user config. Configure if you like.

export KOS_VERSION_MAJOR=`awk '$2 == "KOS_VERSION_MAJOR"{print $3; exit}' ${KOS_BASE}/include/kos/version.h`
export KOS_VERSION_MINOR=`awk '$2 == "KOS_VERSION_MINOR"{print $3; exit}' ${KOS_BASE}/include/kos/version.h`
export KOS_VERSION_PATCH=`awk '$2 == "KOS_VERSION_PATCH"{print $3; exit}' ${KOS_BASE}/include/kos/version.h`
export KOS_VERSION="${KOS_VERSION_MAJOR}.${KOS_VERSION_MINOR}.${KOS_VERSION_PATCH}"

# Default the kos-ports path if it isn't already set.
if [ -z "${KOS_PORTS}" ] ; then
  export KOS_PORTS="${KOS_BASE}/../kos-ports"
fi

# Arch kernel folder.
export KOS_ARCH_DIR="${KOS_BASE}/kernel/arch/${KOS_ARCH}"

# Add the compiler bins dir to the path if it is not already.
if ! expr ":$PATH:" : ".*:${KOS_CC_BASE}/bin:.*" > /dev/null ; then
  export PATH="${PATH}:${KOS_CC_BASE}/bin"
fi

# Add the build wrappers dir to the path if it is not already.
if ! expr ":$PATH:" : ".*:${KOS_BASE}/utils/build_wrappers:.*" > /dev/null ; then
  export PATH="${PATH}:${KOS_BASE}/utils/build_wrappers"
fi

# Our includes.
export KOS_INC_PATHS="${KOS_INC_PATHS} -isystem ${KOS_BASE}/include \
-isystem ${KOS_BASE}/kernel/arch/${KOS_ARCH}/include -isystem ${KOS_BASE}/addons/include/ \
-isystem ${KOS_PORTS}/include"

# Preserve existing GCC configurations which predate toolchain selection.
export KOS_TOOLCHAIN="${KOS_TOOLCHAIN:-gcc}"
if [ ! -d "${KOS_BASE}/env/${KOS_TOOLCHAIN}" ]; then
  echo "ERROR: Unknown toolchain $KOS_TOOLCHAIN" >&2
  return 1
fi

# Tool paths must exist before the architecture probes its compiler. Final
# flags must wait for the architecture's subarch, ABI and linker-script setup.
. "${KOS_BASE}/env/${KOS_TOOLCHAIN}/environ_toolchain.sh" || return $?
. "${KOS_BASE}/env/${KOS_TOOLCHAIN}/environ_${KOS_ARCH}.sh" || return $?

export KOS_CFLAGS="${KOS_CFLAGS} ${KOS_INC_PATHS} -D_arch_${KOS_ARCH}=1 -D_arch_sub_${KOS_SUBARCH}=1 -Wall -g"
export KOS_CPPFLAGS="${KOS_CPPFLAGS} ${KOS_INC_PATHS_CPP}"
export KOS_LDFLAGS="${KOS_CFLAGS} ${KOS_LDFLAGS} ${KOS_LD_SCRIPT} -nostdlib ${KOS_LIB_PATHS}"
