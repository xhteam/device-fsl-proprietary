LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

HAS_LIBUSB:=false

LOCAL_SRC_FILES:= \
    atchannel.c \
    misc.c \
    eventset.c \
    at_tok.c\
    sms.c \
    itu_network.c \
    ril-requestdatahandler.c \
    ril-core.c \
    ril-hardware.c \
    ril-call.c\
    ril-message.c\
    ril-network.c\
    ril-oem.c\
    ril-pdp.c\
    ril-pdp-em350.c \
    ril-services.c\
    ril-sim.c\
    ril-device.c\
    ril-stk.c \
    ptt.c \
    ril-fake.c 


LOCAL_SHARED_LIBRARIES := \
    libcutils libutils libnetutils libril libhardware_legacy


# for asprinf
LOCAL_CFLAGS := -D_GNU_SOURCE -DRIL_SHLIB \
	-Wno-format-security 

LOCAL_LDLIBS := -lpthread

LOCAL_C_INCLUDES := $(KERNEL_HEADERS) \
	$(TOP)/hardware/ril/libril/
	
ifeq ($(HAS_LIBUSB), true)
    LOCAL_CFLAGS+=-DHAS_LIBUSB
    LOCAL_SHARED_LIBRARIES+=libusb
    LOCAL_C_INCLUDES+= external/libusb    
endif

LOCAL_PRELINK_MODULE := false

  #build shared library
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE:= libqst_ril
include $(BUILD_SHARED_LIBRARY)

include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
	sms_test.c 
LOCAL_MODULE_TAGS := tests
LOCAL_MODULE := sms_test

include $(BUILD_EXECUTABLE)


