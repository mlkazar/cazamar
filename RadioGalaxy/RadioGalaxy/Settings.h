//
//  Settings.h
//  RadioGalaxy
//
//  Created by Michael Kazar on 11/287/15.
//  Copyright (c) 2015-2026 Mike Kazar. All rights reserved.
//

#import <UIKit/UIKit.h>

#import "ViewController.h"

@interface SettingsLabel : UIButton

- (SettingsLabel *) initWithFrame: (CGRect) frame
		       target: (id) target
		     selector: (SEL) selector;
@end

@interface Settings : UIView<TopViewInt>

@property (readonly) bool keepStreamingAfterSwitch;
@property (readonly) bool keepStreamingAfterCarPlay;
@property (readonly) uint32_t streamBufferMinutes;
@property (readonly) uint32_t maxSearchReturn;
@property (readonly) bool animateIcons;

- (Settings *) initWithViewController: (ViewController *) vc;

- (void) activateTopView;

- (void) deactivateTopView;

@end
