LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
	main.c

LOCAL_CFLAGS += \
	

LOCAL_SHARED_LIBRARIES := \
       libcutils

LOCAL_MODULE := serialcomm
LOCAL_MODULE_TAGS := optional
include $(BUILD_EXECUTABLE)

