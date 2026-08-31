#import <UIKit/UIKit.h>
#import <Foundation/Foundation.h>
#import "ViewController.h"
#import "SignView.h"

@interface TopAlert : NSObject
- (TopAlert *) initWithMessage: (NSString *) message
		      duration: (float) duration
		      viewCont: (ViewController *) vc;
@end

@interface TopView : UIView<TopViewInt>

- (TopView *) initWithFrame: (CGRect) frame ViewCont: (ViewController *) vc;

- (void) tvActivate;

- (void) tvDeactivate;

- (void) songChanged: (id) player;

- (void) stateChanged: (id) player;

@end
