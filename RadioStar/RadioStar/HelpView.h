//
//  HelpView.h
//  MusicFan
//
//  Created by Michael Kazar on 6/11/14.
//  Copyright (c) 2014,2026 Mike Kazar. All rights reserved.
//

#import <UIKit/UIKit.h>
#import "HelpView.h"

@class ViewController;

@interface HelpView : UIView

- (HelpView *) initWithFile: (NSString *) file viewCont: (ViewController *) vc;

@end
