//
//  MFANWarn.m
//  DJ To Go
//
//  Created by Michael Kazar on 2/7/15.
//  Copyright (c) 2015 Mike Kazar. All rights reserved.
//

#import "MFANWarn.h"

@implementation MFANWarn {
    NSTimer *_timer;
    UIAlertView *_alertView;
}

- (MFANWarn *) initWithTitle: (NSString *) title
		     message: (NSString *) message
			secs: (float) secs;
{
    self = [super init];
    if (self != nil) {
	_alertView = [[UIAlertView alloc]
			 initWithTitle: title
			 message: message
			 delegate:nil 
			 cancelButtonTitle:nil
			 otherButtonTitles:nil];

	_timer = [NSTimer scheduledTimerWithTimeInterval: secs
			  target:self
			  selector:@selector(warnPart2:)
			  userInfo:nil
			  repeats: NO];

	[_alertView show];
    }

    return self;
}

- (void) warnPart2: (id) junk
{
    [_alertView dismissWithClickedButtonIndex: 0 animated: YES];
    _alertView = nil;
    _timer = nil;
}

/*
// Only override drawRect: if you perform custom drawing.
// An empty implementation adversely affects performance during animation.
- (void)drawRect:(CGRect)rect {
    // Drawing code
}
*/

@end
