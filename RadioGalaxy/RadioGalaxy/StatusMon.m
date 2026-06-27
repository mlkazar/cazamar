#include "StatusMon.h"

@implementation StatusMon {
    ViewController *_vc;
    NSTimer *_stallTimer;
    UIAlertController *_alert;
    NSString *_message;
}

- (StatusMon *) initWithMessage: (NSString *) message
			  timer: (float) stallTime
		       viewCont: (ViewController *) vc
			  block: (StatusMonBlock) block {
    self = [super init];
    if (self != nil) {
	_vc = vc;
	_message = message;
	_stallTimer = [NSTimer scheduledTimerWithTimeInterval: stallTime
						       target: self
						     selector: @selector(displayStatus:)
						     userInfo: nil
						      repeats: NO];
	dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0),
		       ^{
			   block(self);
			   dispatch_async(dispatch_get_main_queue(), ^{
				   [self cleanup];
			       });
		       });
    }

    return self;
}

// Must run on the main queue, othewise alert code asserts.
- (void) cleanup {
    // now remove the alert controller if it was present
    NSLog(@"=5= about to remove alert and cancel timer");
    if (_alert) {
	[_alert dismissViewControllerAnimated: YES completion: nil];
	_alert = nil;
    }
    if (_stallTimer != nil) {
	[_stallTimer invalidate];
	_stallTimer = nil;
    }
}

// Internal: used to put up the message if the code takes too long to execute
- (void) displayStatus: (NSTimer *) timer {
    _alert = [UIAlertController
				   alertControllerWithTitle: @"Status"
						    message: _message
					     preferredStyle: UIAlertControllerStyleAlert];
    UIAlertAction *action = [UIAlertAction actionWithTitle: @"Dismiss"
						     style:UIAlertActionStyleDefault
						   handler: ^(UIAlertAction *act) {
	    [self->_alert dismissViewControllerAnimated: YES completion: nil];
	    self->_alert = nil;
	    return;
	}];
    [_alert addAction: action];
    [_vc presentViewController: _alert animated:true completion: nil];
    NSLog(@"=5= presenting view controller");
}

// called from the user provided block to update the status
- (void) updateStatus: (NSString *) updatedMessage {
    _message = updatedMessage;
    if (_alert == nil)
	return;

    dispatch_async( dispatch_get_main_queue(), ^{
	    self->_alert.message = self->_message;
	});
}

@end
