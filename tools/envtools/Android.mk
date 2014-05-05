LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
	crc32.c fw_env.c  fw_env_main.c


#-DDEBUG    
LOCAL_CFLAGS := -DUSE_HOSTCC -DRAW_DEVICE 

LOCAL_MODULE := envtool
LOCAL_MODULE_TAGS := optional
include $(BUILD_EXECUTABLE)

# Make #!/system/bin/envtool launchers for each tool.
#
# Make a symlink from /sbin/ueventd and /sbin/watchdogd to /init
SYMLINKS := \
	$(TARGET_OUT)/bin/fw_printenv \
	$(TARGET_OUT)/bin/fw_setenv

$(SYMLINKS): ENVTOOL_BINARY := $(LOCAL_MODULE)
$(SYMLINKS): $(LOCAL_INSTALLED_MODULE) $(LOCAL_PATH)/Android.mk
	@echo "Symlink: $@ -> $(ENVTOOL_BINARY)"
	@mkdir -p $(dir $@)
	@rm -rf $@
	$(hide) ln -sf $(ENVTOOL_BINARY) $@

ALL_DEFAULT_INSTALLED_MODULES += $(SYMLINKS)

# We need this so that the installed files could be picked up based on the
# local module name
ALL_MODULES.$(LOCAL_MODULE).INSTALLED := \
    $(ALL_MODULES.$(LOCAL_MODULE).INSTALLED) $(SYMLINKS)


