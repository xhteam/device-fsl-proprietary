LOCAL_PATH:= $(call my-dir)

#i2c detect
include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
	i2cbusses.c \
	i2cdetect.c

LOCAL_SHARED_LIBRARIES := \
	liblog \
    libcutils

LOCAL_MODULE := i2cdetect

LOCAL_MODULE_TAGS := optional

include $(BUILD_EXECUTABLE)

#i2c dump
include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
	i2cbusses.c \
	util.c \
	i2cdump.c

LOCAL_SHARED_LIBRARIES := \
	liblog \
    libcutils

LOCAL_MODULE := i2cdump

LOCAL_MODULE_TAGS := optional

include $(BUILD_EXECUTABLE)


#i2c set
include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
	i2cbusses.c \
	util.c \
	i2cset.c

LOCAL_SHARED_LIBRARIES := \
	liblog \
    libcutils

LOCAL_MODULE := i2cset

LOCAL_MODULE_TAGS := optional

include $(BUILD_EXECUTABLE)


#i2c get
include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
	i2cbusses.c \
	util.c \
	i2cget.c

LOCAL_SHARED_LIBRARIES := \
	liblog \
    libcutils

LOCAL_MODULE := i2cget

LOCAL_MODULE_TAGS := optional

include $(BUILD_EXECUTABLE)

