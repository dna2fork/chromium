// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.autofill.keyboard_accessory;

import static org.hamcrest.CoreMatchers.is;
import static org.hamcrest.CoreMatchers.notNullValue;
import static org.hamcrest.CoreMatchers.nullValue;
import static org.junit.Assert.assertThat;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import android.support.v4.view.ViewPager;
import android.view.ViewStub;

import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.MockitoAnnotations;
import org.robolectric.annotation.Config;

import org.chromium.base.test.BaseRobolectricTestRunner;
import org.chromium.chrome.browser.autofill.keyboard_accessory.AccessorySheetModel.PropertyKey;
import org.chromium.chrome.browser.modelutil.ListObservable;
import org.chromium.chrome.browser.modelutil.PropertyObservable;

/**
 * Controller tests for the keyboard accessory bottom sheet component.
 */
@RunWith(BaseRobolectricTestRunner.class)
@Config(manifest = Config.NONE)
public class AccessorySheetControllerTest {
    @Mock
    private PropertyObservable.PropertyObserver<PropertyKey> mMockPropertyObserver;
    @Mock
    private ListObservable.ListObserver<Void> mTabListObserver;
    @Mock
    private ViewStub mMockViewStub;
    @Mock
    private ViewPager mMockView;
    @Mock
    private KeyboardAccessoryData.Tab mMockTab;
    @Mock
    private KeyboardAccessoryData.Tab mSecondMockTab;
    @Mock
    private KeyboardAccessoryData.Tab mThirdMockTab;
    @Mock
    private KeyboardAccessoryData.Tab mFourthMockTab;

    private AccessorySheetCoordinator mCoordinator;
    private AccessorySheetMediator mMediator;
    private AccessorySheetModel mModel;

    @Before
    public void setUp() {
        MockitoAnnotations.initMocks(this);
        when(mMockViewStub.inflate()).thenReturn(mMockView);
        mCoordinator = new AccessorySheetCoordinator(mMockViewStub);
        mMediator = mCoordinator.getMediatorForTesting();
        mModel = mMediator.getModelForTesting();
    }

    @Test
    public void testCreatesValidSubComponents() {
        assertThat(mCoordinator, is(notNullValue()));
        assertThat(mMediator, is(notNullValue()));
        assertThat(mModel, is(notNullValue()));
    }

    @Test
    public void testModelNotifiesAboutVisibilityOncePerChange() {
        mModel.addObserver(mMockPropertyObserver);

        // Calling show on the mediator should make model propagate that it's visible.
        mMediator.show();
        verify(mMockPropertyObserver).onPropertyChanged(mModel, PropertyKey.VISIBLE);
        assertThat(mModel.isVisible(), is(true));

        // Calling show again does nothing.
        mMediator.show();
        verify(mMockPropertyObserver) // Still the same call and no new one added.
                .onPropertyChanged(mModel, PropertyKey.VISIBLE);

        // Calling hide on the mediator should make model propagate that it's invisible.
        mMediator.hide();
        verify(mMockPropertyObserver, times(2)).onPropertyChanged(mModel, PropertyKey.VISIBLE);
        assertThat(mModel.isVisible(), is(false));
    }

    @Test
    public void testModelNotifiesChangesForNewSheet() {
        mModel.addObserver(mMockPropertyObserver);
        mModel.getTabList().addObserver(mTabListObserver);

        assertThat(mModel.getTabList().getItemCount(), is(0));
        mCoordinator.addTab(mMockTab);
        verify(mTabListObserver).onItemRangeInserted(mModel.getTabList(), 0, 1);
        assertThat(mModel.getTabList().getItemCount(), is(1));
    }

    @Test
    public void testFirstAddedTabBecomesActiveTab() {
        mModel.addObserver(mMockPropertyObserver);

        // Initially, there is no active Tab.
        assertThat(mModel.getTabList().getItemCount(), is(0));
        assertThat(mCoordinator.getTab(), is(nullValue()));

        // The first tab becomes the active Tab.
        mCoordinator.addTab(mMockTab);
        verify(mMockPropertyObserver).onPropertyChanged(mModel, PropertyKey.ACTIVE_TAB_INDEX);
        assertThat(mModel.getTabList().getItemCount(), is(1));
        assertThat(mModel.getActiveTabIndex(), is(0));
        assertThat(mCoordinator.getTab(), is(mMockTab));

        // A second tab is added but doesn't become automatically active.
        mCoordinator.addTab(mSecondMockTab);
        verify(mMockPropertyObserver).onPropertyChanged(mModel, PropertyKey.ACTIVE_TAB_INDEX);
        assertThat(mModel.getTabList().getItemCount(), is(2));
        assertThat(mModel.getActiveTabIndex(), is(0));
    }

    @Test
    public void testDeletingFirstTabActivatesNewFirstTab() {
        mCoordinator.addTab(mMockTab);
        mCoordinator.addTab(mSecondMockTab);
        mCoordinator.addTab(mThirdMockTab);
        mCoordinator.addTab(mFourthMockTab);
        assertThat(mModel.getTabList().getItemCount(), is(4));
        assertThat(mModel.getActiveTabIndex(), is(0));

        mCoordinator.removeTab(mMockTab);

        assertThat(mModel.getTabList().getItemCount(), is(3));
        assertThat(mModel.getActiveTabIndex(), is(0));
    }

    @Test
    public void testDeletingFirstAndOnlyTabInvalidatesActiveTab() {
        mCoordinator.addTab(mMockTab);
        mCoordinator.removeTab(mMockTab);

        assertThat(mModel.getTabList().getItemCount(), is(0));
        assertThat(mModel.getActiveTabIndex(), is(AccessorySheetModel.NO_ACTIVE_TAB));
    }

    @Test
    public void testDeletedActiveTabDisappearsAndActivatesLeftNeighbor() {
        mCoordinator.addTab(mMockTab);
        mCoordinator.addTab(mSecondMockTab);
        mCoordinator.addTab(mThirdMockTab);
        mCoordinator.addTab(mFourthMockTab);
        mModel.setActiveTabIndex(2);
        mModel.addObserver(mMockPropertyObserver);

        mCoordinator.removeTab(mThirdMockTab);

        verify(mMockPropertyObserver).onPropertyChanged(mModel, PropertyKey.ACTIVE_TAB_INDEX);
        assertThat(mModel.getTabList().getItemCount(), is(3));
        assertThat(mModel.getActiveTabIndex(), is(1));
    }

    @Test
    public void testCorrectsPositionOfActiveTabForDeletedPredecessors() {
        mCoordinator.addTab(mMockTab);
        mCoordinator.addTab(mSecondMockTab);
        mCoordinator.addTab(mThirdMockTab);
        mCoordinator.addTab(mFourthMockTab);
        mModel.setActiveTabIndex(2);
        mModel.addObserver(mMockPropertyObserver);

        mCoordinator.removeTab(mSecondMockTab);

        verify(mMockPropertyObserver).onPropertyChanged(mModel, PropertyKey.ACTIVE_TAB_INDEX);
        assertThat(mModel.getTabList().getItemCount(), is(3));
        assertThat(mModel.getActiveTabIndex(), is(1));
    }

    @Test
    public void testDoesntChangePositionOfActiveTabForDeletedSuccessors() {
        mCoordinator.addTab(mMockTab);
        mCoordinator.addTab(mSecondMockTab);
        mCoordinator.addTab(mThirdMockTab);
        mCoordinator.addTab(mFourthMockTab);
        mModel.setActiveTabIndex(2);

        mCoordinator.removeTab(mFourthMockTab);

        assertThat(mModel.getTabList().getItemCount(), is(3));
        assertThat(mModel.getActiveTabIndex(), is(2));
    }
}