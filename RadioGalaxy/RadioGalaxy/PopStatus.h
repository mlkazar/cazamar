#import <UIKit/UIKit.h>
#import <Foundation/Foundation.h>

#import "BufferSlider.h"
#import "MFANAqStream.h"
#import "MFANStreamPlayer.h"
#import "SignStation.h"
#import "ViewController.h"
#import "SignView.h"

@interface PopStatus : UIView<TopViewInt>

- (PopStatus *) initWithFrame: (CGRect) frame
		     viewCont: (ViewController *) vc
		     signView: (SignView *) signView;

- (void) setCallback: (id) obj withSel: (SEL) sel;
@end

