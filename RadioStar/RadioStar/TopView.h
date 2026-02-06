#import <UIKit/UIKit.h>
#import <Foundation/Foundation.h>
#import "ViewController.h"
#import "SignView.h"

@interface TopView : UIView

- (TopView *) initWithFrame: (CGRect) frame ViewCont: (ViewController *) vc;

- (void) songChanged: (id) player;

- (void) stateChanged: (id) player;

@end
