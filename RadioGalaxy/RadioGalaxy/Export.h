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

@interface ExportId3V2 : NSObject
- (ExportId3V2 *) initWithGroup: (NSString *) group
			   song: (NSString *) song
			  album: (NSString *) album;
- (NSData *) getId;
@end

@interface Export : UIView<TopViewInt>

typedef void (^PromptContinuation)(NSString *value);

- (Export *) initWithStation: (SignStation *) station
		 exportEntry: (ExportEntry *) entry
		    viewCont: (ViewController *) vc;

#if 0
- (void) setCallback: (id) obj withSel: (SEL) sel;
#endif
@end

