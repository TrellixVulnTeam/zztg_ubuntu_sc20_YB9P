/*
 * Copyright (C) 2011 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package com.android.cellbroadcastreceiver;

import android.app.ActivityManagerNative;
import android.app.Notification;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.database.Cursor;
import android.os.Binder;
import android.os.Bundle;
import android.os.IBinder;
import android.os.PersistableBundle;
import android.os.RemoteException;
import android.os.SystemClock;
import android.os.UserHandle;
import android.os.SystemProperties;
import android.os.PersistableBundle;
import android.preference.PreferenceManager;
import android.provider.Telephony;
import android.telephony.CarrierConfigManager;
import android.telephony.CellBroadcastMessage;
import android.telephony.TelephonyManager;
import android.telephony.SmsCbCmasInfo;
import android.telephony.SmsCbEtwsInfo;
import android.telephony.SmsCbLocation;
import android.telephony.SmsCbMessage;
import android.telephony.SubscriptionManager;
import android.telephony.CarrierConfigManager;
import android.util.Log;

import com.android.cellbroadcastreceiver.CellBroadcastAlertAudio.ToneType;
import com.android.cellbroadcastreceiver.CellBroadcastOtherChannelsManager.CellBroadcastChannelRange;
import com.android.internal.annotations.VisibleForTesting;
import com.android.internal.telephony.PhoneConstants;

import java.util.ArrayList;
import java.util.HashSet;
import java.util.Iterator;
import java.util.Locale;

import android.preference.PreferenceManager;
import android.content.SharedPreferences;
import java.util.LinkedHashMap;
import java.util.Locale;

import static android.text.format.DateUtils.DAY_IN_MILLIS;

/**
 * This service manages the display and animation of broadcast messages.
 * Emergency messages display with a flashing animated exclamation mark icon,
 * and an alert tone is played when the alert is first shown to the user
 * (but not when the user views a previously received broadcast).
 */
public class CellBroadcastAlertService extends Service {
    private static final String TAG = "CBAlertService";

    /** Intent action to display alert dialog/notification, after verifying the alert is new. */
    static final String SHOW_NEW_ALERT_ACTION = "cellbroadcastreceiver.SHOW_NEW_ALERT";

    /** Use the same notification ID for non-emergency alerts. */
    static final int NOTIFICATION_ID = 1;

    /** Sticky broadcast for latest area info broadcast received. */
    static final String CB_AREA_INFO_RECEIVED_ACTION =
            "android.cellbroadcastreceiver.CB_AREA_INFO_RECEIVED";
    /** system property to enable/disable broadcast duplicate detecion.  */
    private static final String CB_DUP_DETECTION = "persist.cb.dup_detection";

    /** Check for system property to enable/disable duplicate detection.  */
    static boolean mUseDupDetection = SystemProperties.getBoolean(CB_DUP_DETECTION, true);

    /** Channel 50 Cell Broadcast. */
    static final int CB_CHANNEL_50 = 50;

    /** Channel 60 Cell Broadcast. */
    static final int CB_CHANNEL_60 = 60;
    private static int TIME12HOURS = 12*60*60*1000;
    private boolean mDuplicateCheckDatabase = false;

    @Override
    public void onCreate() {
        super.onCreate();
        mDuplicateCheckDatabase = getResources().getBoolean(
                R.bool.config_regional_wea_duplicated_check_database);
        if (mDuplicateCheckDatabase) {
            initHalfDayCmasList();
        }
    }

    private static final String COUNTRY_BRAZIL = "br";
    private static final String COUNTRY_INDIA = "in";

    /**
     * Default message expiration time is 24 hours. Same message arrives within 24 hours will be
     * treated as a duplicate.
     */
    private static final long DEFAULT_EXPIRATION_TIME = DAY_IN_MILLIS;

    /**
     *  Container for service category, serial number, location, body hash code, and ETWS primary/
     *  secondary information for duplication detection.
     */
    private static final class MessageServiceCategoryAndScope {
        private final int mServiceCategory;
        private final int mSerialNumber;
        private final SmsCbLocation mLocation;
        private final int mBodyHash;
        private final boolean mIsEtwsPrimary;
        private final SmsCbEtwsInfo mEtwsWarningInfo;
        private final long mDeliveryTime;
        private final String mMessageBody;

        MessageServiceCategoryAndScope(int serviceCategory, int serialNumber,
                SmsCbLocation location, int bodyHash, boolean isEtwsPrimary) {
            mServiceCategory = serviceCategory;
            mSerialNumber = serialNumber;
            mLocation = location;
            mBodyHash = bodyHash;
            mIsEtwsPrimary = isEtwsPrimary;
            mEtwsWarningInfo = null;
            mMessageBody = null;
            mDeliveryTime = 0;
        }

        MessageServiceCategoryAndScope(int serviceCategory, int serialNumber,
                SmsCbLocation location, int bodyHash, boolean isEtwsPrimary,
                SmsCbEtwsInfo etwsWarningInfo) {
            mServiceCategory = serviceCategory;
            mSerialNumber = serialNumber;
            mLocation = location;
            mBodyHash = bodyHash;
            mIsEtwsPrimary = isEtwsPrimary;
            mEtwsWarningInfo = etwsWarningInfo;
            mMessageBody = null;
            mDeliveryTime = 0;
        }

        MessageServiceCategoryAndScope(int serviceCategory, int serialNumber,
                SmsCbLocation location, String messageBody, long deliveryTime,
                int bodyHash, boolean isEtwsPrimary) {
            mServiceCategory = serviceCategory;
            mSerialNumber = serialNumber;
            mLocation = location;
            mMessageBody = messageBody;
            mDeliveryTime = deliveryTime;
            mBodyHash = bodyHash;
            mIsEtwsPrimary = isEtwsPrimary;
            mEtwsWarningInfo = null;
        }

        @Override
        public int hashCode() {
            if (mEtwsWarningInfo != null) {
                return mEtwsWarningInfo.hashCode() + mLocation.hashCode() + 5 * mServiceCategory
                        + 7 * mSerialNumber + 13 * mBodyHash;
            }
            return mLocation.hashCode() + 5 * mServiceCategory + 7 * mSerialNumber + 13 * mBodyHash;
        }

        @Override
        public boolean equals(Object o) {
            if (o == this) {
                return true;
            }
            if (o instanceof MessageServiceCategoryAndScope) {
                MessageServiceCategoryAndScope other = (MessageServiceCategoryAndScope) o;
                if (mEtwsWarningInfo == null && other.mEtwsWarningInfo != null) {
                    return false;
                } else if (mEtwsWarningInfo != null && other.mEtwsWarningInfo == null) {
                    return false;
                } else if (mEtwsWarningInfo != null && other.mEtwsWarningInfo != null
                        && !mEtwsWarningInfo.equals(other.mEtwsWarningInfo)) {
                    return false;
                }
                return (mServiceCategory == other.mServiceCategory &&
                        mSerialNumber == other.mSerialNumber &&
                        mLocation.equals(other.mLocation) &&
                        mBodyHash == other.mBodyHash &&
                        mIsEtwsPrimary == other.mIsEtwsPrimary &&
                        ((mMessageBody == null) ? (other.mMessageBody == null)
                        : (mMessageBody.equals(other.mMessageBody))));
            }
            return false;
        }

        @Override
        public String toString() {
            return "{mServiceCategory: " + mServiceCategory + " serial number: " + mSerialNumber
                    + " location: " + mLocation.toString() + " mEtwsWarningInfo: "
                    + (mEtwsWarningInfo == null ? "NULL" : mEtwsWarningInfo.toString())
                    + " body hash: " + mBodyHash + " mIsEtwsPrimary: " + mIsEtwsPrimary +'}';
        }
    }

    /** Maximum number of message IDs to save before removing the oldest message ID. */
    private static final int MAX_MESSAGE_ID_SIZE = 1024;

    /** Cache of received message IDs, for duplicate message detection. */
    private static final HashSet<MessageServiceCategoryAndScope> sCmasIdSet =
            new HashSet<MessageServiceCategoryAndScope>(8);
    /** List of message IDs received, for removing oldest ID when max message IDs are received. */
    private static final ArrayList<MessageServiceCategoryAndScope> sCmasIdList =
            new ArrayList<MessageServiceCategoryAndScope>(8);
    /** Index of message ID to replace with new message ID when max message IDs are received. */
    private static int sCmasIdListIndex = 0;
    /** List of message IDs received for recent 12 hours. */
    private static final ArrayList<MessageServiceCategoryAndScope> s12HIdList =
        new ArrayList<MessageServiceCategoryAndScope>(8);

    /** Linked hash map of the message identities for duplication detection purposes. The key is the
     * the collection of different message keys used for duplication detection, and the value
     * is the timestamp of message arriving time. Some carriers may require shorter expiration time.
     */
    private static final LinkedHashMap<MessageServiceCategoryAndScope, Long> sMessagesMap =
            new LinkedHashMap<>();

    private void initHalfDayCmasList() {
        long now = System.currentTimeMillis();
        // This is used to query necessary fields from cmas table
        // which are related duplicate check
        // for example receive date, cmas id and so on
        String[] project = new String[] {
            Telephony.CellBroadcasts.PLMN,
            Telephony.CellBroadcasts.LAC,
            Telephony.CellBroadcasts.CID,
            Telephony.CellBroadcasts.DELIVERY_TIME,
            Telephony.CellBroadcasts.SERVICE_CATEGORY,
            Telephony.CellBroadcasts.SERIAL_NUMBER,
            Telephony.CellBroadcasts.MESSAGE_BODY};
        Cursor cursor = getApplicationContext().getContentResolver().query(
                Telephony.CellBroadcasts.CONTENT_URI,project,
                Telephony.CellBroadcasts.DELIVERY_TIME + ">?",
                new String[]{now - TIME12HOURS + ""},
                Telephony.CellBroadcasts.DELIVERY_TIME + " DESC");
        if (s12HIdList != null) {
            s12HIdList.clear();
        }
        MessageServiceCategoryAndScope newCmasId;
        int serviceCategory;
        int serialNumber;
        String messageBody;
        long deliveryTime;
        if(cursor != null){
            int plmnColumn = cursor.getColumnIndex(Telephony.CellBroadcasts.PLMN);
            int lacColumn = cursor.getColumnIndex(Telephony.CellBroadcasts.LAC);
            int cidColumn = cursor.getColumnIndex(Telephony.CellBroadcasts.CID);
            int serviceCategoryColumn = cursor.getColumnIndex(
                    Telephony.CellBroadcasts.SERVICE_CATEGORY);
            int serialNumberColumn = cursor.getColumnIndex(
                    Telephony.CellBroadcasts.SERIAL_NUMBER);
            int messageBodyColumn = cursor.getColumnIndex(Telephony.CellBroadcasts.MESSAGE_BODY);
            int deliveryTimeColumn = cursor.getColumnIndex(
                    Telephony.CellBroadcasts.DELIVERY_TIME);
            while(cursor.moveToNext()){
                String plmn = getStringColumn(plmnColumn, cursor);
                int lac = getIntColumn(lacColumn, cursor);
                int cid = getIntColumn(cidColumn, cursor);
                SmsCbLocation location = new SmsCbLocation(plmn, lac, cid);
                serviceCategory = getIntColumn(serviceCategoryColumn, cursor);
                serialNumber = getIntColumn(serialNumberColumn, cursor);
                messageBody = getStringColumn(messageBodyColumn, cursor);
                deliveryTime = getLongColumn(deliveryTimeColumn, cursor);
                newCmasId = new MessageServiceCategoryAndScope(
                        serviceCategory, serialNumber, location, messageBody,
                        deliveryTime, messageBody.hashCode(), false);
                s12HIdList.add(newCmasId);
            }
        }
        if(cursor != null){
            cursor.close();
        }
    }

    private boolean isDuplicated(SmsCbMessage message) {
        if(!mDuplicateCheckDatabase) {
            return false ;
        }
        final CellBroadcastMessage cbm = new CellBroadcastMessage(message);
        long lastestDeliveryTime = cbm.getDeliveryTime();
        int hashCode = message.isEtwsMessage() ? message.getMessageBody().hashCode() : 0;
        MessageServiceCategoryAndScope newCmasId = new MessageServiceCategoryAndScope(
                message.getServiceCategory(), message.getSerialNumber(),
                message.getLocation(), message.getMessageBody(), lastestDeliveryTime,
                hashCode, false);
        Iterator<MessageServiceCategoryAndScope> iterator = s12HIdList.iterator();
        ArrayList<MessageServiceCategoryAndScope> tempMessageList =
                new ArrayList<MessageServiceCategoryAndScope>();
        boolean duplicatedMessage = false;
        while(iterator.hasNext()){
            MessageServiceCategoryAndScope tempMessage =
                    (MessageServiceCategoryAndScope)iterator.next();
            boolean moreThan12Hour = (lastestDeliveryTime - tempMessage
                    .mDeliveryTime >= TIME12HOURS);
            if (moreThan12Hour) {
                s12HIdList.remove(message);
                break;
            } else {
                tempMessageList.add(tempMessage);
                if (tempMessage.equals(newCmasId)) {
                    duplicatedMessage = true;
                    break;
                }
            }
        }
        if (duplicatedMessage) {
            if (tempMessageList != null) {
                tempMessageList.clear();
                tempMessageList = null;
            }
            return true;
        } else {
            if (s12HIdList != null) {
                s12HIdList.clear();
            }
            if (tempMessageList != null) {
                s12HIdList.addAll(tempMessageList);
                tempMessageList.clear();
                tempMessageList = null;
            }
            s12HIdList.add(0, newCmasId);
        }
        return false;
    }

    private String getStringColumn (int column, Cursor cursor) {
        if (column != -1 && !cursor.isNull(column)) {
            return cursor.getString(column);
        } else {
            return null;
        }
    }

    private int getIntColumn (int column, Cursor cursor) {
        if (column != -1 && !cursor.isNull(column)) {
            return cursor.getInt(column);
        } else {
            return -1;
        }
    }

    private long getLongColumn (int column, Cursor cursor) {
        if (column != -1 && !cursor.isNull(column)) {
            return cursor.getLong(column);
        } else {
            return -1;
        }
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        String action = intent.getAction();
        Log.d(TAG, "onStartCommand: " + action);
        if (Telephony.Sms.Intents.SMS_EMERGENCY_CB_RECEIVED_ACTION.equals(action) ||
                Telephony.Sms.Intents.SMS_CB_RECEIVED_ACTION.equals(action)) {
            handleCellBroadcastIntent(intent);
        } else if (SHOW_NEW_ALERT_ACTION.equals(action)) {
            try {
                if (UserHandle.myUserId() ==
                        ActivityManagerNative.getDefault().getCurrentUser().id) {
                    showNewAlert(intent);
                } else {
                    Log.d(TAG,"Not active user, ignore the alert display");
                }
            } catch (RemoteException e) {
                e.printStackTrace();
            }
        } else {
            Log.e(TAG, "Unrecognized intent action: " + action);
        }
        return START_NOT_STICKY;
    }

    /**
     * Get the carrier specific message duplicate expiration time.
     *
     * @param subId Subscription index
     * @return The expiration time in milliseconds. Small values like 0 (or negative values)
     * indicate expiration immediately (meaning the duplicate will always be displayed), while large
     * values indicate the duplicate will always be ignored. The default value would be 24 hours.
     */
    private long getDuplicateExpirationTime(int subId) {
        CarrierConfigManager configManager = (CarrierConfigManager)
                getApplicationContext().getSystemService(Context.CARRIER_CONFIG_SERVICE);
        Log.d(TAG, "manager = " + configManager);
        if (configManager == null) {
            Log.e(TAG, "carrier config is not available.");
            return DEFAULT_EXPIRATION_TIME;
        }

        PersistableBundle b = configManager.getConfigForSubId(subId);
        if (b == null) {
            Log.e(TAG, "expiration key does not exist.");
            return DEFAULT_EXPIRATION_TIME;
        }

        long time = b.getLong(CarrierConfigManager.KEY_MESSAGE_EXPIRATION_TIME_LONG,
                DEFAULT_EXPIRATION_TIME);
        return time;
    }

    private void handleCellBroadcastIntent(Intent intent) {
        Bundle extras = intent.getExtras();
        if (extras == null) {
            Log.e(TAG, "received SMS_CB_RECEIVED_ACTION with no extras!");
            return;
        }

        SmsCbMessage message = (SmsCbMessage) extras.get("message");

        if (message == null) {
            Log.e(TAG, "received SMS_CB_RECEIVED_ACTION with no message extra");
            return;
        }

        final CellBroadcastMessage cbm = new CellBroadcastMessage(message);
        int subId = intent.getExtras().getInt(PhoneConstants.SUBSCRIPTION_KEY);
        if (SubscriptionManager.isValidSubscriptionId(subId)) {
            cbm.setSubId(subId);
        } else {
            Log.e(TAG, "Invalid subscription id");
        }

        if (!isMessageEnabledByUser(cbm)) {
            Log.d(TAG, "ignoring alert of type " + cbm.getServiceCategory() +
                    " by user preference");
            return;
        }
        if (getResources().getBoolean(R.bool.config_regional_disable_cb_message))
            return;

        // If this is an ETWS message, then we want to include the body message to be a factor for
        // duplication detection. We found that some Japanese carriers send ETWS messages
        // with the same serial number, therefore the subsequent messages were all ignored.
        // In the other hand, US carriers have the requirement that only serial number, location,
        // and category should be used for duplicate detection.
        int hashCode = message.isEtwsMessage() ? message.getMessageBody().hashCode() : 0;

        if (mDuplicateCheckDatabase && isDuplicated(message)) {
            return;
        }
        // If this is an ETWS message, we need to include primary/secondary message information to
        // be a factor for duplication detection as well. Per 3GPP TS 23.041 section 8.2,
        // duplicate message detection shall be performed independently for primary and secondary
        // notifications.
        boolean isEtwsPrimary = false;
        if (message.isEtwsMessage()) {
            SmsCbEtwsInfo etwsInfo = message.getEtwsWarningInfo();
            if (etwsInfo != null) {
                isEtwsPrimary = etwsInfo.isPrimary();
            } else {
                Log.w(TAG, "ETWS info is not available.");
            }
        }

        boolean carrierDisableDupDetection = false;
        CarrierConfigManager configManager =
                (CarrierConfigManager) getSystemService(Context.CARRIER_CONFIG_SERVICE);
        if (configManager != null) {
            PersistableBundle carrierConfig =
                configManager.getConfigForSubId(subId);
            if (carrierConfig != null) {
                carrierDisableDupDetection =
                    carrierConfig.getBoolean("carrier_disable_etws_cmas_dup_detection");
            }
        }
        // Check for duplicate message IDs according to CMAS carrier requirements. Message IDs
        // are stored in volatile memory. If the maximum of 1024 messages is reached, the
        // message ID of the oldest message is deleted from the list.
        MessageServiceCategoryAndScope newCmasId = new MessageServiceCategoryAndScope(
                message.getServiceCategory(), message.getSerialNumber(), message.getLocation(),
                hashCode, isEtwsPrimary);

        Log.d(TAG, "message ID = " + newCmasId);

        long nowTime = SystemClock.elapsedRealtime();
        if (mUseDupDetection && !carrierDisableDupDetection) {
            // Check if the identical message arrives again
            if (sMessagesMap.get(newCmasId) != null) {
                // And if the previous one has not expired yet, treat it as a duplicate message.
                long previousTime = sMessagesMap.get(newCmasId);
                long expirationTime = getDuplicateExpirationTime(subId);
                if (nowTime - previousTime < expirationTime) {
                    Log.d(TAG, "ignoring the duplicate alert " + newCmasId + ", nowTime=" + nowTime
                            + ", previous=" + previousTime + ", expiration=" + expirationTime);
                    return;
                }
                // otherwise, we don't treat it as a duplicate and will show the same message again.
                Log.d(TAG, "The same message shown up " + (nowTime - previousTime)
                        + " milliseconds ago. Not a duplicate.");
            } else if (sMessagesMap.size() >= MAX_MESSAGE_ID_SIZE){
                // If we reach the maximum, remove the first inserted message key.
                MessageServiceCategoryAndScope oldestCmasId = sMessagesMap.keySet().
                        iterator().next();
                Log.d(TAG, "message ID limit reached, removing oldest message ID " + oldestCmasId);
                sMessagesMap.remove(oldestCmasId);
            } else {
                Log.d(TAG, "New message. Not a duplicate. Map size = " + sMessagesMap.size());
            }
        }

        sMessagesMap.put(newCmasId, nowTime);

        final Intent alertIntent = new Intent(SHOW_NEW_ALERT_ACTION);
        alertIntent.setClass(this, CellBroadcastAlertService.class);
        alertIntent.putExtra("message", cbm);

        // write to database on a background thread
        new CellBroadcastContentProvider.AsyncCellBroadcastTask(getContentResolver())
                .execute(new CellBroadcastContentProvider.CellBroadcastOperation() {
                    @Override
                    public boolean execute(CellBroadcastContentProvider provider) {
                        if (provider.insertNewBroadcast(cbm)) {
                            // new message, show the alert or notification on UI thread
                            startService(alertIntent);
                            return true;
                        } else {
                            return false;
                        }
                    }
                });
    }

    private void showNewAlert(Intent intent) {
        Bundle extras = intent.getExtras();
        if (extras == null) {
            Log.e(TAG, "received SHOW_NEW_ALERT_ACTION with no extras!");
            return;
        }

        CellBroadcastMessage cbm = (CellBroadcastMessage) intent.getParcelableExtra("message");

        if (cbm == null) {
            Log.e(TAG, "received SHOW_NEW_ALERT_ACTION with no message extra");
            return;
        }

        if (isEmergencyMessage(this, cbm)) {
            // start alert sound / vibration / TTS and display full-screen alert
            openEmergencyAlertNotification(cbm);
            if (!getResources().getBoolean(
                    R.bool.config_regional_stop_alert_on_duration)) {
                // add notification to the bar by passing the list of unread non-emergency
                // CellBroadcastMessages
                ArrayList<CellBroadcastMessage> messageList = CellBroadcastReceiverApp
                        .addNewMessageToList(cbm);
                addToNotificationBar(cbm, messageList, this, false);
            }
        } else {
            // add notification to the bar by passing the list of unread non-emergency
            // CellBroadcastMessages
            ArrayList<CellBroadcastMessage> messageList = CellBroadcastReceiverApp
                    .addNewMessageToList(cbm);
            addToNotificationBar(cbm, messageList, this, false);
        }
    }

    /**
     * Send broadcast twice, once for apps that have PRIVILEGED permission and
     * once for those that have the runtime one.
     * @param message the message to broadcast
     */
    private void broadcastAreaInfoReceivedAction(CellBroadcastMessage message) {
        Intent intent = new Intent(CB_AREA_INFO_RECEIVED_ACTION);

        intent.putExtra("message", message);
        sendBroadcastAsUser(intent, UserHandle.ALL,
                android.Manifest.permission.READ_PRIVILEGED_PHONE_STATE);
    }

    /**
     * Get preference setting for channel 60
     * @param message the message to check
     * @return true if channel 60 preference is set; false otherwise
     */
    private boolean getChannel60Preference(CellBroadcastMessage message) {
        String country = TelephonyManager.getDefault().
                getSimCountryIso(message.getSubId());

        boolean enable60Channel = SubscriptionManager.
                getResourcesForSubId(getApplicationContext(), message.
                        getSubId()).getBoolean(R.bool.show_india_settings) ||
                COUNTRY_INDIA.equals(country);

        return PreferenceManager.getDefaultSharedPreferences(this).
                getBoolean(CellBroadcastSettings.
                        KEY_ENABLE_CHANNEL_60_ALERTS, enable60Channel);
    }

    /**
     * Filter out broadcasts on the test channels that the user has not enabled,
     * and types of notifications that the user is not interested in receiving.
     * This allows us to enable an entire range of message identifiers in the
     * radio and not have to explicitly disable the message identifiers for
     * test broadcasts. In the unlikely event that the default shared preference
     * values were not initialized in CellBroadcastReceiverApp, the second parameter
     * to the getBoolean() calls match the default values in res/xml/preferences.xml.
     *
     * @param message the message to check
     * @return true if the user has enabled this message type; false otherwise
     */
    private boolean isMessageEnabledByUser(CellBroadcastMessage message) {

        // Check if all emergency alerts are disabled.
        boolean emergencyAlertEnabled = PreferenceManager.getDefaultSharedPreferences(this).
                getBoolean(CellBroadcastSettings.KEY_ENABLE_EMERGENCY_ALERTS, true);

        // Check if ETWS/CMAS test message is forced to disabled on the device.
        boolean forceDisableEtwsCmasTest =
                CellBroadcastSettings.isFeatureEnabled(this,
                        CarrierConfigManager.KEY_CARRIER_FORCE_DISABLE_ETWS_CMAS_TEST_BOOL, false);

        if (message.isEtwsTestMessage()) {
            return emergencyAlertEnabled &&
                    !forceDisableEtwsCmasTest &&
                    PreferenceManager.getDefaultSharedPreferences(this)
                    .getBoolean(CellBroadcastSettings.KEY_ENABLE_ETWS_TEST_ALERTS, false);
        }

        if (message.isEtwsMessage()) {
            // ETWS messages.
            // Turn on/off emergency notifications is the only way to turn on/off ETWS messages.
            return emergencyAlertEnabled;

        }

        if (message.isCmasMessage()) {
            switch (message.getCmasMessageClass()) {
                case SmsCbCmasInfo.CMAS_CLASS_EXTREME_THREAT:
                    return emergencyAlertEnabled &&
                            PreferenceManager.getDefaultSharedPreferences(this).getBoolean(
                            CellBroadcastSettings.KEY_ENABLE_CMAS_EXTREME_THREAT_ALERTS, true);

                case SmsCbCmasInfo.CMAS_CLASS_SEVERE_THREAT:
                    return emergencyAlertEnabled &&
                            PreferenceManager.getDefaultSharedPreferences(this).getBoolean(
                            CellBroadcastSettings.KEY_ENABLE_CMAS_SEVERE_THREAT_ALERTS, true);

                case SmsCbCmasInfo.CMAS_CLASS_CHILD_ABDUCTION_EMERGENCY:
                    return emergencyAlertEnabled &&
                            PreferenceManager.getDefaultSharedPreferences(this)
                            .getBoolean(CellBroadcastSettings.KEY_ENABLE_CMAS_AMBER_ALERTS, true);

                case SmsCbCmasInfo.CMAS_CLASS_REQUIRED_MONTHLY_TEST:
                case SmsCbCmasInfo.CMAS_CLASS_CMAS_EXERCISE:
                case SmsCbCmasInfo.CMAS_CLASS_OPERATOR_DEFINED_USE:
                    return emergencyAlertEnabled &&
                            !forceDisableEtwsCmasTest &&
                            PreferenceManager.getDefaultSharedPreferences(this)
                                    .getBoolean(CellBroadcastSettings.KEY_ENABLE_CMAS_TEST_ALERTS,
                                            false);
                default:
                    return true;    // presidential-level CMAS alerts are always enabled
            }
        }

        int serviceCategory = message.getServiceCategory();
        if (serviceCategory == CB_CHANNEL_50) {
            String country = TelephonyManager.getDefault().
                    getSimCountryIso(message.getSubId());
            // save latest area info broadcast for Settings display and send as
            // broadcast
            CellBroadcastReceiverApp.setLatestAreaInfo(message);
            broadcastAreaInfoReceivedAction(message);
            return !(COUNTRY_BRAZIL.equals(country) ||
                    COUNTRY_INDIA.equals(country));
        } else if (serviceCategory == CB_CHANNEL_60) {
            broadcastAreaInfoReceivedAction(message);
            return getChannel60Preference(message);
        }

        return true;    // other broadcast messages are always enabled
    }

    /**
     * Display a full-screen alert message for emergency alerts.
     * @param message the alert to display
     */
    private void openEmergencyAlertNotification(CellBroadcastMessage message) {
        // Acquire a CPU wake lock until the alert dialog and audio start playing.
        CellBroadcastAlertWakeLock.acquireScreenCpuWakeLock(this);

        // Close dialogs and window shade
        Intent closeDialogs = new Intent(Intent.ACTION_CLOSE_SYSTEM_DIALOGS);
        sendBroadcast(closeDialogs);

        // start audio/vibration/speech service for emergency alerts
        Intent audioIntent = new Intent(this, CellBroadcastAlertAudio.class);
        audioIntent.setAction(CellBroadcastAlertAudio.ACTION_START_ALERT_AUDIO);
        SharedPreferences prefs = PreferenceManager.getDefaultSharedPreferences(this);

        ToneType toneType = ToneType.CMAS_DEFAULT;
        if (!getResources().getBoolean(
                R.bool.config_regional_presidential_wea_with_tone_vibrate)
                && message.isEtwsMessage()) {
            // For ETWS, always vibrate, even in silent mode.
            audioIntent.putExtra(CellBroadcastAlertAudio.ALERT_AUDIO_VIBRATE_EXTRA, true);
            audioIntent.putExtra(CellBroadcastAlertAudio.ALERT_AUDIO_ETWS_VIBRATE_EXTRA, true);

            toneType = ToneType.ETWS_DEFAULT;

            if (message.getEtwsWarningInfo() != null) {
                int warningType = message.getEtwsWarningInfo().getWarningType();

                switch (warningType) {
                    case SmsCbEtwsInfo.ETWS_WARNING_TYPE_EARTHQUAKE:
                    case SmsCbEtwsInfo.ETWS_WARNING_TYPE_EARTHQUAKE_AND_TSUNAMI:
                        toneType = ToneType.EARTHQUAKE;
                        break;
                    case SmsCbEtwsInfo.ETWS_WARNING_TYPE_TSUNAMI:
                        toneType = ToneType.TSUNAMI;
                        break;
                    case SmsCbEtwsInfo.ETWS_WARNING_TYPE_OTHER_EMERGENCY:
                        toneType = ToneType.OTHER;
                        break;
                }
            }
        } else if ((getResources().getBoolean(
            R.bool.config_regional_presidential_wea_with_tone_vibrate))
            && (message.isCmasMessage())
            && (message.getCmasMessageClass()
                == SmsCbCmasInfo.CMAS_CLASS_PRESIDENTIAL_LEVEL_ALERT)){
            audioIntent.putExtra(CellBroadcastAlertAudio.ALERT_AUDIO_VIBRATE_EXTRA, true);
            audioIntent.putExtra(CellBroadcastAlertAudio.ALERT_AUDIO_TONE_EXTRA, true);
            audioIntent.putExtra(
                    CellBroadcastAlertAudio.ALERT_AUDIO_PRESIDENT_TONE_VIBRATE_EXTRA, true);
        } else {
            // For other alerts, vibration can be disabled in app settings.
            audioIntent.putExtra(CellBroadcastAlertAudio.ALERT_AUDIO_VIBRATE_EXTRA,
                    prefs.getBoolean(CellBroadcastSettings.KEY_ENABLE_ALERT_VIBRATE, true));
            int channel = message.getServiceCategory();
            ArrayList<CellBroadcastChannelRange> ranges= CellBroadcastOtherChannelsManager.
                    getInstance().getCellBroadcastChannelRanges(getApplicationContext(),
                    message.getSubId());
            if (ranges != null) {
                for (CellBroadcastChannelRange range : ranges) {
                    if (channel >= range.mStartId && channel <= range.mEndId) {
                        toneType = range.mToneType;
                        break;
                    }
                }
            }
        }
        audioIntent.putExtra(CellBroadcastAlertAudio.ALERT_AUDIO_TONE_TYPE, toneType);

        if (getResources().getBoolean(
                    R.bool.config_regional_wea_alert_tone_enable)) {
            audioIntent.putExtra(CellBroadcastAlertAudio.ALERT_AUDIO_TONE_EXTRA,
                    prefs.getBoolean(CellBroadcastSettings.KEY_ENABLE_ALERT_TONE, true));
        }

        String messageBody = message.getMessageBody();

        if (prefs.getBoolean(CellBroadcastSettings.KEY_ENABLE_ALERT_SPEECH, true)) {
            audioIntent.putExtra(CellBroadcastAlertAudio.ALERT_AUDIO_MESSAGE_BODY, messageBody);

            String preferredLanguage = message.getLanguageCode();
            String defaultLanguage = null;
            if (message.isEtwsMessage()) {
                // Only do TTS for ETWS secondary message.
                // There is no text in ETWS primary message. When we construct the ETWS primary
                // message, we hardcode "ETWS" as the body hence we don't want to speak that out
                // here.

                // Also in many cases we see the secondary message comes few milliseconds after
                // the primary one. If we play TTS for the primary one, It will be overwritten by
                // the secondary one immediately anyway.
                if (!message.getEtwsWarningInfo().isPrimary()) {
                    // Since only Japanese carriers are using ETWS, if there is no language
                    // specified in the ETWS message, we'll use Japanese as the default language.
                    defaultLanguage = "ja";
                }
            } else {
                // If there is no language specified in the CMAS message, use device's
                // default language.
                defaultLanguage = Locale.getDefault().getLanguage();
            }

            Log.d(TAG, "Preferred language = " + preferredLanguage +
                    ", Default language = " + defaultLanguage);
            audioIntent.putExtra(CellBroadcastAlertAudio.ALERT_AUDIO_MESSAGE_PREFERRED_LANGUAGE,
                    preferredLanguage);
            audioIntent.putExtra(CellBroadcastAlertAudio.ALERT_AUDIO_MESSAGE_DEFAULT_LANGUAGE,
                    defaultLanguage);
        }
        startService(audioIntent);

        ArrayList<CellBroadcastMessage> messageList = new ArrayList<CellBroadcastMessage>(1);
        messageList.add(message);

        Intent alertDialogIntent = createDisplayMessageIntent(this, CellBroadcastAlertDialog.class,
                messageList);
        alertDialogIntent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        startActivity(alertDialogIntent);
    }

    /**
     * Add the new alert to the notification bar (non-emergency alerts), or launch a
     * high-priority immediate intent for emergency alerts.
     * @param message the alert to display
     */
    static void addToNotificationBar(CellBroadcastMessage message,
                                     ArrayList<CellBroadcastMessage> messageList, Context context,
                                     boolean fromSaveState) {
        int channelTitleId = CellBroadcastResources.getDialogTitleResource(context, message);
        CharSequence channelName = context.getText(channelTitleId);
        String messageBody = message.getMessageBody();

        // Create intent to show the new messages when user selects the notification.
        Intent intent = createDisplayMessageIntent(context, CellBroadcastAlertDialog.class,
                messageList);

        intent.putExtra(CellBroadcastAlertDialog.FROM_NOTIFICATION_EXTRA, true);
        intent.putExtra(CellBroadcastAlertDialog.FROM_SAVE_STATE_NOTIFICATION_EXTRA, fromSaveState);

        PendingIntent pi = PendingIntent.getActivity(context, NOTIFICATION_ID, intent,
                PendingIntent.FLAG_ONE_SHOT | PendingIntent.FLAG_UPDATE_CURRENT);

        // use default sound/vibration/lights for non-emergency broadcasts
        Notification.Builder builder = new Notification.Builder(context)
                .setSmallIcon(R.drawable.ic_notify_alert)
                .setTicker(channelName)
                .setWhen(System.currentTimeMillis())
                .setContentIntent(pi)
                .setCategory(Notification.CATEGORY_SYSTEM)
                .setPriority(Notification.PRIORITY_HIGH)
                .setColor(context.getResources().getColor(R.color.notification_color))
                .setVisibility(Notification.VISIBILITY_PUBLIC)
                .setDefaults(Notification.DEFAULT_ALL);

        builder.setDefaults(Notification.DEFAULT_ALL);

        // increment unread alert count (decremented when user dismisses alert dialog)
        int unreadCount = messageList.size();
        if (unreadCount > 1) {
            // use generic count of unread broadcasts if more than one unread
            builder.setContentTitle(context.getString(R.string.notification_multiple_title));
            builder.setContentText(context.getString(R.string.notification_multiple, unreadCount));
        } else {
            builder.setContentTitle(channelName).setContentText(messageBody);
        }

        NotificationManager notificationManager = NotificationManager.from(context);

        notificationManager.notify(NOTIFICATION_ID, builder.build());
    }

    static Intent createDisplayMessageIntent(Context context, Class intentClass,
            ArrayList<CellBroadcastMessage> messageList) {
        // Trigger the list activity to fire up a dialog that shows the received messages
        Intent intent = new Intent(context, intentClass);
        intent.putParcelableArrayListExtra(CellBroadcastMessage.SMS_CB_MESSAGE_EXTRA, messageList);
        return intent;
    }

    @VisibleForTesting
    @Override
    public IBinder onBind(Intent intent) {
        return new LocalBinder();
    }

    @VisibleForTesting
    class LocalBinder extends Binder {
        public CellBroadcastAlertService getService() {
            return CellBroadcastAlertService.this;
        }
    }

    /**
     * Check if the cell broadcast message is an emergency message or not
     * @param context Device context
     * @param cbm Cell broadcast message
     * @return True if the message is an emergency message, otherwise false.
     */
    public static boolean isEmergencyMessage(Context context, CellBroadcastMessage cbm) {
        boolean isEmergency = false;

        if (cbm == null) {
            return false;
        }

        int id = cbm.getServiceCategory();
        int subId = cbm.getSubId();

        if (cbm.isEmergencyAlertMessage()) {
            isEmergency = true;
        } else {
            ArrayList<CellBroadcastChannelRange> ranges = CellBroadcastOtherChannelsManager.
                    getInstance().getCellBroadcastChannelRanges(context, subId);

            if (ranges != null) {
                for (CellBroadcastChannelRange range : ranges) {
                    if (range.mStartId <= id && range.mEndId >= id) {
                        isEmergency = range.mIsEmergency;
                        break;
                    }
                }
            }
        }

        Log.d(TAG, "isEmergencyMessage: " + isEmergency + ", subId = " + subId + ", " +
                "message id = " + id);
        return isEmergency;
    }
}
