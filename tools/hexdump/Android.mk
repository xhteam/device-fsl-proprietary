LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES := hexdump.c 

LOCAL_CFLAGS := -O2 -Wall -Wno-unused-parameter -DHEXDUMP_MAIN=1
LOCAL_MODULE := hexdump

LOCAL_MODULE_TAGS := optional
include $(BUILD_EXECUTABLE)
