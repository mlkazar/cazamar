//
//  MFANTopHelp.h
//  MusicFan
//
//  Created by Michael Kazar on 6/11/14.
//  Copyright (c) 2014 Mike Kazar. All rights reserved.
//

#import <UIKit/UIKit.h>
#import "MFANTopApp.h"

@class MFANViewController;

@interface MFANTopHelp : UIView<MFANTopApp>

- (MFANTopHelp *) initWithFrame: (CGRect) frame viewController: (MFANViewController *) vc;

-(void) activateTop;

-(void) deactivateTop;

@end
