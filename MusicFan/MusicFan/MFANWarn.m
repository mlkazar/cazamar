//
//  MFANWarn.m
//  DJ To Go
//
//  Created by Michael Kazar on 2/7/15.
//  Copyright (c) 2015 Mike Kazar. All rights reserved.
//

#import "MFANWarn.h"
#import "MFANCGutil.h"

@implementation MFANWarn {
    NSTimer *_timer;
    UIAlertController *_alert;
}

- (MFANWarn *) initWithTitle: (NSString *) title
		     message: (NSString *) message
			secs: (float) secs;
{
    self = [super init];
    if (self != nil) {
	_alert = [UIAlertController
			 alertControllerWithTitle: title
					  message: message
				   preferredStyle:UIAlertControllerStyleAlert];
	UIAlertAction* defaultAction =
	    [UIAlertAction actionWithTitle:@"OK"
				     style:UIAlertActionStyleDefault
				   handler:^(UIAlertAction * action) {}];
	// [_alert addAction: defaultAction];

	_timer = [NSTimer scheduledTimerWithTimeInterval: secs
			  target:self
			  selector:@selector(warnPart2:)
			  userInfo:nil
			  repeats: NO];

	[currentViewController() presentViewController:_alert animated:YES completion:nil];
    }

    return self;
}

- (void) warnPart2: (id) junk
{
    [_alert dismissViewControllerAnimated: YES completion:nil];
    _alert = nil;
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
