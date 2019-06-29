//
//  MFANPlayerStatus.m
//  MusicFan
//
//  Created by Michael Kazar on 5/21/14.
//  Copyright (c) 2014 Mike Kazar. All rights reserved.
//

#import <MediaPlayer/MediaPlayer.h>
#import "MFANPlayerStatus.h"
#import "MFANComm.h"

#include "osp.h"

/* This is a module that tries to track the true playing state of the
 * MPMusicPlayerController.  Apparently it is beyond Apple's ability
 * to fix the playbackState method.
 *
 * We only report stopped or playing; you'll have to improve the code
 * to differentiate seeking forward or backwards, and I have no idea
 * how to tell stopped from paused, or even what the difference *is*.
 * The documentation for the two says "The music player is stopped"
 * vs.  "The music player is paused."  Thanks for that clear
 * explanation, Apple.
 */

@implementation MFANPlayerStatus {
    MPMusicPlayerController *_mpPlayer;
    NSTimer *_timer;

    /* who to send notifications to */
    id _owner;
    SEL _selector;

    /* state is 0 right after a status check, 1 after the first timer
     * expires, and 2 through N after the 2nd timer expires
     * (potentially multiple times).  After each expiration, we
     * compare the music position with its value at the previous time.
     * When the state has reached N, we believe our status is
     * accurate.
     *
     * The rationale here is that the player itself doesn't indicate
     * the right status, and usually the playing time value is better
     * to use.  But we don't know how long it will take before
     * playingTime is correctly tracking the player's state, and yet
     * we don't want to wait until the worst case before trying to
     * give an indication.  So, we indicate an early guess (after the
     * second timer fires), and then keep watching for a few more
     * timer expirations, incrementing _state each expiration, until
     * _maxState is reached), and indicating status updates if the
     * music player state changes during this time.
     *
     * At each expiration in state >= 2, if the new status isn't the
     * status we indicated to our user, we dispatch a new status
     * indication, after changing our playing status to what we've
     * discovered.  If the new status equals the already reported
     * status, we don't indicate anything.
     */
    int _state;

    int _forceRecheck;

    /* flag saying enabled or not */
    int _enabled;

    /* valid in state >=2, this is the # of seconds into the song that
     * we've gone at the time the previous timer expired.
     */
    CGFloat _secondsIntoSong;

    /* the state at the time we last sent a state change indication */
    MPMusicPlaybackState _lastIndicated;

    /* our best guess of the current state */
    MPMusicPlaybackState _currentState;

    /* we wake up the async thread to get our time */
    NSCondition *_pbtCond;	/* wakeup pbt async thread */
    uint64_t _pbtCounter;	/* increases each time we lookup the current playback time */
    float _pbtLastTime;		/* last valid currentPlaybackTime */
    uint32_t _pbtStartMs;	/* time last currentPlaybackTime started */
    uint32_t _pbtEndMs;		/* time last currentPlaybackTime started */
    BOOL _pbtActive;		/* bkg thread is active */
}

/* time in seconds between checking if musicplayer is playing */
static const float _checkInterval = 0.2;

/* the largest value of the _state variable.  If maxState is 3, for
 * example, we'll record the first time when the state reaches 1,
 * check after a timer period, when the state reaches 2, and then and
 * check a 2nd time after the state reaches 3.
 */
static const int _maxState = 4;

- (MFANPlayerStatus *) initObject: (id) obj sel: (SEL) selector
{
    self = [super init];
    if (self) {
	/* remember the input parameters */
	_owner = obj;
	_selector = selector;

	/* just in case this is faster */
	_mpPlayer = [MPMusicPlayerController systemMusicPlayer];
	_timer = nil;
	_state = 0;
	_enabled = 0;
	_forceRecheck = 0;
	_lastIndicated = (MPMusicPlaybackState) 100; /* invalid last value indicated to caller */
	_currentState = [_mpPlayer playbackState];
	if (_currentState != MPMusicPlaybackStatePlaying)
	    _currentState = MPMusicPlaybackStatePaused;

	_pbtCond = [[NSCondition alloc] init];
	_pbtActive = NO;
	_pbtStartMs = 0;
	_pbtEndMs = 0;
	[NSThread detachNewThreadSelector:@selector(pbtInit:)
		  toTarget: self
		  withObject: nil];

	[[NSNotificationCenter defaultCenter] addObserver: self
					      selector: @selector(isPlayingChanged:)
					      name: MPMusicPlayerControllerPlaybackStateDidChangeNotification
					      object: _mpPlayer];
	[_mpPlayer beginGeneratingPlaybackNotifications];
    }

    return self;
}

- (void) pbtSetTime: (NSTimeInterval) newTime
{
    [_pbtCond lock];
    _pbtLastTime = newTime;
    _pbtEndMs = osp_get_ms();
    [_pbtCond unlock];
}

- (float) pbtGetTime
{
    float lastTime;

    /* make sure we trigger an update soon */
    [_pbtCond lock];

    if (!_pbtActive) {
	_pbtActive = YES;
	[_pbtCond broadcast];	/* make sure bkg thread is working */
    }

    /* if we're playing music, then increment the last time field by the time
     * that passed since we last called the music player.  Otherwise, just use
     * the last time returned from the music player.
     */
    if (_currentState == MPMusicPlaybackStatePlaying)
	lastTime = _pbtLastTime + (osp_get_ms() - _pbtEndMs)/1000.0;
    else
	lastTime = _pbtLastTime;

    [_pbtCond unlock];

    NSLog(@"- pbtGetTime returns %f", lastTime);

    return lastTime;
}

/* return the last time returned, and optionally how out of date the time might be */
- (float) pbtRecentTime: (float *) errorp
{
    NSTimeInterval rval;
    uint64_t lastCounter;
    uint32_t lastTime;
    BOOL timedOut;
    float delay;

    /* don't wait more than a fraction of the interval between checks, since if we exceed
     * that time, we won't really know the time between the probes.
     */
    delay = _checkInterval / 4;

    NSLog(@"> pbtRecentTime starts");
    [_pbtCond lock];
    lastCounter = _pbtCounter;
    if (!_pbtActive) {
	_pbtActive = YES;
	[_pbtCond broadcast];	/* make sure bkg thread is working */
    }

    /* now wait for the counter to change (it should, since we woke up the thread)
     * or 1/4 second has passed.
     */
    lastTime = osp_time_ms();
    timedOut = NO;
    while (1) {
	[_pbtCond waitUntilDate: [NSDate dateWithTimeIntervalSinceNow: delay]];
	if (_pbtCounter > lastCounter) {
	    break;
	}
	else if (osp_time_ms() - lastTime > (int) (delay * 800)) {
	    /* we make timeout condition a little shorter to avoid
	     * going around an extra time if we wake up a little
	     * early.  800 --> 80% of delay.
	     */
	    timedOut = YES;
	    break;
	}
    }

    [_pbtCond unlock];

    rval = _pbtLastTime;

    if (errorp) {
	/* if async task still running, then _pbtActive is set, and we
	 * have no idea how old the value in _pbtLastTime is, so we
	 * return a big value.  Otherwise, _pbtStartMs was the time
	 * before the last currentPlaybackTime call started, so
	 * current time can't be older than that.
	 */
	if (timedOut) {
	    NSLog(@"- pbGetTime gave up");
	    *errorp = 100.0;
	}
	else {
	    /* subtracting pbtStartMs is the strictly worse case
	     * approach to computing the error, but we assume that the
	     * waiting occurs before actually getting the time (after
	     * all, why would currentTime wait if it already had the
	     * answer), so pbtEndMs is probably more accurate.
	     */
	    *errorp = (osp_get_ms() - _pbtEndMs) / 1000.0;
	}
    }

    NSLog(@"< pbtRecentTime returns %f", rval);
    return rval;
}

- (void) pbtInit: (id) junk
{
    float newTime;
    uint32_t endMs;

    /* wait until someone needs the currentPlaybackTime, and then get it, along with
     * an indication of how long getting the time took.  Also, maintain _pbtActive
     * as true if we're still trying to get the current playback time.
     */
    [_pbtCond lock];
    while(1) {
	/* we need to increment this right before sleeping, since we need to see it
	 * increment if we saw active set, or if we woke up this task when it was 
	 * inactive.
	 */
	_pbtCounter++;

	_pbtActive = NO;
	[_pbtCond wait];
	_pbtActive = YES;
	_pbtStartMs = osp_get_ms();
	[_pbtCond unlock];

	NSLog(@"pbt task starts currrentPlaybackTime");
	newTime= [_mpPlayer currentPlaybackTime];
	endMs = osp_get_ms();
	NSLog(@"pbt task ends, latency=%d ms", endMs-_pbtStartMs);

	[_pbtCond lock];
	_pbtEndMs = endMs;
	_pbtLastTime = newTime;
	[_pbtCond broadcast];
    }
    /* should unlock, but not reached */
}

- (void) doAsyncCheck
{
    if(_state == 0) {
	/* do not update _currentState from player here, since player
	 * often is wrong, and we're just here to double check the value
	 * of _currentState anyway.
	 */
	_state = 1;
	_timer = [NSTimer scheduledTimerWithTimeInterval: _checkInterval
			  target:self
			  selector:@selector(timerFired:)
			  userInfo:nil
			  repeats: NO];
    }
}

/* callback made when Apple thinks the player changed state.  Report what it says, but
 * set a timer so we can probe the currentPlaybackTime two more times to see if the
 * player is really playing music.  We do it a little after the notification arrives
 * in case the player hasn't quite started yet (or stopped) at the time we receive
 * the notification.
 */
- (void) isPlayingChanged: (id) notification
{
    NSLog(@"- mp isplayingchanged currentState=%d", (int) [_mpPlayer playbackState]);
    /* always report this state, even if it is the same as we reported
     * last time (probably could suppress repeats).  We map everything
     * to either playing or paused states.
     */
    _currentState = [_mpPlayer playbackState];
    if (_currentState != MPMusicPlaybackStatePlaying)
	_currentState = MPMusicPlaybackStatePaused;

    /* indicate what we know so far; we'll update it again if we discover
     * after a couple of timer triggers that we're wrong.
     */
    if (_lastIndicated != _currentState) {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Warc-performSelector-leaks"
	_lastIndicated = _currentState;
	NSLog(@"- mp indicating %d", (int) _currentState);
	[_owner performSelector: _selector withObject: nil];
#pragma clang diagnostic pop
    }

    if (_state == 0) {
	_state = 1;
	_timer = [NSTimer scheduledTimerWithTimeInterval: _checkInterval
			  target:self
			  selector:@selector(timerFired:)
			  userInfo:nil
			  repeats: NO];
    }
    else {
	/* when we're done with the current state machine, force a recheck
	 * because we were notified in the middle of checking the state
	 * after a change notification.
	 */
	_forceRecheck = 1;
    }
}

/* one of our timers has fired; the first time a timer fires, we're in
 * state 1, and we just remember how far into the music we are.  Later
 * firings have us compare the current playing time with the previous
 * one.
 */
- (void) timerFired: (id) junk
{
    float errorBound;
    static const float checkInterval = 0.4;

    _timer = nil;

    if (!_enabled)
	return;

    if (_state == 1) {
	/* first timer event -- see how long the music has been
	 * playing, and then start the timer a 2nd time to see if it
	 * is still playing.
	 */
	_secondsIntoSong = [self pbtRecentTime: &errorBound];
	if (errorBound > checkInterval/3) {
	    /* failed to get a clean current time reading, go back to state 1 */
	    NSLog(@"- mp timer1 failed to read");
	}
	else {
	    _state = 2;
	}

	_timer = [NSTimer scheduledTimerWithTimeInterval: checkInterval
		      target:self
		      selector:@selector(timerFired:)
		      userInfo:nil
		      repeats: NO];
	NSLog(@"- mp timer1 seconds=%f state=%d", _secondsIntoSong, _state);
    }
    else if (_state > 1) {
	/* later timer has gone off -- see how long the music was playing */
	float songTime;
	float difference;
	MPMusicPlaybackState newState;

	songTime = [self pbtRecentTime: &errorBound];

	if ( errorBound > checkInterval/3) {
	    NSLog(@"- mp second read failed clean, going back to state 1");
	    _state = 1;
	    _timer = [NSTimer scheduledTimerWithTimeInterval: checkInterval
			      target:self
			      selector:@selector(timerFired:)
			      userInfo:nil
			      repeats: NO];
	    return;
	}

	difference = songTime - _secondsIntoSong;
	if (difference < 0)
	    difference = -difference;
	if (difference > 0.001) {
	    newState = MPMusicPlaybackStatePlaying;
	}
	else {
	    newState = MPMusicPlaybackStatePaused;
	}
	NSLog(@"- mp timer2 seconds=%f diff=%f newPlayerState=%d",
	      songTime, difference, (int) newState);

	/* OK, this is our real state */
	_currentState = newState;

	/* if the state is different from what we reported, indicate a state
	 * change again.
	 */
	if (_currentState != _lastIndicated) {
	    _lastIndicated = _currentState;

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Warc-performSelector-leaks"
	    NSLog(@"- mp indicating %d", (int) _currentState);
	    [_owner performSelector: _selector withObject: nil];
#pragma clang diagnostic pop
	}

	if (_state >= _maxState) {
	    /* we've finished the desired number of checks */
	    NSLog(@"- mp timer reached maxState=%d", _state);
	    _state = 0;

	    /* if we got an indication of a change while we were doing our timing of the
	     * current song, so start a new timer.
	     */
	    if (_forceRecheck) {
		_forceRecheck = 0;

		/* this will kick off the new state machine again, but we don't
		 * want to indicate the currently reported state from the player,
		 * since we *just* computed the correct state.
		 */
		_state = 1;
		_timer = [NSTimer scheduledTimerWithTimeInterval: checkInterval/2
				  target:self
				  selector:@selector(timerFired:)
				  userInfo:nil
				  repeats: NO];
		NSLog(@"- mp timer saw force recheck, new state 1");
	    }
	}
	else {
	    _state++;
	    _secondsIntoSong = songTime;	/* NB error bound on songTime checked above */
	    _timer = [NSTimer scheduledTimerWithTimeInterval: checkInterval
			      target:self
			      selector:@selector(timerFired:)
			      userInfo:nil
			      repeats: NO];
	    NSLog(@"- mp timer1 seconds=%f state=%d", _secondsIntoSong, _state);
	}
    }
}

- (void) setStateHint: (MPMusicPlaybackState) newState
{
    [_pbtCond lock];
    _currentState = newState;

    /* force an indication on any update, in case hint is wrong */
    _lastIndicated = (MPMusicPlaybackState) 100;

    [_pbtCond unlock];

    /* and make sure we validate the updated state */
    [self doAsyncCheck];
}

/* call this from your indicated callback to see the real current state */
- (MPMusicPlaybackState) stateWithForce: (BOOL) force
{
    /* return our best guess so far */
    if (force) {
	[self doAsyncCheck];
    }

    return _currentState;
}

- (MPMusicPlaybackState) state
{
    return [self stateWithForce: NO];
}

- (void) disable
{
    [_pbtCond lock];
    _enabled = 0;
    [_pbtCond unlock];
}

- (void) enable
{
    [_pbtCond lock];

    _state = 0;
    _enabled = 1;
    if (!_timer) {
	_timer = [NSTimer scheduledTimerWithTimeInterval: _checkInterval
			  target:self
			  selector:@selector(timerFired:)
			  userInfo:nil
			  repeats: NO];
    }

    [_pbtCond unlock];
}

@end
