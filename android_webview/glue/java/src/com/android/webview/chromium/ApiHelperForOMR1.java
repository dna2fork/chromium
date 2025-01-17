// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package com.android.webview.chromium;

import android.annotation.TargetApi;
import android.os.Build;
import android.webkit.WebView;
import android.webkit.WebViewClient;

import org.chromium.android_webview.AwContentsClient;
import org.chromium.android_webview.AwSafeBrowsingResponse;
import org.chromium.base.Callback;

/**
 * Utility class to use new APIs that were added in OMR1 (API level 27). These need to exist in a
 * separate class so that Android framework can successfully verify WebView classes without
 * encountering the new APIs.
 */
@TargetApi(Build.VERSION_CODES.O_MR1)
public class ApiHelperForOMR1 {
    private ApiHelperForOMR1() {}

    /**
     * See {@link WebViewClient#onSafeBrowsingHit(WebView, WebResourceRequest, int,
     * SafeBrowsingResponse)}, which was added in OMR1.
     */
    public static void onSafeBrowsingHit(WebViewClient webViewClient, WebView webView,
            AwContentsClient.AwWebResourceRequest request, int threatType,
            final Callback<AwSafeBrowsingResponse> callback) {
        webViewClient.onSafeBrowsingHit(webView, new WebResourceRequestAdapter(request), threatType,
                new SafeBrowsingResponseAdapter(callback));
    }
}
