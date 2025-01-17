// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.preferences.website;

import android.util.Pair;

import org.chromium.base.Callback;
import org.chromium.chrome.browser.ContentSettingsType;

import java.util.ArrayList;
import java.util.Collection;
import java.util.HashMap;
import java.util.LinkedList;
import java.util.Map;

/**
 * Utility class that asynchronously fetches any Websites and the permissions
 * that the user has set for them.
 */
public class WebsitePermissionsFetcher {
    /**
     * A callback to pass to WebsitePermissionsFetcher. This is run when the
     * website permissions have been fetched.
     */
    public interface WebsitePermissionsCallback {
        void onWebsitePermissionsAvailable(Collection<Website> sites);
    }

    /**
     * A specialization of Pair to hold an (origin, embedder) tuple. This overrides
     * android.util.Pair#hashCode, which simply XORs the hashCodes of the pair of values together.
     * Having origin == embedder (a fix for a crash in crbug.com/636330) results in pathological
     * performance and causes Site Settings/All Sites to lag significantly on opening. See
     * crbug.com/732907.
     */
    public static class OriginAndEmbedder extends Pair<WebsiteAddress, WebsiteAddress> {
        public OriginAndEmbedder(WebsiteAddress origin, WebsiteAddress embedder) {
            super(origin, embedder);
        }

        public static OriginAndEmbedder create(WebsiteAddress origin, WebsiteAddress embedder) {
            return new OriginAndEmbedder(origin, embedder);
        }

        private static boolean isEqual(Object o1, Object o2) {
            // Returns true iff o1 == o2, handling nulls.
            return (o1 == o2) || (o1 != null && o1.equals(o2));
        }

        @Override
        public boolean equals(Object o) {
            // Prior to KitKat, android.util.Pair would crash with a NullPointerException in this
            // method. This override specialises the post-Kitkat implementation to this class, and
            // correctly handles nulls.
            if (!(o instanceof OriginAndEmbedder)) return false;

            OriginAndEmbedder p = (OriginAndEmbedder) o;
            return isEqual(p.first, first) && isEqual(p.second, second);
        }

        @Override
        public int hashCode() {
            // This is the calculation used by Arrays#hashCode().
            int result = 31 + (first == null ? 0 : first.hashCode());
            return 31 * result + (second == null ? 0 : second.hashCode());
        }
    }

    // This map looks up Websites by their origin and embedder.
    private final Map<OriginAndEmbedder, Website> mSites = new HashMap<>();

    // The callback to run when the permissions have been fetched.
    private final WebsitePermissionsCallback mCallback;

    private final boolean mFetchSiteImportantInfo;

    /**
     * @param callback The callback to run when the fetch is complete.
     */
    public WebsitePermissionsFetcher(WebsitePermissionsCallback callback) {
        this(callback, false);
    }

    /**
     * @param callback The callback to run when the fetch is complete.
     * @param fetchSiteImportantInfo if the fetcher should query whether each site is 'important'.
     */
    public WebsitePermissionsFetcher(
            WebsitePermissionsCallback callback, boolean fetchSiteImportantInfo) {
        mCallback = callback;
        mFetchSiteImportantInfo = fetchSiteImportantInfo;
    }

    /**
     * Fetches preferences for all sites that have them.
     * TODO(mvanouwerkerk): Add an argument |url| to only fetch permissions for
     * sites from the same origin as that of |url| - https://crbug.com/459222.
     */
    public void fetchAllPreferences() {
        TaskQueue queue = new TaskQueue();
        // Populate features from more specific to less specific.
        // Geolocation lookup permission is per-origin and per-embedder.
        queue.add(new PermissionInfoFetcher(PermissionInfo.Type.GEOLOCATION));
        // Midi sysex access permission is per-origin and per-embedder.
        queue.add(new PermissionInfoFetcher(PermissionInfo.Type.MIDI));
        // Cookies are stored per-host.
        queue.add(new CookieExceptionInfoFetcher());
        // Local storage info is per-origin.
        queue.add(new LocalStorageInfoFetcher());
        // Website storage is per-host.
        queue.add(new WebStorageInfoFetcher());
        // Popup exceptions are host-based patterns (unless we start
        // synchronizing popup exceptions with desktop Chrome).
        queue.add(new PopupExceptionInfoFetcher());
        // Ads exceptions are host-based.
        queue.add(new AdsExceptionInfoFetcher());
        // JavaScript exceptions are host-based patterns.
        queue.add(new JavaScriptExceptionInfoFetcher());
        // Sound exceptions are host-based patterns.
        queue.add(new SoundExceptionInfoFetcher());
        // Protected media identifier permission is per-origin and per-embedder.
        queue.add(new PermissionInfoFetcher(PermissionInfo.Type.PROTECTED_MEDIA_IDENTIFIER));
        // Notification permission is per-origin.
        queue.add(new PermissionInfoFetcher(PermissionInfo.Type.NOTIFICATION));
        // Camera capture permission is per-origin and per-embedder.
        queue.add(new PermissionInfoFetcher(PermissionInfo.Type.CAMERA));
        // Micropohone capture permission is per-origin and per-embedder.
        queue.add(new PermissionInfoFetcher(PermissionInfo.Type.MICROPHONE));
        // Background sync permission is per-origin.
        queue.add(new BackgroundSyncExceptionInfoFetcher());
        // Autoplay permission is per-origin.
        queue.add(new AutoplayExceptionInfoFetcher());
        // USB device permission is per-origin and per-embedder.
        queue.add(new UsbInfoFetcher());
        // Clipboard info is per-origin.
        queue.add(new PermissionInfoFetcher(PermissionInfo.Type.CLIPBOARD));
        // Sensors permission is per-origin.
        queue.add(new PermissionInfoFetcher(PermissionInfo.Type.SENSORS));

        queue.add(new PermissionsAvailableCallbackRunner());

        queue.next();
    }

    /**
     * Fetches all preferences within a specific category.
     *
     * @param catgory A category to fetch.
     */
    public void fetchPreferencesForCategory(SiteSettingsCategory category) {
        if (category.showAllSites()) {
            fetchAllPreferences();
            return;
        }

        TaskQueue queue = new TaskQueue();
        // Populate features from more specific to less specific.
        if (category.showGeolocationSites()) {
            // Geolocation lookup permission is per-origin and per-embedder.
            queue.add(new PermissionInfoFetcher(PermissionInfo.Type.GEOLOCATION));
        } else if (category.showCookiesSites()) {
            // Cookies exceptions are patterns.
            queue.add(new CookieExceptionInfoFetcher());
        } else if (category.showStorageSites()) {
            // Local storage info is per-origin.
            queue.add(new LocalStorageInfoFetcher());
            // Website storage is per-host.
            queue.add(new WebStorageInfoFetcher());
        } else if (category.showCameraSites()) {
            // Camera capture permission is per-origin and per-embedder.
            queue.add(new PermissionInfoFetcher(PermissionInfo.Type.CAMERA));
        } else if (category.showMicrophoneSites()) {
            // Micropohone capture permission is per-origin and per-embedder.
            queue.add(new PermissionInfoFetcher(PermissionInfo.Type.MICROPHONE));
        } else if (category.showPopupSites()) {
            // Popup exceptions are host-based patterns (unless we start
            // synchronizing popup exceptions with desktop Chrome.)
            queue.add(new PopupExceptionInfoFetcher());
        } else if (category.showAdsSites()) {
            // Ads exceptions are host-based.
            queue.add(new AdsExceptionInfoFetcher());
        } else if (category.showJavaScriptSites()) {
            // JavaScript exceptions are host-based patterns.
            queue.add(new JavaScriptExceptionInfoFetcher());
        } else if (category.showSoundSites()) {
            // Sound exceptions are host-based patterns.
            queue.add(new SoundExceptionInfoFetcher());
        } else if (category.showNotificationsSites()) {
            // Push notification permission is per-origin.
            queue.add(new PermissionInfoFetcher(PermissionInfo.Type.NOTIFICATION));
        } else if (category.showBackgroundSyncSites()) {
            // Background sync info is per-origin.
            queue.add(new BackgroundSyncExceptionInfoFetcher());
        } else if (category.showProtectedMediaSites()) {
            // Protected media identifier permission is per-origin and per-embedder.
            queue.add(new PermissionInfoFetcher(PermissionInfo.Type.PROTECTED_MEDIA_IDENTIFIER));
        } else if (category.showAutoplaySites()) {
            // Autoplay permission is per-origin.
            queue.add(new AutoplayExceptionInfoFetcher());
        } else if (category.showUsbDevices()) {
            // USB device permission is per-origin.
            queue.add(new UsbInfoFetcher());
        } else if (category.showClipboardSites()) {
            // Clipboard permission is per-origin.
            queue.add(new PermissionInfoFetcher(PermissionInfo.Type.CLIPBOARD));
        } else if (category.showSensorsSites()) {
            // Sensors permission is per-origin.
            queue.add(new PermissionInfoFetcher(PermissionInfo.Type.SENSORS));
        }
        queue.add(new PermissionsAvailableCallbackRunner());
        queue.next();
    }

    private Website findOrCreateSite(WebsiteAddress origin, WebsiteAddress embedder) {
        OriginAndEmbedder key = OriginAndEmbedder.create(origin, embedder);
        Website site = mSites.get(key);
        if (site == null) {
            site = new Website(origin, embedder);
            mSites.put(key, site);
        }
        return site;
    }

    private void setException(int contentSettingsType) {
        for (ContentSettingException exception :
                WebsitePreferenceBridge.getContentSettingsExceptions(contentSettingsType)) {
            // The pattern "*" represents the default setting, not a specific website.
            if (exception.getPattern().equals("*")) continue;
            WebsiteAddress address = WebsiteAddress.create(exception.getPattern());
            if (address == null) continue;
            Website site = findOrCreateSite(address, null);
            switch (contentSettingsType) {
                case ContentSettingsType.CONTENT_SETTINGS_TYPE_ADS:
                    site.setAdsException(exception);
                    break;
                case ContentSettingsType.CONTENT_SETTINGS_TYPE_AUTOPLAY:
                    site.setAutoplayException(exception);
                    break;
                case ContentSettingsType.CONTENT_SETTINGS_TYPE_BACKGROUND_SYNC:
                    site.setBackgroundSyncException(exception);
                    break;
                case ContentSettingsType.CONTENT_SETTINGS_TYPE_COOKIES:
                    site.setCookieException(exception);
                    break;
                case ContentSettingsType.CONTENT_SETTINGS_TYPE_JAVASCRIPT:
                    site.setJavaScriptException(exception);
                    break;
                case ContentSettingsType.CONTENT_SETTINGS_TYPE_POPUPS:
                    site.setPopupException(exception);
                    break;
                case ContentSettingsType.CONTENT_SETTINGS_TYPE_SOUND:
                    site.setSoundException(exception);
                    break;
                default:
                    assert false : "Unexpected content setting type received: "
                                   + contentSettingsType;
                    break;
            }
        }
    }

    /**
     * A single task in the WebsitePermissionsFetcher task queue. We need fetching of features to be
     * serialized, as we need to have all the origins in place prior to populating the hosts.
     */
    private abstract class Task {
        /** Override this method to implement a synchronous task. */
        void run() {}

        /**
         * Override this method to implement an asynchronous task. Call queue.next() once execution
         * is complete.
         */
        void runAsync(TaskQueue queue) {
            run();
            queue.next();
        }
    }

    /**
     * A queue used to store the sequence of tasks to run to fetch the website preferences. Each
     * task is run sequentially, and some of the tasks may run asynchronously.
     */
    private static class TaskQueue extends LinkedList<Task> {
        void next() {
            if (!isEmpty()) removeFirst().runAsync(this);
        }
    }

    private class AutoplayExceptionInfoFetcher extends Task {
        @Override
        public void run() {
            setException(ContentSettingsType.CONTENT_SETTINGS_TYPE_AUTOPLAY);
        }
    }

    private class PermissionInfoFetcher extends Task {
        final @PermissionInfo.Type int mType;

        public PermissionInfoFetcher(@PermissionInfo.Type int type) {
            mType = type;
        }

        @Override
        public void run() {
            for (PermissionInfo info : WebsitePreferenceBridge.getPermissionInfo(mType)) {
                WebsiteAddress origin = WebsiteAddress.create(info.getOrigin());
                if (origin == null) continue;
                WebsiteAddress embedder = mType == PermissionInfo.Type.SENSORS
                        ? null
                        : WebsiteAddress.create(info.getEmbedder());
                findOrCreateSite(origin, embedder).setPermissionInfo(info);
            }
        }
    }

    private class PopupExceptionInfoFetcher extends Task {
        @Override
        public void run() {
            setException(ContentSettingsType.CONTENT_SETTINGS_TYPE_POPUPS);
        }
    }

    private class AdsExceptionInfoFetcher extends Task {
        @Override
        public void run() {
            setException(ContentSettingsType.CONTENT_SETTINGS_TYPE_ADS);
        }
    }

    private class JavaScriptExceptionInfoFetcher extends Task {
        @Override
        public void run() {
            setException(ContentSettingsType.CONTENT_SETTINGS_TYPE_JAVASCRIPT);
        }
    }

    private class SoundExceptionInfoFetcher extends Task {
        @Override
        public void run() {
            setException(ContentSettingsType.CONTENT_SETTINGS_TYPE_SOUND);
        }
    }

    private class CookieExceptionInfoFetcher extends Task {
        @Override
        public void run() {
            setException(ContentSettingsType.CONTENT_SETTINGS_TYPE_COOKIES);
        }
    }

    private class LocalStorageInfoFetcher extends Task {
        @Override
        public void runAsync(final TaskQueue queue) {
            WebsitePreferenceBridge.fetchLocalStorageInfo(new Callback<HashMap>() {
                @Override
                public void onResult(HashMap result) {
                    for (Object o : result.entrySet()) {
                        @SuppressWarnings("unchecked")
                        Map.Entry<String, LocalStorageInfo> entry =
                                (Map.Entry<String, LocalStorageInfo>) o;
                        WebsiteAddress address = WebsiteAddress.create(entry.getKey());
                        if (address == null) continue;
                        findOrCreateSite(address, null).setLocalStorageInfo(entry.getValue());
                    }
                    queue.next();
                }
            }, mFetchSiteImportantInfo);
        }
    }

    private class WebStorageInfoFetcher extends Task {
        @Override
        public void runAsync(final TaskQueue queue) {
            WebsitePreferenceBridge.fetchStorageInfo(new Callback<ArrayList>() {
                @Override
                public void onResult(ArrayList result) {
                    @SuppressWarnings("unchecked")
                    ArrayList<StorageInfo> infoArray = result;

                    for (StorageInfo info : infoArray) {
                        WebsiteAddress address = WebsiteAddress.create(info.getHost());
                        if (address == null) continue;
                        findOrCreateSite(address, null).addStorageInfo(info);
                    }
                    queue.next();
                }
            });
        }
    }

    private class BackgroundSyncExceptionInfoFetcher extends Task {
        @Override
        public void run() {
            setException(ContentSettingsType.CONTENT_SETTINGS_TYPE_BACKGROUND_SYNC);
        }
    }

    private class UsbInfoFetcher extends Task {
        @Override
        public void run() {
            for (ChosenObjectInfo info : WebsitePreferenceBridge.getChosenObjectInfo(
                         ContentSettingsType.CONTENT_SETTINGS_TYPE_USB_CHOOSER_DATA)) {
                WebsiteAddress origin = WebsiteAddress.create(info.getOrigin());
                if (origin == null) continue;
                WebsiteAddress embedder = WebsiteAddress.create(info.getEmbedder());
                findOrCreateSite(origin, embedder).addChosenObjectInfo(info);
            }
        }
    }

    private class PermissionsAvailableCallbackRunner extends Task {
        @Override
        public void run() {
            mCallback.onWebsitePermissionsAvailable(mSites.values());
        }
    }
}
