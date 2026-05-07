@protocol AudioInt
- (void) setupAudioSession: (BOOL) mix;

- (void) enterBackground;

- (void) leaveBackground;
@end
