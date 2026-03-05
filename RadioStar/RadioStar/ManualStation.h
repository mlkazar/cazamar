#import <UIKit/UIKit.h>
#import <Foundation/Foundation.h>

@class ViewController;

@interface ManualStation : UIView<UITextFieldDelegate>

@property NSString *stationName;
@property NSString *stationUrl;
@property bool canceled;

- (ManualStation *) initWithViewCont: (ViewController *) vc;

- (void) setCallback: (id) obj withSel: (SEL) sel;

@end

