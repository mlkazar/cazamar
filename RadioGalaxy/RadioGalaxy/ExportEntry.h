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
@property bool fixable;

// if fixable, the records at spliceStart and spliceEnd are identical,
// so when we're about to write the record at spliceStart, we continue
// at spliceEnd instead.
@property uint64_t spliceStart;
@property uint64_t spliceEnd;

- (ExportEntry *) initWithStartTime: (float) start end: (float) end;
@end
