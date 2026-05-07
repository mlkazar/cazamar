//
//  HelpView.h
//  MusicFan
//
//  Created by Michael Kazar on 6/11/14.
//  Copyright (c) 2014,2026 Mike Kazar. All rights reserved.
//

#import <UIKit/UIKit.h>
#import "HelpView.h"
#import "ViewController.h"

@interface HelpView : UIView<TopViewInt>

- (HelpView *) initWithFile: (NSString *) file viewCont: (ViewController *) vc;

@end
