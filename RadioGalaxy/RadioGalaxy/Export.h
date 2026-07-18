#import <UIKit/UIKit.h>
#import <Foundation/Foundation.h>

#import "BufferSlider.h"
#import "EditStation.h"
#import "ExportEntry.h"
#import "MFANAqStream.h"
#import "MFANStreamPlayer.h"
#import "SignStation.h"
#import "ViewController.h"
#import "SignView.h"

@interface Export : UIView<TopViewInt, UITableViewDataSource, UITableViewDelegate>

typedef void (^PromptContinuation)(NSString *value);

- (Export *) initWithStation: (SignStation *) station
		    viewCont: (ViewController *) vc;

- (void) setCallback: (id) obj withSel: (SEL) sel;
@end

