// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.content.browser;

import android.content.Context;
import android.content.res.Configuration;

import org.chromium.base.TraceEvent;
import org.chromium.content.browser.webcontents.WebContentsImpl;
import org.chromium.content_public.browser.ViewEventSink;
import org.chromium.content_public.browser.WebContents;
import org.chromium.content_public.browser.WebContents.UserDataFactory;
import org.chromium.ui.base.ViewAndroidDelegate;
import org.chromium.ui.base.WindowAndroid.ActivityStateObserver;

/**
 * Implementation of the interface {@link ViewEventSink}.
 */
public final class ViewEventSinkImpl implements ViewEventSink, ActivityStateObserver {
    private final WebContentsImpl mWebContents;
    private Context mContext;

    // Whether the container view has view-level focus.
    private Boolean mHasViewFocus;

    // This is used in place of window focus on the container view, as we can't actually use window
    // focus due to issues where content expects to be focused while a popup steals window focus.
    // See https://crbug.com/686232 for more context.
    private boolean mPaused;

    // Whether we consider this WebContents to have input focus. This is computed through
    // mHasViewFocus and mPaused. See the comments on mPaused for how this doesn't exactly match
    // Android's notion of input focus and why we need to do this.
    private Boolean mHasInputFocus;
    private boolean mHideKeyboardOnBlur;

    private static final class UserDataFactoryLazyHolder {
        private static final UserDataFactory<ViewEventSinkImpl> INSTANCE = ViewEventSinkImpl::new;
    }

    public static ViewEventSinkImpl create(Context context, WebContents webContents) {
        ViewEventSinkImpl manager = webContents.getOrSetUserData(
                ViewEventSinkImpl.class, UserDataFactoryLazyHolder.INSTANCE);
        assert manager != null;
        assert !manager.initialized();
        manager.init(context);
        return manager;
    }

    public static ViewEventSinkImpl from(WebContents webContents) {
        return webContents.getOrSetUserData(ViewEventSinkImpl.class, null);
    }

    public ViewEventSinkImpl(WebContents webContents) {
        mWebContents = (WebContentsImpl) webContents;
    }

    private void init(Context context) {
        mContext = context;
    }

    private boolean initialized() {
        return mContext != null;
    }

    public Context getContext() {
        return mContext;
    }

    @Override
    public void onAttachedToWindow() {
        WindowEventObserverManager.from(mWebContents).onAttachedToWindow();
    }

    @Override
    public void onDetachedFromWindow() {
        WindowEventObserverManager.from(mWebContents).onDetachedFromWindow();
    }

    @Override
    public void onWindowFocusChanged(boolean hasWindowFocus) {
        WindowEventObserverManager.from(mWebContents).onWindowFocusChanged(hasWindowFocus);
    }

    @Override
    public void onViewFocusChanged(boolean gainFocus) {
        if (mHasViewFocus != null && mHasViewFocus == gainFocus) return;
        mHasViewFocus = gainFocus;
        onFocusChanged();
    }

    @Override
    public void setHideKeyboardOnBlur(boolean hideKeyboardOnBlur) {
        mHideKeyboardOnBlur = hideKeyboardOnBlur;
    }

    @SuppressWarnings("javadoc")
    @Override
    public void onConfigurationChanged(Configuration newConfig) {
        try {
            TraceEvent.begin("ViewEventSink.onConfigurationChanged");
            WindowEventObserverManager.from(mWebContents).onConfigurationChanged(newConfig);
            // To request layout has side effect, but it seems OK as it only happen in
            // onConfigurationChange and layout has to be changed in most case.
            ViewAndroidDelegate delegate = mWebContents.getViewAndroidDelegate();
            if (delegate != null) delegate.getContainerView().requestLayout();
        } finally {
            TraceEvent.end("ViewEventSink.onConfigurationChanged");
        }
    }

    private void onFocusChanged() {
        // Wait for view focus to be set before propagating focus changes.
        if (mHasViewFocus == null) return;

        // See the comments on mPaused for why we use it to compute input focus.
        boolean hasInputFocus = mHasViewFocus && !mPaused;
        if (mHasInputFocus != null && mHasInputFocus == hasInputFocus) return;
        mHasInputFocus = hasInputFocus;

        if (mWebContents == null) {
            // CVC is on its way to destruction. The rest needs not running as all the states
            // will be discarded, or WebContentsUserData-based objects are not reachable
            // any more. Simply return here.
            return;
        }
        WindowEventObserverManager.from(mWebContents)
                .onViewFocusChanged(mHasInputFocus, mHideKeyboardOnBlur);
        mWebContents.setFocus(mHasInputFocus);
    }

    // ActivityStateObserver

    @Override
    public void onActivityPaused() {
        // When the activity pauses, the content should lose focus.
        // TODO(mthiesse): See https://crbug.com/686232 for context. Desktop platforms use keyboard
        // focus to trigger blur/focus, and the equivalent to this on Android is Window focus.
        // However, we don't use Window focus because of the complexity around popups stealing
        // Window focus.
        if (mPaused) return;
        mPaused = true;
        onFocusChanged();
    }

    @Override
    public void onActivityResumed() {
        // When the activity resumes, the View#onFocusChanged may not be called, so we should
        // restore the View focus state.
        if (!mPaused) return;
        mPaused = false;
        onFocusChanged();
    }

    @Override
    public void onPauseForTesting() {
        onActivityPaused();
    }

    @Override
    public void onResumeForTesting() {
        onActivityResumed();
    }
}
