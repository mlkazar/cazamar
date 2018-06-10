//
//  MFANPopStatus.h
//  DJ To Go
//
//  Created by Michael Kazar on 3/15/16.
//  Copyright (c) 2016 Mike Kazar. All rights reserved.
//

#import <UIKit/UIKit.h>
#import "MFANTopApp.h"

@class MFANViewController;

@interface MFANPopStatus : UIView
- (MFANPopStatus *) initWithFrame: (CGRect) frame
			      msg: (NSString *) initMsg
		       parentView: (UIView *) parentView;

- (void) donePressed: (id) junk;

- (void) show;

- (void) stop;

- (BOOL) canceled;

- (void) updateMsg: (NSString *) msg;

@end
