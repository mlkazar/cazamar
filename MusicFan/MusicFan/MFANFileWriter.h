#import <UIKit/UIKit.h>

@interface MFANFileWriter : NSObject

- (MFANFileWriter *) initWithFile: (NSString *) file;

- (BOOL) failed;

- (FILE *) fileOf;

- (int32_t) flush;

- (void) cleanup;

@end
