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
@property NSString *streamType;
@property bool isPlaying;
@property uint32_t streamRateKb;
@property bool isRecording;
@property bool isSelected;
@property CGPoint origin;
@property UIImage *iconImage;

- (void) setRowColumn: (SignCoord) rowColumn;

- (SignStation *) init;

@end

NS_ASSUME_NONNULL_END
