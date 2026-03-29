#import <UIKit/UIKit.h>
#import <Foundation/Foundation.h>

#import "MFANAqStream.h"
#import "MFANStreamPlayer.h"
#import "SignStation.h"
#import "ViewController.h"
#import "SignView.h"

@interface PopStatus : UIButton

- (PopStatus *) initWithFrame: (CGRect) frame
		     viewCont: (ViewController *) vc
		     signView: (SignView *) signView;

- (void) setCallback: (id) obj withSel: (SEL) sel;

- (void) shutdown;

@end

