LOG_TO_ANDROID_LOGCAT := true

LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES:= tinyxml2.cpp xmlparser.cpp
LOCAL_MODULE:=xmlparser
LOCAL_CFLAGS += -DANDROID_NDK -I$(LOCAL_PATH)/../include

include $(BUILD_STATIC_LIBRARY)
