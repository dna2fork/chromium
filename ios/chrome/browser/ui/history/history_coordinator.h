// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IOS_CHROME_BROWSER_UI_HISTORY_HISTORY_COORDINATOR_H_
#define IOS_CHROME_BROWSER_UI_HISTORY_HISTORY_COORDINATOR_H_

#import <UIKit/UIKit.h>

#import "ios/chrome/browser/ui/coordinators/chrome_coordinator.h"

@protocol ApplicationCommands;
@protocol BrowserCommands;
@protocol UrlLoader;
@protocol HistoryPresentationDelegate;

// Coordinator that presents History.
@interface HistoryCoordinator : ChromeCoordinator
// The dispatcher for this Coordinator.
@property(nonatomic, weak) id<ApplicationCommands, BrowserCommands> dispatcher;
// URL loader being managed by this Coordinator.
@property(nonatomic, weak) id<UrlLoader> loader;
// Delegate used to make the Tab UI visible.
@property(nonatomic, weak) id<HistoryPresentationDelegate> presentationDelegate;
@end

#endif  // IOS_CHROME_BROWSER_UI_HISTORY_HISTORY_COORDINATOR_H_
