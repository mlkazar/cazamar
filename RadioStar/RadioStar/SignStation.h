#import <Foundation/Foundation.h>
#import <CoreAudio/CoreAudioTypes.h>
#import <CoreFoundation/CoreFoundation.h>
#import "MFANCGUtil.h"

typedef struct _SignCoord {
    uint8_t _x;
    uint8_t _y;
} SignCoord;

NS_ASSUME_NONNULL_BEGIN

@interface SignStation : NSObject

@property NSString *stationName;
@property NSString *shortDescr;
@property NSString *streamUrl;
@property NSString *iconUrl;
@property bool isPlaying;
@property bool isRecording;
@property bool isSelected;
@property CGPoint origin;

- (void) setRowColumn: (SignCoord) rowColumn;

- (SignStation *) init;

@end

NS_ASSUME_NONNULL_END
