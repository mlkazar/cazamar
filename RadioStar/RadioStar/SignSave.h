#import <Foundation/Foundation.h>
#import <CoreAudio/CoreAudioTypes.h>
#import <CoreFoundation/CoreFoundation.h>

@interface SignSave : NSObject

+ (int32_t) saveStationsToFile: (NSMutableOrderedSet *) allStations;

+ (int32_t) restoreStationsFromFile: (NSMutableOrderedSet *) allStatons;

@end

