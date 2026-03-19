#import <UIKit/UIKit.h>
#import <Foundation/Foundation.h>
#import <MediaPlayer/MediaPlayer.h>

#import "MFANAqStream.h"

NSString *MFANStreamPlayer_getUnknownString(void);

@interface MFANStreamPlayer : NSObject

- (MFANStreamPlayer *) initWithStream : (MFANAqStream *) stream ms: (uint64_t) ms;

- (void) resume;

- (void) pause;

- (NSString *)getCurrentPlaying;

- (bool) isPaused;

- (BOOL) isPlaying;

- (void) setStateCallback: (id) callbackObj  sel: (SEL) callbackSel;

- (void) setSongCallback: (id) callbackObj  sel: (SEL) callbackSel;

- (void) shutdown;

- (bool) isShutdown;

- (float) getDataRate;

- (NSString *) getEncodingType;

- (NSString *) getPublicUrl;

- (uint64_t) getSeekTarget: (float) offset;

@end
