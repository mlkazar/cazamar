//
//  MFANAppDelegate.h
//  MusicFan
//
//  Created by Michael Kazar on 4/19/14.
//  Copyright (c) 2014 Mike Kazar. All rights reserved.
//

#import <UIKit/UIKit.h>
#import "MFANTopLevel.h"
#import "MFANViewController.h"

@interface MFANAppDelegate : UIResponder <UIApplicationDelegate>

@property (strong, nonatomic) UIWindow *window;
@property (strong, nonatomic) MFANTopLevel *_topLevel;
@property (strong, nonatomic) MFANViewController *_controller;

@end
