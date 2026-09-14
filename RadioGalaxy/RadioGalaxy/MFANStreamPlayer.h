#import <UIKit/UIKit.h>
#import <Foundation/Foundation.h>
#import <MediaPlayer/MediaPlayer.h>

#import "Callback.h"
#import "MFANAqStream.h"

NSString *MFANStreamPlayer_getUnknownString(void);

@interface MFANStreamPlayer : NSObject

@property BOOL muted;

- (MFANStreamPlayer *) initWithStreamBuffer : (MFANAqStreamBuffer *) stream
					  ms: (uint64_t) ms;

- (void) resume;

- (void) pause;

- (NSString *)getCurrentPlaying;

- (bool) isPaused;

- (BOOL) isPlaying;

- (void) addStateCallback: (NSObject *) callbackObj  sel: (SEL) callbackSel;

- (void) addSongCallback: (id) callbackObj  sel: (SEL) callbackSel;

- (void) shutdown;

- (bool) isShutdown;

- (float) getDataRate;

- (NSString *) getEncodingType;

- (uint64_t) getSeekTarget: (float) offset;

- (void) mute;

- (void) unmute;

- (void) setupAudioSession: (BOOL) mix;

@end
