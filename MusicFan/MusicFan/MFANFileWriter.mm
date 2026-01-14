#import "MFANFileWriter.h"

#include <string>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <inttypes.h>
#include <sys/stat.h>

// Usage: create FileWriter, init with name to create, and flush when
// done.
@implementation MFANFileWriter {
    FILE *_file;
    NSString *_tempName;
    NSString *_originalName;
}

- (MFANFileWriter *) initWithFile: (NSString *) file
{
    NSString *tempName = [NSString stringWithFormat: @"/temp-%d", getpid()];
    long i = -1;
    long end;

    self = [super init];
    if (self) {
	_file = NULL;

	end = [file length] - 1;

	for(i = end; i >= 0; i--) {
	    if ([file characterAtIndex:i] == '/')
		break;
	}
    }

    _originalName = file;

    if (i < 0) {
	// no '/' character
	_tempName = tempName;
    } else {
	_tempName = [[file substringToIndex: i] stringByAppendingString: tempName];
    }

    _file = fopen([_tempName cStringUsingEncoding: NSUTF8StringEncoding], "w");
    if (_file == NULL) {
	return self;
    }

    return self;
}

- (BOOL) failed {
    return (_file == NULL);
}

- (FILE *) fileOf {
    return _file;
}

// Call to discard temporary file, when something goes wrong
- (void) cleanup {
    if (_file) {
	fclose(_file);
	_file = NULL;
    }

    unlink([_tempName cStringUsingEncoding: NSUTF8StringEncoding]);
}

- (int32_t) flush {
    int32_t code;

    if (_file == NULL) {
	NSLog(@"FileWriter: flush called but never opened");
	return -1;
    }

    fflush(_file);
    int fd = fileno(_file);
    code = fsync(fd);
    fclose(_file);
    _file = NULL;

    if (code != 0) {
	NSLog(@"FileWriter: fsync failed");
	return code;
    }

    code = rename([_tempName cStringUsingEncoding: NSUTF8StringEncoding],
		  [_originalName cStringUsingEncoding: NSUTF8StringEncoding]);

    if(code != 0) {
	NSLog(@"FileWriter: rename failed");
    }

    return code;
}

@end
