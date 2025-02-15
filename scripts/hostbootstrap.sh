#!/bin/sh
# hostbootstrap.sh - bootstraps host tools
# Copyright 2022 - 2024 The NexNix Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# Panics with a message and then exits
panic()
{
    echo "$0: error: $1"
    exit 1
}

# Checks return value and panics if an error occured
checkerr()
{
    # Check if return value is success or not
    if [ $1 -ne 0 ]
    then
        panic "$2" $3
    fi
}

if [ "$1" != "-help" ]
then
    # Component to build
    component=$1
    if [ -z "$component" ]
    then
        panic "component not specified"
    fi
    isdash=$(echo "$1" | awk '/^-/')
    if [ ! -z "$isdash" ] && [ "$1" != "-help" ]
    then
        panic "invalid component \"$1\"" 
    fi
    shift
fi

# Argument flags
rebuild=0

while [ $# -gt 0 ]
do
    case $1 in
        -help)
            cat <<HELPEND
$(basename $0) - bootstraps host toolchain components
Usage: $0 COMPONENT [-help] [-rebuild]
  -help
                Show this help menu
  -rebuild
                Force rebuild of specified component

COMPONENT can be one of hostlibs, hosttools, toolchainlibs, 
firmware, nnpkg-host, or toolchain.
HELPEND
            exit 0
            ;;
        -rebuild)
            rebuild=1
            shift
            ;;
        *)
            panic "invalid argument $1"
            ;;
    esac
done

if [ $NNUSENINJA -eq 1 ]
then
    cmakeargs="-G Ninja"
    cmakegen=ninja
else
    cmakegen=make
    makeargs="--no-print-directory"
fi

if [ "$component" = "hostlibs" ]
then
    echo "Bootstraping host libraries..."
    # Download libchardet
    if [ ! -d $NNSOURCEROOT/external/tools/libraries/libchardet ] || [ "$rebuild" = "1" ]
    then
        rm -rf $NNSOURCEROOT/external/tools/libraries/libchardet
        git clone https://github.com/nexos-dev/libchardet.git \
                  $NNSOURCEROOT/external/tools/libraries/libchardet
        checkerr $? "unable to download libchardet"
    fi
    # Build libchardet
    if ([ ! -f $NNBUILDROOT/tools/lib/libchardet.a ] && [ ! -f $NNBUILDROOT/tools/lib64/libchardet.a ]) \
       || [ "$rebuild" = "1" ]
    then
        chardetbuild="$NNBUILDROOT/build/tools/chardet-build/$cmakegen"
        mkdir -p $chardetbuild && cd $chardetbuild
        cmake $NNSOURCEROOT/external/tools/libraries/libchardet \
              -DCMAKE_BUILD_TYPE=Debug \
              -DCMAKE_INSTALL_PREFIX="$NNBUILDROOT/tools" $cmakeargs
        checkerr $? "unable to configure libchardet"
        $cmakegen -j $NNJOBCOUNT
        checkerr $? "unable to build libchardet"
        $cmakegen install -j $NNJOBCOUNT
        checkerr $? "unable to build libchardet"
    fi
    # Build libnex
    if [ ! -f $NNBUILDROOT/tools/lib/libnex.a -a ! -f $NNBUILDROOT/tools/lib64/libnex.a ] || 
       [ "$rebuild" = "1" ]
    then
        if [ "$NNTOOLS_ENABLE_TESTS" = "1" ]
        then
            libnex_cmakeargs="$cmakeargs -DLIBNEX_ENABLE_TESTS=ON"
        else
            libnex_cmakeargs="$cmakeargs"
        fi
        libnex_builddir="$NNBUILDROOT/build/tools/libnex-build"
        mkdir -p $libnex_builddir/$cmakegen
        cd $libnex_builddir/$cmakegen
        cmake $NNSOURCEROOT/libraries/libnex \
              -DCMAKE_BUILD_TYPE=Debug \
              -DCMAKE_INSTALL_PREFIX="$NNBUILDROOT/tools" $libnex_cmakeargs
        checkerr $? "unable to configure libnex"
        $cmakegen -j $NNJOBCOUNT $makeargs
        checkerr $? "unable to build libnex"
        $cmakegen -j $NNJOBCOUNT $makeargs install
        checkerr $? "unable to install libnex"
        ctest -V
        checkerr $? "test suite failed"
    fi
    # Build libconf
    if [ ! -f $NNBUILDROOT/tools/lib/libconf.a -a ! -f $NNBUILDROOT/tools/lib64/libconf.a ] || 
       [ "$rebuild" = "1" ]
    then
        if [ "$NNTOOLS_ENABLE_TESTS" = "1" ]
        then
            libconf_cmakeargs="$cmakeargs -DLIBCONF_ENABLE_TESTS=ON"
        else
            libconf_cmakeargs="$cmakeargs"
        fi
        libconf_builddir="$NNBUILDROOT/build/tools/libconf-build"
        mkdir -p $libconf_builddir/$cmakegen
        cd $libconf_builddir/$cmakegen
        cmake $NNSOURCEROOT/libraries/libconf \
              -DCMAKE_BUILD_TYPE=Debug \
              -DCMAKE_INSTALL_PREFIX="$NNBUILDROOT/tools" \
             -DLIBCONF_LINK_DEPS=ON \
             $libconf_cmakeargs
        checkerr $? "unable to configure libconf"
        $cmakegen -j $NNJOBCOUNT $makeargs
        checkerr $? "unable to build libconf"
        $cmakegen -j $NNJOBCOUNT $makeargs install
        checkerr $? "unable to install libconf"
        # Run tests
        ctest -V
        checkerr $? "test suite failed"
    fi
elif [ "$component" = "hosttools" ]
then
    echo "Bootstrapping host tools..."
    # Build host tools
    if [ ! -f "$NNBUILDROOT/tools/bin/nnimage" ] || [ "$rebuild" = "1" ]
    then
        if [ "$NNTOOLS_ENABLE_TESTS" = "1" ]
        then
            tools_cmakeargs="$cmakeargs -DTOOLS_ENABLE_TESTS=ON"
        else
            tools_cmakeargs="$cmakeargs"
        fi
        builddir=$NNBUILDROOT/build/tools/tools-build
        mkdir -p $builddir/$cmakegen
        cd $builddir/$cmakegen
        # So libuuid is found
        export PKG_CONFIG_PATH="$NNBUILDROOT/tools/lib/pkgconfig /usr/local/lib/pkgconfig"
        cmake $NNSOURCEROOT/hosttools \
              -DCMAKE_BUILD_TYPE=Debug \
              -DCMAKE_INSTALL_PREFIX="$NNBUILDROOT/tools" $tools_cmakeargs
        checkerr $? "unable to build host tools"
        $cmakegen -j$NNJOBCOUNT $makeargs
        checkerr $? "unable to build host tools"
        $cmakegen install -j$NNJOBCOUNT $makeargs
        checkerr $? "unable to build host tools"
        # Run tests
        ctest -V
        checkerr $? "test suite failed"
    fi
elif [ "$component" = "toolchainlibs" ]
then
    echo "Building toolchain libraries..."
    if [ "$NNTOOLCHAIN" = "gnu" ]
    then
        gmpver=6.3.0
        mpfrver=4.2.1
        mpcver=1.3.1
        islver=0.24
        cloogver=0.20.0

        # Download libgmp
        if [ ! -d $NNSOURCEROOT/external/tools/libraries/gmp-${gmpver} ] || [ "$rebuild" = "1" ]
        then
            mkdir -p $NNEXTSOURCEROOT/tools/tarballs && cd $NNEXTSOURCEROOT/tools/tarballs
            wget https://ftp.gnu.org/gnu/gmp/gmp-${gmpver}.tar.xz
            checkerr $? "unable to download libgmp"
            cd ../libraries
            echo "Extracting libgmp..."
            tar xf ../tarballs/gmp-${gmpver}.tar.xz
        fi
        # Build it
        if [ ! -f $NNBUILDROOT/tools/lib/libgmp.a ] || [ "$rebuild" = "1" ]
        then
            gmproot="$NNEXTSOURCEROOT/tools/libraries/gmp-${gmpver}"
            mkdir -p $NNBUILDROOT/build/tools/libgmp-build
            cd $NNBUILDROOT/build/tools/libgmp-build
            $gmproot/configure --disable-shared --prefix=$NNBUILDROOT/tools CFLAGS="-O2 -DNDEBUG" \
                                CXXFLAGS="-O2 -DNDEBUG"
            checkerr $? "unable to configure libgmp"
            make -j$NNJOBCOUNT
            checkerr $? "unable to build libgmp"
            make install -j$NNJOBCOUNT
            checkerr $? "unable to install libgmp"
            make check -j$NNJOBCOUNT
            checkerr $? "libgmp test suite failed"
        fi

        # Download libmpfr
        if [ ! -d $NNSOURCEROOT/external/tools/libraries/mpfr-${mpfrver} ] || [ "$rebuild" = "1" ]
        then
            mkdir -p $NNEXTSOURCEROOT/tools/tarballs && cd $NNEXTSOURCEROOT/tools/tarballs
            wget https://ftp.gnu.org/gnu/mpfr/mpfr-${mpfrver}.tar.xz
            checkerr $? "unable to download libmpfr"
            cd ../libraries
            echo "Extracting libmpfr..."
            tar xf ../tarballs/mpfr-${mpfrver}.tar.xz
        fi
        # Build it
        if [ ! -f $NNBUILDROOT/tools/lib/libmpfr.a ] || [ "$rebuild" = "1" ]
        then
            mpfrroot="$NNEXTSOURCEROOT/tools/libraries/mpfr-${mpfrver}"
            mkdir -p $NNBUILDROOT/build/tools/libmpfr-build
            cd $NNBUILDROOT/build/tools/libmpfr-build
            $mpfrroot/configure --disable-shared --prefix=$NNBUILDROOT/tools \
                                --with-gmp=$NNBUILDROOT/tools CFLAGS="-O2 -DNDEBUG" \
                                CXXFLAGS="-O2 -DNDEBUG"
            checkerr $? "unable to configure libmpfr"
            make -j$NNJOBCOUNT
            checkerr $? "unable to build libmpfr"
            make install -j$NNJOBCOUNT
            checkerr $? "unable to install libmpfr"
            make check -j$NNJOBCOUNT
            checkerr $? "libmpfr test suite failed"
        fi

        # Download libmpc
        if [ ! -d $NNSOURCEROOT/external/tools/libraries/mpc-${mpcver} ] || [ "$rebuild" = "1" ]
        then
            mkdir -p $NNEXTSOURCEROOT/tools/tarballs && cd $NNEXTSOURCEROOT/tools/tarballs
            wget https://ftp.gnu.org/gnu/mpc/mpc-${mpcver}.tar.gz
            checkerr $? "unable to download libmpc"
            cd ../libraries
            echo "Extracting libmpc..."
            tar xf ../tarballs/mpc-${mpcver}.tar.gz
        fi
        # Build it
        if [ ! -f $NNBUILDROOT/tools/lib/libmpc.a ] || [ "$rebuild" = "1" ]
        then
            mpcroot="$NNEXTSOURCEROOT/tools/libraries/mpc-${mpcver}"
            mkdir -p $NNBUILDROOT/build/tools/libmpc-build
            cd $NNBUILDROOT/build/tools/libmpc-build
            $mpcroot/configure --disable-shared --prefix=$NNBUILDROOT/tools \
                                --with-gmp=$NNBUILDROOT/tools --with-mpfr=$NNBUILDROOT/tools \
                                CFLAGS="-O2 -DNDEBUG" CXXFLAGS="-O2 -DNDEBUG"
            checkerr $? "unable to configure libmpc"
            make -j$NNJOBCOUNT
            checkerr $? "unable to build libmpc"
            make install -j$NNJOBCOUNT
            checkerr $? "unable to install libmpc"
            make check -j$NNJOBCOUNT
            checkerr $? "libmpc test suite failed"
        fi
    fi
elif [ "$component" = "nasm" ] && [ "$NNCOMMONARCH" = "x86" ]
then
    echo "Building nasm..."
    # Download libgmp
    yasmver=1.3.0
    if [ ! -d $NNEXTSOURCEROOT/tools/yasm-${yasmver} ] || [ "$rebuild" = "1" ]
    then
        mkdir -p $NNEXTSOURCEROOT/tools/tarballs && cd $NNEXTSOURCEROOT/tools/tarballs
        wget http://www.tortall.net/projects/yasm/releases/yasm-${yasmver}.tar.gz
        checkerr $? "unable to download nasm"
        cd ..
        echo "Extracting nasm..."
        tar xf tarballs/yasm-${yasmver}.tar.gz
    fi
    if [ ! -f $NNBUILDROOT/tools/bin/nasm ] || [ "$rebuild" = "1" ]
    then
        cd $NNEXTSOURCEROOT/tools/yasm-${yasmver}
        yasmroot="$NNEXTSOURCEROOT/tools/yasm-${yasmver}"
        mkdir -p $NNBUILDROOT/build/tools/yasm-build
        cd $NNBUILDROOT/build/tools/yasm-build
        $yasmroot/configure --prefix=$NNBUILDROOT/tools
        checkerr $? "unable to configure nasm"
        make -j$jobcount
        checkerr $? "unable to build nasm"
        make install -j$jobcount
        checkerr $? "unable to install nasm"
    fi
elif [ "$component" = "toolchain" ]
then
    echo "Building toolchain..."
    # Translate to toolchain arch
    if [ "$NNARCH" = "armv8" ]
    then
        toolarch=aarch64
    else
        toolarch=$NNARCH
    fi
    if [ "$NNTOOLCHAIN" = "gnu" ]
    then
        gnusys=nexnix
        binutilsver=2.41
        gccver=13.1.0
        # Download & build binutils
        if [ ! -d $NNEXTSOURCEROOT/tools/binutils-${binutilsver} ]
        then
            mkdir -p $NNEXTSOURCEROOT/tools/tarballs && cd $NNEXTSOURCEROOT/tools/tarballs
            wget https://ftp.gnu.org/gnu/binutils/binutils-${binutilsver}.tar.xz
            checkerr $? "unable to download binutils"
            cd ..
            echo "Extracting binutils..."
            tar xf tarballs/binutils-${binutilsver}.tar.xz
            patch -p0 < $NNPKGROOT/binutils/patches/binutils.patch
        fi
        # Build it
        if [ ! -f $NNTOOLCHAINPATH/$toolarch-$gnusys-ld ] || [ "$rebuild" = "1" ]
        then
            binroot="$NNEXTSOURCEROOT/tools/binutils-${binutilsver}"
            rm -rf $NNBUILDROOT/build/tools/binutils-build
            mkdir -p $NNBUILDROOT/build/tools/binutils-build
            cd $NNBUILDROOT/build/tools/binutils-build
            if [ "$NNARCH" = "riscv64" ]
            then
                targetcmdline="--target $toolarch-$gnusys"
            else
                targetcmdline="--target=$toolarch-$gnusys --enable-targets=$toolarch-$gnusys,$toolarch-pe"
            fi
            $binroot/configure --prefix=$NNBUILDROOT/tools/$NNTOOLCHAIN --disable-nls \
                               --enable-shared --disable-werror --with-sysroot=$NNDESTDIR \
                                $targetcmdline \
                                CFLAGS="-O2 -DNDEBUG" \
                                CXXFLAGS="-O2 -DNDEBUG"
            checkerr $? "unable to configure binutils"
            make -j$NNJOBCOUNT
            checkerr $? "unable to build binutils"
            make install -j$NNJOBCOUNT
            checkerr $? "unable to install binutils"
        fi

        # Download & build gcc
        if [ ! -d $NNEXTSOURCEROOT/tools/gcc-${gccver} ]
        then
            mkdir -p $NNEXTSOURCEROOT/tools/tarballs && cd $NNEXTSOURCEROOT/tools/tarballs
            wget https://ftp.gnu.org/gnu/gcc/gcc-${gccver}/gcc-${gccver}.tar.xz
            checkerr $? "unable to download GCC"
            cd ..
            echo "Extracting GCC..."
            tar xf tarballs/gcc-${gccver}.tar.xz
            patch -p0 < $NNPKGROOT/gcc/patches/gcc.patch
        fi
        # Build it
        if [ ! -f $NNTOOLCHAINPATH/$toolarch-$gnusys-gcc ] || [ "$rebuild" = "1" ]
        then
            gccroot="$NNEXTSOURCEROOT/tools/gcc-${gccver}"
            rm -rf $NNBUILDROOT/build/tools/gcc-build
            mkdir -p $NNBUILDROOT/build/tools/gcc-build
            cd $NNBUILDROOT/build/tools/gcc-build
            $gccroot/configure --target=$toolarch-$gnusys \
                               --disable-nls --enable-shared \
                                --enable-languages=c,c++ \
                                --with-gmp=$NNBUILDROOT/tools --with-mpfr=$NNBUILDROOT/tools \
                                --with-mpc=$NNBUILDROOT/tools \
                                CFLAGS="-O2 -DNDEBUG" CXXFLAGS="-O2 -DNDEBUG"\
                                --prefix=$NNTOOLCHAINPATH/.. \
                                --with-sysroot=$NNDESTDIR
            checkerr $? "unable to configure GCC"
            make all-gcc -j$NNJOBCOUNT
            checkerr $? "unable to build GCC"
            make install-gcc -j$NNJOBCOUNT
            checkerr $? "unable to install GCC"
        fi
    elif [ "$NNTOOLCHAIN" = "llvm" ]
    then
        panic "LLVM support imcomplete"
    fi
elif [ "$component" = "gcclibs" ]
then
    # Translate to toolchain arch
    if [ "$NNARCH" = "armv8" ]
    then
        toolarch=aarch64
    else
        toolarch=$NNARCH
    fi
    if [ ! -f $NNBUILDROOT/tools/gnu/lib/gcc/$toolarch-nexnix/13.1.0/libgcc.a ] || [ "$rebuild" = "1" ]
    then
        gccroot="$NNEXTSOURCEROOT/tools/gcc-${gccver}"
        cd $NNBUILDROOT/build/tools/gcc-build
        make all-target-libgcc -j $NNJOBCOUNT
        checkerr $? "unable to build GCC"
        make install-target-libgcc -j $NNJOBCOUNT
        checkerr $? "unable to install GCC"
    fi
elif [ "$component" = "firmware" ]
then
    echo "Building firmware images..."
    if [ "$NNFIRMWARE" = "efi" ]
    then
        if ([ ! -d $NNEXTSOURCEROOT/tools/edk2 ] || [ "$rebuild" = "1" ]) && [ "$NNFWIMAGE" = "edk2" ]
        then
            rm -rf $NNEXTSOURCEROOT/tools/edk2 $NNEXTSOURCEROOT/tools/edk2-platforms \
                  $NNEXTSOURCEROOT/tools/edk2-non-osi
            cd $NNEXTSOURCEROOT/tools
            git clone https://github.com/tianocore/edk2.git -b edk2-stable202205
            checkerr $? "unable to download EDK2"
            cd edk2
            git submodule update --init
            checkerr $? "unable to download EDK2"
            cd ..
            git clone https://github.com/tianocore/edk2-platforms.git
            checkerr $? "unable to download EDK2"
            git clone https://github.com/tianocore/edk2-non-osi.git
            checkerr $? "unable to download EDK2"
            cd edk2-platforms
            git submodule update --init
            checkerr $? "unable to download EDK2"
        elif ([ ! -d $NNEXTSOURCEROOT/tools/u-boot ] || [ "$rebuild" = "1" ]) && [ "$NNFWIMAGE" = "uboot" ]
        then
            rm -rf $NNEXTSOURCEROOT/tools/u-boot
            cd $NNEXTSOURCEROOT/tools
            git clone https://source.denx.de/u-boot/u-boot.git -b v2024.10
            checkerr $? "unable to download U-Boot"
        fi
    fi
    if [ ! -f $NNBUILDROOT/tools/firmware/fw${NNARCH}done ] || [ "$rebuild" = "1" ] && [ "$NNBUILDFW" = "1" ]
    then
        if [ "$NNFIRMWARE" = "efi" ]
        then
            if [ "$NNFWIMAGE" = "edk2" ]
            then
                edk2root="$NNEXTSOURCEROOT/tools"
                export WORKSPACE=$edk2root/edk2
                export EDK_TOOLS_PATH="$WORKSPACE/BaseTools"
                export PACKAGES_PATH="$edk2root/edk2:$edk2root/edk2-platforms:$edk2root/edk2-non-osi"
                export GCC5_AARCH64_PREFIX=aarch64-linux-gnu-\
                export GCC5_X64_PREFIX=x86_64-linux-gnu-
                export GCC5_IA32_PREFIX=i686-linux-gnu-
                cd $edk2root
                . edk2/edksetup.sh
                make -C edk2/BaseTools -j $NNJOBCOUNT
                # Build it
                if [ "$NNARCH" = "i386" ]
                then
                    ln -sf $edk2root/edk2/Build $NNBUILDROOT/build/edk2-build
                    mkdir -p $NNBUILDROOT/tools/firmware
                    build -a IA32 -t GCC5 -p OvmfPkg/OvmfPkgIa32.dsc
                    checkerr $? "unable to build EDK2"
                    echo "Installing EDK2..."
                    cp $edk2root/edk2/Build/OvmfIa32/DEBUG_GCC5/FV/OVMF_CODE.fd \
                        $NNBUILDROOT/tools/firmware/OVMF_CODE_IA32.fd
                    cp $edk2root/edk2/Build/OvmfIa32/DEBUG_GCC5/FV/OVMF_VARS.fd \
                        $NNBUILDROOT/tools/firmware/OVMF_VARS_IA32.fd
                    touch $NNBUILDROOT/tools/firmware/fw${NNARCH}done
                elif [ "$NNARCH" = "x86_64" ]
                then
                    ln -sf $edk2root/edk2/Build $NNBUILDROOT/build/edk2-build
                    mkdir -p $NNBUILDROOT/tools/firmware
                    build -a X64 -t GCC5 -p OvmfPkg/OvmfPkgX64.dsc
                    checkerr $? "unable to build EDK2"
                    echo "Installing EDK2..."
                    cp $edk2root/edk2/Build/OvmfX64/DEBUG_GCC5/FV/OVMF_CODE.fd \
                        $NNBUILDROOT/tools/firmware/OVMF_CODE_X64.fd
                    cp $edk2root/edk2/Build/OvmfX64/DEBUG_GCC5/FV/OVMF_VARS.fd \
                        $NNBUILDROOT/tools/firmware/OVMF_VARS_X64.fd
                    touch $NNBUILDROOT/tools/firmware/fw${NNARCH}done
                elif [ "$NNARCH" = "armv8" ]
                then
                    ln -sf $edk2root/edk2/Build $NNBUILDROOT/build/edk2-build
                    mkdir -p $NNBUILDROOT/tools/firmware
                    build -a AARCH64 -t GCC5 -p ArmVirtPkg/ArmVirtQemu.dsc
                    checkerr $? "unable to build EDK2"
                    echo "Installing EDK2..."
                    cp $edk2root/edk2/Build/ArmVirtQemu-AARCH64//DEBUG_GCC5/FV/QEMU_EFI.fd \
                        $NNBUILDROOT/tools/firmware/OVMF_CODE_AA64.fd
                    truncate -s 64M $NNBUILDROOT/tools/firmware/OVMF_CODE_AA64.fd
                    cp $edk2root/edk2/Build/ArmVirtQemu-AARCH64/DEBUG_GCC5/FV/QEMU_VARS.fd \
                        $NNBUILDROOT/tools/firmware/OVMF_VARS_AA64.fd
                    truncate -s 64M $NNBUILDROOT/tools/firmware/OVMF_VARS_AA64.fd
                    touch $NNBUILDROOT/tools/firmware/fw${NNARCH}done
                fi
            elif [ "$NNFWIMAGE" = "uboot" ]
            then
                ubootdir=$NNEXTSOURCEROOT/tools/u-boot
                cd $ubootdir
                export CROSS_COMPILE=$NNARCH-linux-gnu-
                make qemu-${NNARCH}_defconfig
                make -j $NNJOBCOUNT
                checkerr $? "unable to build U-Boot"
                cp $ubootdir/u-boot $NNBUILDROOT/tools/firmware/u-boot-$NNARCH
                touch $NNBUILDROOT/tools/firmware/fw${NNARCH}done
            fi
        fi
    fi
elif [ "$component" = "nnpkg-host" ]
then
    echo "Bootstraping nnpkg..."
    if [ ! -d $NNEXTSOURCEROOT/tools/nnpkg ] || [ "$rebuild" = "1" ]
    then
        rm -rf $NNEXTSOURCEROOT/tools/nnpkg
        cd $NNEXTSOURCEROOT/tools
        git clone https://github.com/nexos-dev/nnpkg.git \
                  $NNSOURCEROOT/external/tools/nnpkg
        checkerr $? "unable to download nnpkg"
        cd nnpkg
        git submodule update --init
        checkerr $? "unable to download nnpkg"
    fi
    if [ ! -f $NNBUILDROOT/tools/bin/nnpkg ] || [ "$rebuild" = "1" ]
    then
        nnpkgroot="$NNEXTSOURCEROOT/tools/nnpkg"
        mkdir -p $NNBUILDROOT/build/tools/nnpkg-build
        cd $NNBUILDROOT/build/tools/nnpkg-build
        cmake $nnpkgroot -DCMAKE_INSTALL_PREFIX="$NNBUILDROOT/tools" -DCMAKE_BUILD_TYPE=Debug \
              -DBUILD_SHARED_LIBS=OFF -DNNPKG_ENABLE_NLS=OFF $cmakeargs
        checkerr $? "unable to build nnpkg"
        $cmakegen -j $NNJOBCOUNT
        checkerr $? "unable to build nnpkg"
        $cmakegen install -j $NNJOBCOUNT
        checkerr $? "unable to build nnpkg"
    fi
elif [ "$component" = "nexnix-sdk" ]
then
    echo "Bootstraping NexNix SDK..."
    nnsdk_builddir="$NNOBJDIR/nexnix-sdk"
    mkdir -p $nnsdk_builddir/$cmakegen
    cd $nnsdk_builddir/$cmakegen
    cmake $NNSOURCEROOT/NexnixSdk \
         -DCMAKE_INSTALL_PREFIX=$NNDESTDIR/Programs/SDKs/NexNixSdk/0.0.1 \
         $cmakeargs
    checkerr $? "unable to configure NexNix SDK"
    $cmakegen -j $NNJOBCOUNT $makeargs
    checkerr $? "unable to build NexNix SDK"
    $cmakegen -j $NNJOBCOUNT $makeargs install
    checkerr $? "unable to install NexNix SDK"
    # Add to package database
    nnpkg add $NNPKGROOT/NexNixSdk/sdkPackage.conf -c $NNCONFROOT/nnpkg.conf \
        || true
elif [ "$component" = "nnpkgdb" ]
then
    echo "Initializing package database..."
    if [ ! -f $NNDESTDIR/Programs/Nnpkg/var/nnpkgdb ]
    then
        # Initialize folders
        mkdir -p $NNDESTDIR/Programs/Nnpkg/var
        mkdir -p $NNDESTDIR/Programs/Nnpkg/etc
        mkdir -p $NNDESTDIR/Programs/Index/bin
        mkdir -p $NNDESTDIR/Programs/Index/lib
        mkdir -p $NNDESTDIR/Programs/Index/etc
        mkdir -p $NNDESTDIR/Programs/Index/share
        mkdir -p $NNDESTDIR/Programs/Index/libexec
        mkdir -p $NNDESTDIR/Programs/Index/sbin
        mkdir -p $NNDESTDIR/Programs/Index/include
        mkdir -p $NNDESTDIR/Programs/Index/var
        ln -sf $NNDESTDIR/Programs/Index $NNDESTDIR/usr
        ln -sf $NNDESTDIR/Programs/Index/bin $NNDESTDIR/bin
        ln -sf $NNDESTDIR/Programs/Index/sbin $NNDESTDIR/sbin
        ln -sf $NNDESTDIR/Programs/Index/etc $NNDESTDIR/etc
        # Create a new nnpkg configuration file
        echo "settings" > $NNCONFROOT/nnpkg.conf
        echo "{" >> $NNCONFROOT/nnpkg.conf
        echo "    packageDb: \"$NNDESTDIR/Programs/Nnpkg/var/nnpkgdb\";" \
            >> $NNCONFROOT/nnpkg.conf
        echo "    strtab: \"$NNDESTDIR/Programs/Nnpkg/var/nnstrtab\";" \
            >> $NNCONFROOT/nnpkg.conf
        echo "   indexPath: '$NNDESTDIR/Programs/Index';" >> $NNCONFROOT/nnpkg.conf
        echo "}" >> $NNCONFROOT/nnpkg.conf
        nnpkg init -c $NNCONFROOT/nnpkg.conf
    fi
elif [ "$component" = "efi-headers" ]
then
    echo "Downloading EFI headers..."
    # Check if we need to even consider building them
    if [ "$NNFIRMWARE" != "efi" ]
    then
        # Nothing we need to do
        exit 0
    fi
    # Check if they've been built already
    if [ "$NNARCH" = "i386" ]
    then
        efiarch=ia32
    elif [ "$NNARCH" = "armv8" ]
    then
        efiarch=aarch64
    else
        efiarch=$NNARCH
    fi
    if [ ! -f $NNDESTDIR/Programs/gnu-efi/lib/crt0-efi-$efiarch.o ] || [ "$rebulid" = "1" ]
    then
        mkdir -p $NNDESTDIR/Programs/Index/include/nexboot/efi/inc
        # Downlod GNU-EFI
        gnuefidir=$NNEXTSOURCEROOT/tools/gnu-efi
        rm -rf $gnuefidir
        git clone https://github.com/nexos-dev/gnu-efi.git $gnuefidir
        checkerr $? "unable to download GNU-EFI"
        cd $gnuefidir
        export DESTDIR=$NNDESTDIR
        if [ "$NNTOOLCHAIN" != "gnu" ]
        then
            panic "unable to bootstrap gnu-efi, GNU toolchain required"
        fi
        # Translate to toolchain arch
        if [ "$NNARCH" = "armv8" ]
        then
            toolarch=aarch64
        else
            toolarch=$NNARCH
        fi
        export CROSS_COMPILE=$NNBUILDROOT/tools/$NNTOOLCHAIN/bin/$toolarch-nexnix-
        export PREFIX=/Programs/gnu-efi
        export CFLAGS="-I$NNSOURCEROOT/libraries/libc/include -I$NNSOURCEROOT/libraries/libc/arch/$NNARCH/include"
        make -j$NNJOBCOUNT
        checkerr $? "unable to build GNU-EFI"
        make install -j $NNJOBCOUNT
        checkerr $? "unable to build GNU-EFI"
        nnpkg add $NNPKGROOT/gnu-efi/nnpkg-pkg.conf -c $NNCONFROOT/nnpkg.conf || true
    fi
elif [ "$component" = "nnimage-conf" ]
then
    echo "Generating nnimage configuration..."
    # Generate list file
    echo "Programs" > $NNCONFROOT/nnimage-list.lst
    # Special cases for boot partition: on no emulation disks, there is no "boot partition"
    # per se. We also add nexboot to the root directory of the disk
    # On emulated disks, /System/Core/Boot is the root of the boot partition
    if [ "$NNIMGBOOTMODE" = "bios" ] || [ "$NNIMGBOOTMODE" = "isofloppy" ]
    then
        if [ "$NNIMGBOOTEMU" = "noemu" ]
        then
            echo "System" >> $NNCONFROOT/nnimage-list.lst
            echo "nexboot" >> $NNCONFROOT/nnimage-list.lst
            echo "nexke" >> $NNCONFROOT/nnimage-list.lst
            echo "nexboot.cfg" >> $NNCONFROOT/nnimage-list.lst
            mv $NNDESTDIR/System/Core/Boot/nexboot $NNDESTDIR/nexboot
            mv $NNDESTDIR/System/Core/Boot/nexboot.cfg $NNDESTDIR/nexboot.cfg
            mv $NNDESTDIR/System/Core/Boot/nexke $NNDESTDIR/nexke
        else
            echo "/System/Core/Boot" >>  $NNCONFROOT/nnimage-list.lst
        fi
    elif [ "$NNIMGBOOTMODE" = "efi" ]
    then
        echo "/System/Core/Boot" >> $NNCONFROOT/nnimage-list.lst
        # Copy out nexboot.efi to appropiate location so EFI firmware can boot
        mkdir -p $NNDESTDIR/System/Core/Boot/EFI/BOOT
        cp $NNDESTDIR/System/Core/Boot/nexboot.efi \
            $NNDESTDIR/System/Core/Boot/EFI/BOOT/BOOT${NNEFIARCH}.EFI
    fi
    echo "usr" >> $NNCONFROOT/nnimage-list.lst
    echo "bin" >> $NNCONFROOT/nnimage-list.lst
    echo "sbin" >> $NNCONFROOT/nnimage-list.lst
    echo "etc" >> $NNCONFROOT/nnimage-list.lst
    # Generate configuration file
    cd $NNCONFROOT
    echo "image nnimg" > nnimage.conf
    echo "{" >> nnimage.conf
    if [ "$NNIMGTYPE" = "mbr" ]
    then
        echo "    defaultFile: '$NNCONFROOT/nndisk.img';" >> nnimage.conf
        echo "    sizeMul: MiB;" >> nnimage.conf
        echo "    size: 128;" >> nnimage.conf
        echo "    format: mbr;" >> nnimage.conf
        # Ensure boot mode is BIOS
        if [ "$NNIMGBOOTMODE" != "bios" ]
        then
            panic "only BIOS boot mode is allowed on MBR disks"
        fi
        echo "    bootMode: bios;" >> nnimage.conf
        # Set path to MBR
        echo "    mbrFile: '$NNDESTDIR/System/Core/Boot/bootrec/hdmbr';" >> nnimage.conf
        echo "}" >> nnimage.conf
        # Output partitions
        echo "partition boot" >> nnimage.conf
        echo "{" >> nnimage.conf
        echo "    start: 1;" >> nnimage.conf
        echo "    size: 35;" >> nnimage.conf
        echo "    format: fat32;" >> nnimage.conf
        echo "    prefix: '/System/Core/Boot';" >> nnimage.conf
        echo "    isBoot: true;" >> nnimage.conf
        echo "    vbrFile: '$NNDESTDIR/System/Core/Boot/bootrec/hdvbr';" >> nnimage.conf
        echo "    image: nnimg;" >> nnimage.conf
        echo "}" >> nnimage.conf
        echo "partition system" >> nnimage.conf
        echo "{" >> nnimage.conf
        echo "    start: 36;" >> nnimage.conf
        echo "    size: 85;" >> nnimage.conf
        echo "    format: ext2;" >> nnimage.conf
        echo "    prefix: '/';" >> nnimage.conf
        echo "    image: nnimg;" >> nnimage.conf
        echo "}" >> nnimage.conf
    elif [ "$NNIMGTYPE" = "gpt" ]
    then
        echo "    defaultFile: '$NNCONFROOT/nndisk.img';" >> nnimage.conf
        echo "    sizeMul: MiB;" >> nnimage.conf
        echo "    size: 128;" >> nnimage.conf
        echo "    format: gpt;" >> nnimage.conf
        # Check for valid boot mode
        if [ "$NNIMGBOOTMODE" = "isofloppy" ] || [ "$NNIMGBOOTMODE" = "none" ]
        then
            panic "invalid boot mode specified"
        fi
        echo "    bootMode: $NNIMGBOOTMODE;" >> nnimage.conf
        # Set path to MBR if needed
        if [ "$NNIMGBOOTMODE" != "efi" ]
        then
            echo "    mbrFile: '$NNDESTDIR/System/Core/Boot/bootrec/gptmbr';" >> nnimage.conf
        fi
        echo "}" >> nnimage.conf
        # Output partitions
        echo "partition boot" >> nnimage.conf
        echo "{" >> nnimage.conf
        echo "    start: 1;" >> nnimage.conf
        echo "    size: 35;" >> nnimage.conf
        echo "    format: fat32;" >> nnimage.conf
        echo "    prefix: '/System/Core/Boot';" >> nnimage.conf
        echo "    isBoot: true;" >> nnimage.conf
        echo "    image: nnimg;" >> nnimage.conf
        if [ "$NNIMGBOOTMODE" != "efi" ]
        then
            echo "    vbrFile: '$NNDESTDIR/System/Core/Boot/bootrec/hdvbr';" >> nnimage.conf
        fi
        echo "}" >> nnimage.conf
        echo "partition system" >> nnimage.conf
        echo "{" >> nnimage.conf
        echo "    start: 36;" >> nnimage.conf
        echo "    size: 85;" >> nnimage.conf
        echo "    format: ext2;" >> nnimage.conf
        echo "    prefix: '/';" >> nnimage.conf
        echo "    image: nnimg;" >> nnimage.conf
        echo "}" >> nnimage.conf
    elif [ "$NNIMGTYPE" = "iso9660" ]
    then
        echo "    defaultFile: '$NNCONFROOT/nncdrom.iso';" >> nnimage.conf
        echo "    sizeMul: KiB;" >> nnimage.conf
        echo "    format: iso9660;" >> nnimage.conf
        if [ "$NNIMGBOOTMODE" = "isofloppy" ]
        then
            echo "    bootMode: noboot;" >> nnimage.conf
        else
            echo "    bootMode: $NNIMGBOOTMODE;" >> nnimage.conf
            if [ "$NNIMGBOOTMODE" != "efi" ]
            then
                echo "    bootEmu: $NNIMGBOOTEMU;" >> nnimage.conf
            fi
        fi
        if [ "$NNIMGBOOTMODE" = "isofloppy" ]
        then
            # Write out boot floppy
            echo "}" >> nnimage.conf
            echo "image nnboot" >> nnimage.conf
            echo "{" >> nnimage.conf
            echo "    defaultFile: '$NNCONFROOT/nndisk.flp';" >> nnimage.conf
            echo "    sizeMul: KiB;" >> nnimage.conf
            echo "    size: 1440;" >> nnimage.conf
            echo "    format: floppy;" >> nnimage.conf
            echo "    mbrFile: '$NNDESTDIR/System/Core/Boot/bootrec/fdmbr';" >> nnimage.conf
            echo "}" >> nnimage.conf
            echo "partition bootpart" >> nnimage.conf
            echo "{" >> nnimage.conf
            echo "    size: 1440;" >> nnimage.conf
            echo "    prefix: '/System/Core/Boot';" >> nnimage.conf
            echo "    format: fat12;" >> nnimage.conf
            echo "    isBoot: true;" >> nnimage.conf
            echo "    image: nnboot;" >> nnimage.conf
        elif [ "$NNIMGBOOTMODE" = "bios" ]
        then
            if [ "$NNIMGBOOTEMU" = "noemu" ]
            then
                echo "    mbrFile: '$NNDESTDIR/System/Core/Boot/bootrec/isombr';" >> nnimage.conf
            elif [ "$NNIMGBOOTEMU" = "hdd" ]
            then
                echo "    mbrFile: '$NNDESTDIR/System/Core/Boot/bootrec/hdmbr';" >> nnimage.conf
            fi
        fi
        echo "}" >> nnimage.conf
        # Create partitions
        if [ "$NNIMGBOOTMODE" = "bios" ]
        then
            if [ "$NNIMGBOOTEMU" = "hdd" ]
            then
                echo "partition boot" >> nnimage.conf
                echo "{" >> nnimage.conf
                echo "    start: 1;" >> nnimage.conf
                echo "    size: 131050;" >> nnimage.conf
                echo "    format: fat32;" >> nnimage.conf
                echo "    isBoot: true;"  >> nnimage.conf
                echo "    prefix: '/System/Core/Boot';" >> nnimage.conf
                echo "    image: nnimg;" >> nnimage.conf
                echo "    vbrFile: '$NNDESTDIR/System/Core/Boot/bootrec/hdvbr';" >> nnimage.conf
                echo "}" >> nnimage.conf
            elif [ "$NNIMGBOOTEMU" = "fdd" ]
            then
                echo "partition boot" >> nnimage.conf
                echo "{" >> nnimage.conf
                echo "    size: 1440;" >> nnimage.conf
                echo "    format: fat12;" >> nnimage.conf
                echo "    isBoot: true;"  >> nnimage.conf
                echo "    prefix: '/System/Core/Boot';" >> nnimage.conf
                echo "    image: nnimg;" >> nnimage.conf
                echo "    vbrFile: '$NNDESTDIR/System/Core/Boot/bootrec/fdmbr';" >> nnimage.conf
                echo "}" >> nnimage.conf
            fi
        fi
        if [ "$NNIMGBOOTMODE" = "efi" ]
        then
            echo "partition boot" >> nnimage.conf
            echo "{" >> nnimage.conf
            echo "    format: fat32;" >> nnimage.conf
            echo "    size: 131072;" >> nnimage.conf
            echo "    isBoot: true;" >> nnimage.conf
            echo "    prefix: '/System/Core/Boot';" >> nnimage.conf
            echo "    image: nnimg;" >> nnimage.conf
            echo "}" >> nnimage.conf
        fi
        echo "partition system" >> nnimage.conf
        echo "{" >> nnimage.conf
        echo "    format: iso9660;" >> nnimage.conf
        echo "    prefix: '/';" >> nnimage.conf
        echo "    image: nnimg;" >> nnimage.conf
        if [ "$NNIMGBOOTEMU" = "noemu" ]
        then
            echo "    isBoot: true;" >> nnimage.conf
        fi
        echo "}" >> nnimage.conf
    fi
else
    panic "invalid component $component specified"
fi
