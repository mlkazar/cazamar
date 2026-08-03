#import <UIKit/UIKit.h>
#import <Foundation/Foundation.h>

@interface ExportEntry : NSObject

@property NSString *label;      // typically song name, group - song or group - song - album
@property NSString *groupName;
@property NSString *songName;
@property NSString *albumName;
@property float start;
@property float end;
@property bool saved;
@property bool damaged;

- (ExportEntry *) initWithStartTime: (float) start end: (float) end;
@end
