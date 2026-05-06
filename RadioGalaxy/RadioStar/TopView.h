#import <UIKit/UIKit.h>
#import <Foundation/Foundation.h>
#import "ViewController.h"
#import "SignView.h"

@interface TopView : UIView<TopViewInt>

- (TopView *) initWithFrame: (CGRect) frame ViewCont: (ViewController *) vc;

- (void) activateTopView;

- (void) deactivateTopView;

- (void) songChanged: (id) player;

- (void) stateChanged: (id) player;

@end
