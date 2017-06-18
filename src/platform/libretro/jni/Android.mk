LOCAL_PATH := $(call my-dir)
GLES=1

include $(CLEAR_VARS)

LOCAL_MODULE  := retro
LOCAL_CLFAGS   =
LOCAL_CXXFLAGS =

ifeq ($(TARGET_ARCH),arm)
LOCAL_CFLAGS += -DANDROID_ARM
LOCAL_ARM_MODE := arm
endif

ifeq ($(TARGET_ARCH),x86)
LOCAL_CFLAGS += -DANDROID_X86
endif

ifeq ($(TARGET_ARCH),mips)
LOCAL_CFLAGS += -DANDROID_MIPS
endif

ifeq ($(GLES), 3)
   LOCAL_CFLAGS += -DHAVE_OPENGLES3 -DGLES3
   GLES_LIB := -lGLESv3
else
   LOCAL_CFLAGS += -DHAVE_OPENGLES2 -DGLES3
   GLES_LIB := -lGLESv2
endif

CORE_DIR := ..

include ../Makefile.common

LOCAL_SRC_FILES := $(SOURCES_C) $(SOURCES_CXX)

LOCAL_CXXFLAGS   += -O2 -Wall -std=c++11 -ffast-math -DHAVE_OPENGLES -D__LIBRETRO__
LOCAL_CFLAGS     += -O2 -Wall -ffast-math -DHAVE_OPENGLES -D__LIBRETRO__
LOCAL_C_INCLUDES = $(CORE_DIR) \
						 $(CORE_DIR)/../.. \
						 $(CORE_DIR)/glsym
LOCAL_LDLIBS += $(GLES_LIB)

include $(BUILD_SHARED_LIBRARY)

