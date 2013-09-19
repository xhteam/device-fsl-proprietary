LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
	main.c 

LOCAL_SHARED_LIBRARIES := \
	liblog \
    libcutils
    
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE := udev_monitor

include $(BUILD_EXECUTABLE)

