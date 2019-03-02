//
//  MFANTopList.h
//  DJ To Go
//
//  Created by Michael Kazar on 8/10/14.
//  Copyright (c) 2014 Mike Kazar. All rights reserved.
//

#import <UIKit/UIKit.h>
#import "MFANRadioConsole.h"
#import "MFANViewController.h"

@interface MFANTopList : UIView<UITextViewDelegate,
				    MFANTopApp,
				    UITableViewDelegate,
				    UITableViewDataSource>

- (id)initWithFrame:(CGRect)frame
	channelType: (MFANChannelType) channelType
	      level: (MFANTopLevel *) level
	    console: (MFANRadioConsole *) console
     viewController: (MFANViewController *) viewCont;

- (long) deletePrevious: (long) ix;

- (void) updatePodcastsWithForce: (BOOL) forceToZero;

- (void) refreshLibrary;

- (void) refreshRss: (NSInteger) ix;

- (void) refreshPressed: (id) sender;

@end
