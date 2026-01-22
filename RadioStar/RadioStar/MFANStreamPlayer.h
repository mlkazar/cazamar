#import <UIKit/UIKit.h>
#import <Foundation/Foundation.h>
#import <MediaPlayer/MediaPlayer.h>

NSString *MFANStreamPlayer_getUnknownString();

@interface MFANStreamPlayer : NSObject

- (MFANStreamPlayer *) init;

- (void) stop;

- (void) resume;

- (void) pause;

- (void) play: (NSString *) urlString;

- (NSString *)getCurrentPlaying;

- (BOOL) isPlaying;

- (void) setStateCallback: (id) callbackObj  sel: (SEL) callbackSel;

- (void) setSongCallback: (id) callbackObj  sel: (SEL) callbackSel;

- (int) getSongIndex;

- (NSString *) getEncodingType;

- (BOOL) stopped;

- (BOOL) shouldRestart;

- (NSString *) getStreamUrl;

- (float) dataRate;

- (BOOL) startRecordingFor: (id) who sel: (SEL) sel;

- (int32_t) stopRecording;

- (BOOL) recording;

@end
