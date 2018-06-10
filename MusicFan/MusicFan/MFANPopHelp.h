//
//  MFANPopHelp.h
//  DJ To Go
//
//  Created by Michael Kazar on 2/25/15.
//  Copyright (c) 2015 Mike Kazar. All rights reserved.
//

#import <UIKit/UIKit.h>
#import "MFANTopApp.h"

@class MFANViewController;

@interface MFANPopHelp : UIView
- (MFANPopHelp *) initWithFrame: (CGRect) frame
		       helpFile: (NSString *) helpFileName
		     parentView: (UIView *) parentView
		    warningFlag: (int) warningFlag;

- (BOOL) shouldShow;

- (void) checkShow;

- (void) donePressed: (id) junk;

- (void) show;

- (void) noMorePressed: (id) junk;

@end
