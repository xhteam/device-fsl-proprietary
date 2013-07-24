LOCAL_PATH:= $(call my-dir)

# mksplash host tool
# this tool is for building splash.img
include $(CLEAR_VARS)

LOCAL_C_INCLUDES := external/kernel-headers/original

LOCAL_SRC_FILES := mksplash.c bmpmanager.c flashmanager.c mtd.c

LOCAL_CFLAGS += -O2 -Wall -Wno-unused-parameter
LOCAL_MODULE := mksplash

include $(BUILD_HOST_EXECUTABLE)

