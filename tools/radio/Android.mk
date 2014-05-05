LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
	comtest.c


LOCAL_SHARED_LIBRARIES := \
       libcutils

LOCAL_MODULE := atem305
LOCAL_MODULE_TAGS := optional
include $(BUILD_EXECUTABLE)


include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
	main.c


LOCAL_SHARED_LIBRARIES := \
       libcutils

LOCAL_MODULE := em305
LOCAL_MODULE_TAGS := optional
include $(BUILD_EXECUTABLE)


