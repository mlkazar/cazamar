#import <UIKit/UIKit.h>
#import <Foundation/Foundation.h>

#import "BufferSlider.h"
#import "EditStation.h"
#import "MFANAqStream.h"
#import "MFANStreamPlayer.h"
#import "SignStation.h"
#import "ViewController.h"
#import "SignView.h"

@interface ExportEntry : NSObject
@property NSString *song;
@property NSString *altSong;
@property float start;
@property float end;
@property bool saved;

- (ExportEntry *) initWithStartTime: (float) start end: (float) end;

- (void) setSong: (NSString *) song  alt: (NSString *) alt;
@end

@interface Export : UIView<TopViewInt, UITableViewDataSource, UITableViewDelegate>

typedef void (^PromptContinuation)(NSString *value);

- (Export *) initWithStation: (SignStation *) station
		    viewCont: (ViewController *) vc;

- (void) setCallback: (id) obj withSel: (SEL) sel;
@end

