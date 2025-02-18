ifeq (0,1)

LOCAL_PATH:= $(call my-dir)

#MAKE_JAR
include $(CLEAR_VARS)
LOCAL_SRC_FILES := $(call all-java-files-under, src-Wrapper)
LOCAL_JAVA_LIBRARIES := telephony-common
LOCAL_MODULE := phonebook_wrapper
LOCAL_MODULE_TAGS := optional
LOCAL_CERTIFICATE := platform
include $(BUILD_STATIC_JAVA_LIBRARY)

include $(CLEAR_VARS)

LOCAL_MODULE_TAGS := optional

contacts_common_dir := ../ContactsCommon
phone_common_dir := ../PhoneCommon

#ifeq ($(TARGET_BUILD_APPS),)
#support_library_root_dir := frameworks/support
#else
#support_library_root_dir := prebuilts/sdk/current/support
#endif

src_dirs := src $(contacts_common_dir)/src $(phone_common_dir)/src
res_dirs := res res-aosp $(contacts_common_dir)/res $(contacts_common_dir)/icons/res $(phone_common_dir)/res
asset_dirs := $(contacts_common_dir)/assets

LOCAL_SRC_FILES := $(call all-java-files-under, $(src_dirs))
LOCAL_SRC_FILES += src/org/codeaurora/presenceserv/IPresenceService.aidl \
                   src/org/codeaurora/presenceserv/IPresenceServiceCB.aidl

LOCAL_RESOURCE_DIR := $(addprefix $(LOCAL_PATH)/, $(res_dirs)) \
    $(support_library_root_dir)/v7/cardview/res
LOCAL_ASSET_DIR := $(addprefix $(LOCAL_PATH)/, $(asset_dirs))

LOCAL_AAPT_FLAGS := \
    --auto-add-overlay \
    --extra-packages com.android.contacts.common \
    --extra-packages com.android.phone.common \
    --extra-packages android.support.v7.cardview

LOCAL_STATIC_JAVA_LIBRARIES := \
    com.android.vcard \
    android-common \
    guava \
    android-support-v13 \
    android-support-v7-cardview \
    android-support-v7-palette \
    android-support-v4 \
    libphonenumber \
    phonebook_wrapper

LOCAL_PACKAGE_NAME := Contacts
LOCAL_CERTIFICATE := shared
LOCAL_PRIVILEGED_MODULE := true
LOCAL_JAVA_LIBRARIES := telephony-common rcscommon
LOCAL_PROGUARD_FLAG_FILES := proguard.flags

LOCAL_SDK_VERSION := current
LOCAL_MIN_SDK_VERSION := 21

include $(BUILD_PACKAGE)

# Use the folloing include to make our test apk.
include $(call all-makefiles-under,$(LOCAL_PATH))

endif

