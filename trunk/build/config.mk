#To build Raspberry Pi 3 and 2, set ARCH=arm7 and PLAT=rpi
#To build Raspberry Pi Zero and 1, set ARCH=arm6 and PLAT=rpi
#To build CentOS, set PLAT=centos

CC = g++

ARCH = x64
OPT = release
PLAT = ubuntu

INC_DIR = ../include
INC_DIR_LNX = $(INC_DIR)/linux
EXT_INC_DIR = ../../external/include

SRC_DIR = ../src
SRC_DIR_LNX = $(SRC_DIR)/linux

EXT_LIB_DIR = ../../external/lib/$(OPT)/$(ARCH)/$(PLAT)
OUT = ../out
OBJ_DIR = $(OUT)/$(OPT)/$(ARCH)/obj
BIN_DIR = $(OUT)/$(OPT)/$(ARCH)

BINARY = ipop-tincan
TARGET = $(patsubst %,$(BIN_DIR)/%,$(BINARY))

defines = -DLINUX -D_IPOP_LINUX -DWEBRTC_POSIX -DWEBRTC_LINUX

cflags_cc = -std=c++14 -O3 -pthread -g2 -gsplit-dwarf -fno-strict-aliasing --param=ssp-buffer-size=4 -fstack-protector -funwind-tables -fPIC -pipe -Wall -fno-rtti

LIBS = -ljsoncpp -lrtc_p2p -lrtc_base -lrtc_base_approved -lfield_trial_default -lboringssl -lboringssl_asm -lprotobuf_lite -lpthread

HDR_FILES = $(wildcard $(INC_DIR)/*.h)
SRC_FILES = $(wildcard $(SRC_DIR)/*.cc)
OBJ_FILES = $(patsubst $(SRC_DIR)/%.cc, $(OBJ_DIR)/%.o, $(SRC_FILES))

LSRC_FILES = $(wildcard $(SRC_DIR_LNX)/*.cc)
LOBJ_FILES = $(patsubst $(SRC_DIR_LNX)/%.cc, $(OBJ_DIR)/%.o, $(LSRC_FILES))
