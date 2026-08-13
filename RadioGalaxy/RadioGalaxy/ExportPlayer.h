#import <UIKit/UIKit.h>
#import <Foundation/Foundation.h>
#import <AVFoundation/AVFoundation.h>
#import "ViewController.h"

@interface ExportPlayerEntry : NSObject

@property NSString *fileName;
@property NSString *name;
@property bool playing;
@property NSString *song;
@property NSString *album;
@property NSString *artist;

- (ExportPlayerEntry *) initWithName: (NSString *) fileName;

@end

@interface ExportPlayer : UIView<TopViewInt, UITableViewDelegate, UITableViewDataSource,
				     AVAudioPlayerDelegate>

- (ExportPlayer *) initWithViewCont: (ViewController *) vc;

- (void) activateTopView;

- (void) deactivateTopView;

@end
