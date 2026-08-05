#import "ExportEntry.h"

@implementation ExportEntry {
    // all properties
}

- (ExportEntry *) initWithStartTime: (float) start end: (float) end {
    self = [super init];
    if (self != nil) {
	self.start = start;
	self.end = end;
	self.saved = false;
	self.damaged = false;
	self.fixable = false;
    }

    return self;
}
@end
