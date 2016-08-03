GENCCODE_ASSEMBLY_TYPE=
SO=so
SOBJ=so
A=a
LIBPREFIX=lib
LIB_EXT_ORDER=.56.1
COMPILE=arm-none-eabi-gcc -I/home/jenkins/workspace/toolchain-3ds/3ds/include -g0 -O2 -mword-relocations -fomit-frame-pointer -ffast-math -march=armv6k -mtune=mpcore -mfloat-abi=hard -D_REENTRANT  -DU_ENABLE_DYLOAD=0 -DU_HAVE_ATOMIC=1 -DU_HAVE_MMAP=0 -DU_HAVE_POPEN=0  -DU_ATTRIBUTE_DEPRECATED= -I/home/jenkins/workspace/toolchain-3ds/3ds/include -g0 -O2 -mword-relocations -fomit-frame-pointer -ffast-math -march=armv6k -mtune=mpcore -mfloat-abi=hard   -c
LIBFLAGS=-I/home/jenkins/workspace/toolchain-3ds/3ds/include -DPIC -fPIC
GENLIB=arm-none-eabi-gcc -I/home/jenkins/workspace/toolchain-3ds/3ds/include -g0 -O2 -mword-relocations -fomit-frame-pointer -ffast-math -march=armv6k -mtune=mpcore -mfloat-abi=hard   -L/home/jenkins/workspace/toolchain-3ds/3ds/lib  -shared -Wl,-Bsymbolic
LDICUDTFLAGS=-nodefaultlibs -nostdlib
LD_SONAME=-Wl,-soname -Wl,
RPATH_FLAGS=
BIR_LDFLAGS=-Wl,-Bsymbolic
AR=arm-none-eabi-ar
ARFLAGS=r
RANLIB=arm-none-eabi-ranlib
INSTALL_CMD=/usr/bin/install -c
