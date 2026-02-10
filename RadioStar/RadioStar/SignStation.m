#import "SignStation.h"

@implementation SignStation {
    NSString *_stationName;
    NSString *_shortDescr;
    NSString *_streamUrl;
    NSString *_iconUrl;

    // row and column, both 0 based
    SignCoord _rowColumn;

    bool _isPlaying;
    bool _isRecording;
    bool _isSelected;

    CGPoint _origin;
}

- (void) setRowColumn: (SignCoord) rowColumn {
    _rowColumn = rowColumn;
}

- (SignStation *) init {
    self = [super init];
    if (self) {
	self.isPlaying = NO;
	self.isRecording = NO;
	self.isSelected = NO;
    }
    return self;
}
@end
