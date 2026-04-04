#import "SignStation.h"

@implementation SignStation {
    NSString *_stationName;
    NSString *_shortDescr;
    NSString *_streamUrl;
    NSString *_iconUrl;
    NSString *_streamType;
    UIImage *_iconImage;
    uint16_t _signIndex;

    uint32_t _streamRateKb;

    // row and column, both 0 based
    SignCoord _rowColumn;

    bool _isRecording;		// so don't stop stream if switch away
    bool _isSelected;		// selected as part of search operation
    bool _isLoaded;		// image is loaded

    MFANAqStream *_recordingStream;	// stream being recorded
    uint64_t _recordingPosition;	// ms currently coming out the speaker

    CGPoint _origin;
}

- (void) setRowColumn: (SignCoord) rowColumn {
    _rowColumn = rowColumn;
}

// Is this station currently streaming data
- (bool) isBkgStreaming {
    if (_recordingStream != nil &&
	!_recordingStream.shuttingDown) {
	return true;
    } else {
	return false;
    }
}

- (SignStation *) init {
    self = [super init];
    if (self) {
	self.isRecording = NO;
	self.isSelected = NO;
	self.isLoaded = NO;
    }
    return self;
}

+ (UIImage *) imageFromText: (NSString *) text Size: (CGSize) size {
    // 1. Ensure the UIKit context is pushed (necessary if not in drawRect:)
    //    If you are in a UIView's drawRect:, this is already handled.

    // Graphics context doesn't handle 0 width contexts
    if ([text length] <= 0) {
	text = @"??";
    }

    size = [text sizeWithFont: [UIFont systemFontOfSize: size.height]];
    size.width *= 1.2;

    UIGraphicsBeginImageContext(size);

    // 2. Define the text and attributes.  The '-2' reducing the
    // height below works around a weirdness where the lowest pixel in
    // a character gets duplicated as a vertical paint drip in gray
    // across the bottom of the graphics image.
    UIFont *font = [UIFont systemFontOfSize: size.height-2];
    UIColor *textColor = [UIColor blackColor];

    NSMutableParagraphStyle *paragraphStyle = [[NSMutableParagraphStyle alloc] init];
    paragraphStyle.alignment = NSTextAlignmentCenter; // Example alignment

    NSDictionary *attributes = @{
        NSFontAttributeName: font,
        NSForegroundColorAttributeName: textColor,
        NSParagraphStyleAttributeName: paragraphStyle
    };

    // 3. Define the drawing rectangle
    CGRect textRect = CGRectMake(0.0, 0.0, size.width, size.height);

    // 4. Draw the string
    [text drawInRect:textRect withAttributes:attributes]; // Or use drawAtPoint for single line

    UIImage *resultImage = UIGraphicsGetImageFromCurrentImageContext();
    UIGraphicsEndImageContext();

    return resultImage;
}

- (void) tryLoadFromUrl {
    UIImage *image;
    if (!_isLoaded && [_iconUrl length] > 0) {
	NSURL *imageUrl = [NSURL URLWithString: _iconUrl];
	NSData *imageData = [[NSData alloc] initWithContentsOfURL: imageUrl];
	if (imageData != nil) {
	    image = [UIImage imageWithData: imageData];
	    if (image != nil) {
		_iconImage = [self fixupImage: image];
		_isLoaded = YES;
	    }
	}
    }
}

- (UIImage *) fixupImage: (UIImage *) image {
    CGSize size = image.size;
    CGRect rect = CGRectMake(0, 0, size.width, size.height);
    UIGraphicsBeginImageContextWithOptions(size, YES, image.scale);

    // fill it with white so that transparent parts of the icon don't
    // appear weird/dark, and then draw the image of the icon into
    // this current context.
    CGContextRef context = UIGraphicsGetCurrentContext();
    [[UIColor whiteColor] setFill];
    CGContextFillRect(context, rect);
    [image drawInRect: CGRectMake(0, 0, size.width, size.height)];
    UIImage *newImage = UIGraphicsGetImageFromCurrentImageContext();
    UIGraphicsEndImageContext();

    return newImage;
}

- (void) setIconImageFromUrl: (bool) doLoad {
    UIImage *image;
    NSString *nameToUse;
    static const uint32_t maxChars = 6;
    CGSize nameSize;

    // already done
    if (_iconImage != nil)
       return;

    // try URL
    if (doLoad && [_iconUrl length] > 0) {
	[self tryLoadFromUrl];
    }

    // generate image from text of first few characters of station name
    if (image == nil) {
       uint64_t length = [_stationName length];
       if (length > maxChars) {
           nameToUse = [_stationName substringToIndex: maxChars];
       } else {
           nameToUse = _stationName;
       }

       nameSize.width = 100.0;
       nameSize.height = 30.0;
       image = [SignStation imageFromText: nameToUse Size: nameSize];
    }

    // Now fix the image by turning any transparent pixels into white pixels
    _iconImage = [self fixupImage: image];
}

@end
