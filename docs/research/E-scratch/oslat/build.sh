#!/bin/bash
J=/Users/seanfunk/audio/plugins/HardwareReverb/build/_deps/juce-src/modules
F=(-std=c++17 -O2 -mcpu=apple-m1 -DNDEBUG -I. -I$J -include AppConfig.h)
clang++ "${F[@]}" -c m1.mm -o m1.o & clang++ "${F[@]}" -c m2.mm -o m2.o & clang++ "${F[@]}" -c m3.mm -o m3.o & clang++ "${F[@]}" -c m4.cpp -o m4.o & clang++ "${F[@]}" -c main.cpp -o main.o & wait
clang++ m1.o m2.o m3.o m4.o main.o -o oslat -framework Cocoa -framework Foundation -framework IOKit -framework Security -framework Accelerate -framework CoreAudio -framework CoreMIDI -framework QuartzCore -framework AudioToolbox
