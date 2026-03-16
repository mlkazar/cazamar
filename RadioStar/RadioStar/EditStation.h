#import <UIKit/UIKit.h>
#import <Foundation/Foundation.h>

#import "SignStation.h"

@class ViewController;

@interface EditStation : UIView<UITextFieldDelegate>

@property NSString *stationName;
@property NSString *shortDescr;
@property NSString *streamUrl;
@property BOOL canceled;
@property BOOL doRemove;

- (EditStation *) initWithFrame: (CGRect) frame
			station: (SignStation *) station
		       viewCont: (ViewController *) vc;

- (void) setCallback: (id) obj withSel: (SEL) sel;

@end

