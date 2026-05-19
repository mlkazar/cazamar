#import <UIKit/UIKit.h>
#import <Foundation/Foundation.h>

#import "BufferSlider.h"
#import "EditStation.h"
#import "MFANAqStream.h"
#import "MFANStreamPlayer.h"
#import "SignStation.h"
#import "ViewController.h"
#import "SignView.h"

@interface PopStatus : UIView<TopViewInt>

- (PopStatus *) initWithFrame: (CGRect) frame
		  editStation: (EditStation *) editStation
		      station: (SignStation *) station
		     viewCont: (ViewController *) vc
		     signView: (SignView *) signView;

- (void) setCallback: (id) obj withSel: (SEL) sel;
@end

