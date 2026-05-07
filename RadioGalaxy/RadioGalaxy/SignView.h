//
//  SignView.h
//  RadioStar
//
//  Created by Michael Kazar on 11/29/25.
//

#import <UIKit/UIKit.h>
#import <Foundation/Foundation.h>
#import "AudioInt.h"
#import "MFANStreamPlayer.h"
#import "RadioHistory.h"
#import "SignStation.h"
#import "ViewController.h"

NS_ASSUME_NONNULL_BEGIN

@interface SignView : UIView<AudioInt>

@property RadioHistory *history;
@property ViewController *vc;
@property (readonly) MFANStreamPlayer *player;
@property BOOL resumeAtEnd;

- (SignView *) initWithFrame: (CGRect) frame ViewCont: (ViewController *)vc;

+ (Class) layerClass;

- (CALayer *) makeBackingLayer;

- (void) setSongCallback: (NSObject *) callbackObj  sel: (SEL) callbackSel;

- (void) setStateCallback: (NSObject *) callbackObj  sel: (SEL) callbackSel;

- (NSString *) getPlayingStationName;

- (MFANStreamPlayer *) getCurrentPlayer;

- (MFANAqStream *) getCurrentStream;

- (SignStation *) getCurrentStation;

- (float) getCurrentBufferTimestamp;

- (void) setRadioHistory: (RadioHistory *) history;

- (void) seek: (float) distance relative: (bool) isRelative;

- (void) stopRadioResetStream: (BOOL) doReset;

- (void) changeStationBy: (int16_t) change;

- (void) startCurrentStation;

- (void) displayAppOptions;

- (void) stopRecording;

- (void) startRecording;

- (void) stopRadioResumeAtEnd;

- (float) getStationBufferEnd: (SignStation *) station;

- (float) getStationBufferStart: (SignStation *) station;

- (MFANAqStream *) startStationStream: (SignStation *) station;

- (void) setupAudioSession: (BOOL) mix;

@end

NS_ASSUME_NONNULL_END
