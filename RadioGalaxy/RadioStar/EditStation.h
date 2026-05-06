#import <UIKit/UIKit.h>
#import <Foundation/Foundation.h>

#import "SignStation.h"
#import "ViewController.h"

@interface EditStation : UIView<UITextFieldDelegate,TopViewInt>

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

