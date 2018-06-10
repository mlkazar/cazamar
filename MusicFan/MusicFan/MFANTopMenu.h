//
//  MFANTopMenu.h
//  DJ To Go
//
//  Created by Michael Kazar on 12/4/14.
//  Copyright (c) 2014 Mike Kazar. All rights reserved.
//

#import <UIKit/UIKit.h>
#import "MFANTopApp.h"
#import "MFANViewController.h"

@class MFANViewController;

@interface MFANTopMenu : UIView<MFANTopApp>

- (id)initWithFrame:(CGRect)frame
     viewController: (MFANViewController *) viewCont;

@end
