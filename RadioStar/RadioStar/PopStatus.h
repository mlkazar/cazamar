#import <UIKit/UIKit.h>
#import <Foundation/Foundation.h>

#import "MFANAqStream.h"
#import "MFANStreamPlayer.h"
#import "SignStation.h"
#import "ViewController.h"

@interface PopStatus : UIView

- (PopStatus *) initWithFrame: (CGRect) frame
		     viewCont: (ViewController *) vc
		       stream: (MFANAqStream *) stream
		       player: (MFANStreamPlayer *) player
		      station: (SignStation *) station;

- (void) setCallback: (id) obj withSel: (SEL) sel;

- (void) shutdown;

@end

