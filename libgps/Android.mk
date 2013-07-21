LOCAL_PATH := $(call my-dir)

ifeq ($(BOARD_HAVE_HARDWARE_GPS),true)
ifeq ($(USE_NMEA_GPS_HARDWARE),true)

include $(CLEAR_VARS)
 
LOCAL_SRC_FILES := \
	gpshal.c
	

LOCAL_SHARED_LIBRARIES := \
	liblog \
	libcutils \

LOCAL_PRELINK_MODULE := false
LOCAL_MODULE_PATH := $(TARGET_OUT_SHARED_LIBRARIES)/hw	
LOCAL_MODULE := gps.freescale
LOCAL_MODULE_TAGS := optional

include $(BUILD_SHARED_LIBRARY)

endif
endif

