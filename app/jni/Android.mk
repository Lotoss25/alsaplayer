
LOCAL_PATH := $(call my-dir)

# doesn't fuckingly work here: use "ndk-build TARGET_PLATFORM=android-18"
# TARGET_PLATFORM := android-18

# common codecs & startup library
include $(CLEAR_VARS)
LOCAL_MODULE := lossless
LOCAL_STATIC_LIBRARIES := flac ape xmlparser
LOCAL_CFLAGS += -O3 -Wall -finline-functions -fPIC -I$(LOCAL_PATH)/include
LOCAL_CFLAGS += -DHAVE_CONFIG_H -DCLASS_NAME=\"net/avs234/alsaplayer/AlsaPlayerSrv\"
LOCAL_CFLAGS += -DBUILD_STANDALONE -DCPU_ARM
#LOCAL_ARM_MODE := arm
LOCAL_SRC_FILES := main.c alsa.c alsa_offload.c buffer.c alac_main.c wav_main.c compr.c compr0101.c compr0102.c
LOCAL_LDLIBS := -llog -ldl
include $(BUILD_SHARED_LIBRARY)

#include $(CLEAR_VARS)
#LOCAL_MODULE := flactest
#LOCAL_STATIC_LIBRARIES := flac
#LOCAL_CFLAGS += -O3 -Wall -finline-functions -fPIC -DHAVE_CONFIG_H -Iinclude
#LOCAL_SRC_FILES := flactest.c
#LOCAL_LDLIBS := -llog -ldl
#include $(BUILD_EXECUTABLE)

SUBDIRS := flac ape tinyxml
codec-makefiles =  $(patsubst %,$(LOCAL_PATH)/%/Android.mk,$(SUBDIRS)) 
include $(call codec-makefiles)

