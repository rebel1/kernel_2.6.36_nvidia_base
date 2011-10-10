#!/bin/sh
make ANDROID_ENV=1 ANDROID=1 ATH_LINUXPATH=/home/rebel1/kernel_2.6.36_nvidia_base ATH_CROSS_COMPILE_TYPE=/home/rebel1/cyanogen-gingerbread/android/system/prebuilt//linux-x86/toolchain/arm-eabi-4.4.0/bin/arm-eabi-
mkdir bin
cp -fr host/.output/tegra-sdio/image/ar6000.ko bin
cp -fr ../AR6kSDK.2.2.1.151-proprietary/target/AR6002/hw2.0/bin/* bin
