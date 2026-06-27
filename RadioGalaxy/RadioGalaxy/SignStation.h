#import <Foundation/Foundation.h>
#import <CoreAudio/CoreAudioTypes.h>
#import <CoreFoundation/CoreFoundation.h>
#import "MFANAqStream.h"
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
@property uint32_t streamRateKb;
@property bool isSelected;
@property bool isLoaded;
@property bool isFrozen;
@property bool isSnapshot;
@property bool didRestoreBlocks;
@property uint32_t fileId;

// flags for the search operation
@property bool verified;	// we've checked the station
@property bool verifiedWorking;	// check indicated station is up

@property CGPoint origin;
@property uint16_t signIndex;
@property (nullable) UIImage *iconImage;
@property (nullable) MFANAqStream *recordingStream;
@property (nullable) MFANAqStreamBuffer *recordingBuffer;
@property uint64_t recordingPosition;

- (bool) isBkgStreaming;

- (void) setRowColumn: (SignCoord) rowColumn;

- (SignStation *) initWithFileId: (uint32_t) fileId;

- (void) setIconImageFromUrl: (BOOL) doLoad;

- (void) tryLoadFromUrl;

@end

NS_ASSUME_NONNULL_END
