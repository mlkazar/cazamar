//
//  MFANViewController.h
//  MusicFan
//
//  Created by Michael Kazar on 4/22/14.
//  Copyright (c) 2014 Mike Kazar. All rights reserved.
//

#import <UIKit/UIKit.h>
#import "MFANTopLevel.h"
#import "MFANTopEdit.h"
#import "MFANTopApp.h"

@class MFANTopSettings;

@interface MFANViewController : UIViewController

-(void) restorePlayer;

- (BOOL) shouldAutorotate;

- (void) switchToAppByName: (NSString *) name;

- (MFANTopSettings *) getSettings;

- (void) enterBackground;

- (void) leaveBackground;

- (UIView<MFANTopApp> *) getTopAppByName: (NSString *) name;

- (void) setChannelType: (MFANChannelType) channelType;

- (MFANChannelType) channelType;

- (void) makeActive: (UIView<MFANTopApp> *) newView;

@end
