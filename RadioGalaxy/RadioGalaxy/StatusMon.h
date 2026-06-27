#import <UIKit/UIKit.h>
#import "HelpView.h"
#import "ViewController.h"

@interface StatusMon : NSObject

typedef void (^StatusMonBlock) (StatusMon *mon);

- (StatusMon *) initWithMessage: (NSString *) message
			  timer: (float) stallTime
		       viewCont: (ViewController *) vc
			  block: (StatusMonBlock) block;

- (void) updateStatus: (NSString *) updatedMessage;

@end
