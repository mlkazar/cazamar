//
//  MFANAddChoice.h
//  DJ To Go
//
//  Created by Michael Kazar on 8/14/14.
//  Copyright (c) 2014 Mike Kazar. All rights reserved.
//

#import <UIKit/UIKit.h>

@class MFANViewController;

@interface MFANAddChoice : UIView
- (id)initWithFrame:(CGRect)frame
	     labels: (NSArray *)labels
	   contexts: (NSArray *) contexts
     viewController: (MFANViewController *) viewCon;

- (void) addCallback: (id) target
	  withAction: (SEL) callback;

@end
