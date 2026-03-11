#import <Foundation/Foundation.h>
#import <CoreAudio/CoreAudioTypes.h>
#import <CoreFoundation/CoreFoundation.h>

@interface SignSave : NSObject

typedef void (^CompletionBlock)();

- (int32_t) initSaveToFile: (NSMutableOrderedSet *) allStations;

- (int32_t) initRestoreFromFile: (NSMutableOrderedSet *) allStatons
		     completion: (CompletionBlock) block;

@end

