ifeq ($(BOARD_HAVE_HARDWARE_GPS),true)
ifeq ($(USE_NMEA_GPS_HARDWARE),true)

LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)
 
LOCAL_SRC_FILES := \
	gpshal.c
	
LOCAL_C_INCLUDES := \
	$(LOCAL_PATH) \
	hardware\libhardware_legacy


LOCAL_SHARED_LIBRARIES := \
	liblog \
	libcutils \
	libhardware

LOCAL_MODULE_PATH := $(TARGET_OUT_SHARED_LIBRARIES)/hw	
LOCAL_MODULE := gps.$(TARGET_BOOTLOADER_BOARD_NAME)
LOCAL_MODULE_TAGS := optional


include $(BUILD_SHARED_LIBRARY)

endif
endif

