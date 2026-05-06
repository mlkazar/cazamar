#import <UIKit/UIKit.h>
#import <Foundation/Foundation.h>

#import "TopViewInt.h"

@class ViewController;

@interface ManualStation : UIView<UITextFieldDelegate,TopViewInt>

@property NSString *stationName;
@property NSString *stationUrl;
@property bool canceled;

- (ManualStation *) initWithViewCont: (ViewController *) vc;

- (void) setCallback: (id) obj withSel: (SEL) sel;

@end

