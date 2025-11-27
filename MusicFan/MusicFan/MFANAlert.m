//
//  MFANAlert.m
//  DJ To Go
//
//  Created by Michael Kazar on 11/27/25.
//  Copyright © 2025 Mike Kazar. All rights reserved.
//

#import "MFANAlert.h"
#import "MFANCGUtil.h"

NS_ASSUME_NONNULL_BEGIN

@implementation MFANAlert {
    UIAlertController *_alert;
}

- (MFANAlert *) initWithTitle: (NSString *) title
		      message:(NSString *) message
		  buttonTitle:(NSString *) buttonTitle {
    UIAlertController *alert;

    self = [super init];
    if (self != nil) {
	_alert = [UIAlertController
		    alertControllerWithTitle: title
				     message: message
			      preferredStyle: UIAlertControllerStyleAlert];
	if (buttonTitle != nil) {
	    UIAlertAction *action = [UIAlertAction actionWithTitle: buttonTitle
							     style: UIAlertActionStyleDefault
							   handler: ^(UIAlertAction *act) {}];
	    [_alert addAction: action];
	}
    }

    return self;
}

- (MFANAlert *) initWithTitleEx: (NSString *) title
		      messageEx:(NSString *) message
		  buttonTitleEx:(NSString *) buttonTitle
		      handlerEx: (void(^)(UIAlertAction *))  handler
{
    UIAlertController *alert;

    self = [super init];
    if (self != nil) {
	_alert = [UIAlertController
		    alertControllerWithTitle: title
				     message: message
			      preferredStyle: UIAlertControllerStyleAlert];

	// add requested button
	UIAlertAction *action = [UIAlertAction actionWithTitle: buttonTitle
							 style: UIAlertActionStyleDefault
						       handler: handler];
	[_alert addAction: action];

	// add cancel button that doesn't do anything
	action = [UIAlertAction actionWithTitle: @"Cancel"
					  style: UIAlertActionStyleCancel
					handler: ^(UIAlertAction *act) {}];
	[_alert addAction: action];
    }

    return self;
}

- (void) show {
    [currentViewController() presentViewController: _alert animated:YES completion: nil];
}

- (void) dismiss {
    [_alert dismissViewControllerAnimated: YES completion:nil];
}

@end

NS_ASSUME_NONNULL_END
