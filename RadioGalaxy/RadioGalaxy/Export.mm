#import "Export.h"

#import "ExportEntry.h"
#import "ExportSlider.h"
#import "HelpLabel.h"
#import "MFANAqStreamBuffer.h"
#import "MFANCGUtil.h"
#import "MFANCoreButton.h"
#import "MFANIconButton.h"
#import "MFANWarn.h"
#import "MarqueeLabel.h"
#import "SignView.h"
#import "TopView.h"
#import "ViewController.h"

#include "osp.h"
#include <pthread.h>

@implementation Export {
    ViewController *_vc;
    SignStation *_station;
    MFANAqStreamBuffer *_buffer;

    ExportSlider *_startSlider;
    ExportSlider *_endSlider;
    UIStepper *_stepper;
    long _selectedRow;
    HelpLabel *_startHelpLabel;
    HelpLabel *_endHelpLabel;

    // useful buttons
    MFANIconButton *_exportButton;
    MFANIconButton *_cancelButton;
    MFANIconButton *_populateSwitch;
    MFANIconButton *_doneButton;

    MarqueeLabel *_marquee;
    NSString *_currentLabel;

    MFANStreamPlayer *_samplePlayer;
    NSTimer *_sampleTimer;
    int64_t _sampleIndex;

    float _lastStepperValue;

#if 0
    id _callbackObj;
    SEL _callbackSel;
#endif

    NSThread *_scanThread;

    UIAlertController *_alert;

    ExportEntry *_entry;

    // We have a different UI while playing a full entry -- in playing
    // mode, only the start slider is visible, and current playing
    // time is reflected in the slider.
    bool _playingMode;			// true if we're in playing mode
}

static const float _kPlayDuration = 4.0;

#if 0
- (void) setCallback: (id) obj withSel: (SEL) sel {
    _callbackObj = obj;
    _callbackSel = sel;
}

- (void) doNotify {
    if (_didNotify)
	return;
    _didNotify = true;

    if (_callbackObj != nil) {
	[_callbackObj  performSelectorOnMainThread: _callbackSel
					withObject: nil
				     waitUntilDone: true];
    }
}
#endif

- (Export *) initWithStation: (SignStation *) station
		 exportEntry: (ExportEntry *) exportEntry
		    viewCont: (ViewController *) vc
{
    // we get the frame from the view controller
    CGRect buttonFrame;
    CGRect startSliderFrame;
    CGRect endSliderFrame;
    CGRect startLabelFrame;
    CGRect endLabelFrame;
    CGRect frame;

    self.frame = vc.activeFrame;
    frame = vc.activeFrame;

    self = [super initWithFrame: frame];
    if (self != nil) {
	_vc = vc;
	_station = station;

	_buffer = _station.recordingBuffer;
	osp_assert(_buffer != nil);

	_vc = vc;
	_entry = exportEntry;
	
	UIColor *labelColor = [UIColor colorWithRed: 0.8
					      green: 0.8
					       blue: 0.8
					      alpha: 1.0];
	// layout the station name, descr, URL, rate+type
	float boxHeight = frame.size.height * 0.06;
	float labelHeight = boxHeight;
	float okButtonWidth = labelHeight;
	float buttonWidth = frame.size.width * 0.8;
	float sliderLabelPct = 0.12;	// fraction used by the each slider's label
	float labelHeightFactor = 1.25;

	// 60% for the table
	// 6% for start slider
	// 6% for end slider
	// 6% for populate button
	// spare space
	// 6% for the cancel / export buttons

	// indent things so that we center the label and text box in
	//the frame.

	float viewOffset = 0.0;
	float viewHeight = 0.45 * frame.size.height;

	viewHeight = frame.size.height * .06;
	startSliderFrame = frame;
	startSliderFrame.origin.y = viewOffset;
	startSliderFrame.size.height = viewHeight;

	// and shrink to allow label
	startSliderFrame.origin.x = sliderLabelPct * frame.size.width;
	startSliderFrame.size.width = (1.0 - sliderLabelPct) * frame.size.width;

	_startSlider = [[ExportSlider alloc] initWithFrame: startSliderFrame
						    buffer: _buffer
						     apply: ^(float value) {
		[self startSliderChanged: value];
	    }
						  viewCont: _vc];
	[self addSubview: _startSlider];

	startLabelFrame = startSliderFrame;
	startLabelFrame.origin.x = 0;
	startLabelFrame.size.width = sliderLabelPct * frame.size.width;
	_startHelpLabel = [[HelpLabel alloc]
			      initWithFrame: startLabelFrame
				     target: self
				   selector: @selector(startHelp:)];
	[_startHelpLabel setTitle: @"Start"
			 forState: UIControlStateNormal];
	[self addSubview: _startHelpLabel];

	viewOffset += labelHeightFactor * viewHeight;
	viewHeight = labelHeight;
	endSliderFrame = frame;
	endSliderFrame.origin.y = viewOffset;
	endSliderFrame.size.height = viewHeight;

	// and shrink to allow label
	endSliderFrame.origin.x = sliderLabelPct * frame.size.width;
	endSliderFrame.size.width = (1.0 - sliderLabelPct) * frame.size.width;

	_endSlider = [[ExportSlider alloc] initWithFrame: endSliderFrame
						  buffer: _buffer
						   apply: ^(float value) {
		[self playTo: value];
	    }
						viewCont: _vc];
	[self addSubview: _endSlider];

	endLabelFrame = endSliderFrame;
	endLabelFrame.origin.x = 0;
	endLabelFrame.size.width = sliderLabelPct * frame.size.width;
	_endHelpLabel = [[HelpLabel alloc]
			    initWithFrame: endLabelFrame
				   target: self
				 selector: @selector(endHelp:)];
	[_endHelpLabel setTitle: @"End"
		      forState: UIControlStateNormal];
	[self addSubview: _endHelpLabel];

	CGRect marqueeFrame;
	viewOffset += labelHeightFactor * viewHeight;
	viewHeight = labelHeight;

	marqueeFrame = frame;
	marqueeFrame.origin.y = viewOffset;
	marqueeFrame.size.height = viewHeight;

	_marquee = [[MarqueeLabel alloc] initWithFrame: marqueeFrame];
	[_marquee setTextColor: [UIColor blackColor]];
	[_marquee setTextAlignment: NSTextAlignmentCenter];
	[_marquee setFont: [UIFont fontWithName: @"Arial-BoldMT" size: 30]];
	[_marquee setText: @"[Unknown]"];
	[self addSubview: _marquee];

	// add stepper button
	CGRect stepperFrame;
	_lastStepperValue = 0.0;
	viewOffset += labelHeightFactor * viewHeight;
	viewHeight = labelHeight;
	stepperFrame = frame;
	stepperFrame.origin.x = (frame.size.width - 100) / 2.0;
	stepperFrame.size.width = 100;
	stepperFrame.origin.y = viewOffset;
	stepperFrame.size.height = labelHeight;
	_stepper = [[UIStepper alloc] initWithFrame: stepperFrame];
	_stepper.minimumValue = -1000000.0;
	_stepper.maximumValue = 1000000.0;
	_stepper.stepValue = 1.0;
	_stepper.tintColor = [UIColor blueColor];
	_stepper.backgroundColor = [UIColor colorWithRed: 0.8
						   green: 0.8
						    blue: 0.8
						   alpha: 1.0];
	_stepper.layer.cornerRadius = 16.0;
	
	[_stepper addTarget: self
		     action:@selector(stepperChanged:)
	   forControlEvents:UIControlEventAllEvents];
	[self addSubview: _stepper];
	_stepper.value = 0.0;

	CGRect addButtonFrame;
	viewOffset += labelHeightFactor * viewHeight;
	viewHeight = labelHeight;
	addButtonFrame.origin.x = (frame.size.width - buttonWidth)/2.0;
	addButtonFrame.origin.y = viewOffset;
	addButtonFrame.size.height = labelHeight;
	addButtonFrame.size.width = buttonWidth;

	MFANCoreButton *addButton;
	addButton = [[MFANCoreButton alloc] initWithFrame: addButtonFrame
							 title: @"Border"
							 color: [UIColor blackColor]
					       backgroundColor: labelColor];
	[addButton setFillColor: [UIColor whiteColor]];
	[addButton setClearText: @"Export from range"];
	[addButton addCallback: self
			 withAction: @selector(exportRangePressed:)];
	[self addSubview: addButton];

	viewOffset += labelHeightFactor * viewHeight;
	viewHeight = labelHeight;

	// OK button
	buttonFrame.origin.y = frame.size.height - labelHeight;
	buttonFrame.origin.x = 2*frame.size.width/3 - okButtonWidth/2;
	buttonFrame.size.width = okButtonWidth;
	buttonFrame.size.height = labelHeight;
	_doneButton = [[MFANIconButton alloc] initWithFrame: buttonFrame
					      title: @"OK"
					      color: [UIColor colorWithHue: 0.3
							      saturation: 1.0
							      brightness: 1.0
							      alpha: 1.0]
					      file: @"icon-done.png"];
	[self addSubview: _doneButton];
	[_doneButton addCallback: self withAction:@selector(donePressed:)];

	// Cancel button
	buttonFrame.origin.x = frame.size.width/3 - okButtonWidth/2;
	_cancelButton = [[MFANIconButton alloc] initWithFrame: buttonFrame
					      title: @"Cancel"
					      color: [UIColor colorWithHue: 0.3
							      saturation: 1.0
							      brightness: 1.0
							      alpha: 1.0]
					      file: @"icon-cancel.png"];
	[self addSubview: _cancelButton];
	[_cancelButton addCallback: self withAction:@selector(donePressed:)];

	_playingMode = false;

	[self setBackgroundColor: [UIColor whiteColor]];

	[vc pushTopView: self];

	[self setupSliders];

	[self retrieveNameForEntry: _entry];
    }

    return self;
}

- (void) startSliderChanged: (float) value {
    if (self->_playingMode) {
	[self playSeek: value];
    } else {
	[self playFrom: value];
    }
}

- (void) enterPlayingMode {
    if (_playingMode)
	return;

    _playingMode = true;
    [_endSlider removeFromSuperview];
    [_startHelpLabel removeFromSuperview];
    [_endHelpLabel removeFromSuperview];
}

- (void) leavePlayingMode {
    if (!_playingMode)
	return;

    _playingMode = false;
    [self addSubview: _endSlider];
    [self addSubview: _startHelpLabel];
    [self addSubview: _endHelpLabel];
}

- (void) stepperChanged: (UIStepper *) stepper {
    NSLog(@"%f value", stepper.value);

    float diff = stepper.value - _lastStepperValue;
    NSLog(@"playingmode=%d val=%f lastVal=%f",
	  _playingMode, stepper.value, _lastStepperValue);
    _lastStepperValue = stepper.value;

    if (_playingMode) {
	// In playing mode, current position is constantly updated, so we
	// just add or subtract a fixed amount per step press.
	if (_samplePlayer != nil) {
	    uint64_t  currentTimestamp = [_samplePlayer getSeekTarget: 0.0];
	    NSLog(@" current timestamp=%f", currentTimestamp/1000.0);
	    // Note that a single tap will come in with a diff of 0.
	    if (diff >= 0)
		[ _startSlider setValue: (currentTimestamp / 1000.0)  + diff + 2.0];
	    else
		[ _startSlider setValue: (currentTimestamp / 1000.0)  + diff  - 2.0];
	}
    } else {
	if (_startSlider.lastTouchMs >= _endSlider.lastTouchMs) {
	    [ _startSlider setValue: [_startSlider getValue] + diff];
	} else {
	    [ _endSlider setValue: [_endSlider getValue] + diff];
	}
    }
}

- (void) retrieveNameForEntry: (ExportEntry *) entry {
    [_marquee setText: _entry.label];
}

#if 0
- (void) songCallback: (MFANAqStreamPacket *) packet {
    NSString *groupName;
    NSString *songName;
    NSString *albumName;
    NSString *song = packet.playingSong;
    ExportEntry *entry;

    [_marquee setText: song];
    _currentLabel = song;

    [ViewController splitLabel: song
			 group: &groupName
			  song: &songName
			 album: &albumName];

    uint32_t songIndex;

    if (_selectedRow >= 0)
	songIndex = (uint32_t) _selectedRow;
    else
	songIndex = 0;

    entry = _recordings[songIndex];
    float durationTime = entry.end - entry.start;

    [_vc updateNowPlayingCenter: song
		      baseImage: _station.iconImage
		    currentTime: 0.0
		       duration: (float) durationTime
		      songIndex: songIndex];

}
#endif

#if 0
- (void) retrieveNameAt: (float) time {
    uint64_t ms = (uint64_t) (time * 1000);
    NSString *song;

    song = [_buffer nameAt: ms];
    [_marquee setText: song];
}
#endif

- (void) playTo: (float) value {
    float playTarget;
    NSLog(@"playto %f", value);
    [self stopSample];

    if (value < _kPlayDuration)
	playTarget = 0.0;
    else
	playTarget = value - _kPlayDuration;

    NSLog(@"starting player");
    _samplePlayer = [[MFANStreamPlayer alloc]
			initWithStreamBuffer: _buffer
					  ms: (uint64_t) (playTarget * 1000)];
    _sampleTimer = [NSTimer scheduledTimerWithTimeInterval: _kPlayDuration
						    target: self
						  selector: @selector(stopSampleTimer:)
						  userInfo: nil
						   repeats: NO];
}

- (void) playIndex {
    ExportEntry *ep;

    [self stopSample];

    [self enterPlayingMode];

    ep = _entry;

    NSLog(@"playing at start=%f", ep.start);

    _samplePlayer = [[MFANStreamPlayer alloc]
			initWithStreamBuffer: _buffer
					  ms: (uint64_t) (ep.start * 1000.0)];

    _startSlider.value = ep.start;
    [_startSlider monitor: _samplePlayer];

    if (_sampleTimer != nil) {
	[_sampleTimer invalidate];
	_sampleTimer = nil;
    }

    float duration = ep.end - ep.start;
    _sampleTimer = [NSTimer scheduledTimerWithTimeInterval: duration
						    target: self
						  selector: @selector(stopSampleTimer:)
						  userInfo: nil
						   repeats: NO];
}

- (void) playSeek: (float) target {
    [self stopSample];

    _samplePlayer = [[MFANStreamPlayer alloc]
			initWithStreamBuffer: _buffer
					  ms: (uint64_t) target * 1000.0];
    [_startSlider monitor: _samplePlayer];

    if (_sampleTimer != nil) {
	[_sampleTimer invalidate];
	_sampleTimer = nil;
    }
}

- (void) stopSample {
    NSLog(@"in stopsample");

    if (_sampleTimer != nil) {
	[_sampleTimer invalidate];
	_sampleTimer = nil;
    }
    if (_samplePlayer != nil) {
	[_samplePlayer shutdown];
	_samplePlayer = nil;
    }

    // if we were in playing mode, we want to stop using that player's
    // current value.
    [_startSlider monitor: nil];
}

- (void) playFrom: (float) value {
    // make sure we stop anything already playing
    [self stopSample];

    NSLog(@"starting player");
    _samplePlayer = [[MFANStreamPlayer alloc]
			initWithStreamBuffer: _buffer
					  ms: (uint64_t) (value*1000)];
    _sampleTimer = [NSTimer scheduledTimerWithTimeInterval: _kPlayDuration
						    target:self
						  selector:@selector(stopSampleTimer:)
						  userInfo:nil
						   repeats: NO];
}

- (void) stopSampleTimer: (id) junk{
    NSLog(@"in stopsampletimer");
    [self leavePlayingMode];
    [self stopSample];
}

- (void) donePressed: (id) junk1 {
    [self stopSample];
    [self leavePlayingMode];
    [_vc popTopView];
}

- (void) setupSliders {
    ExportEntry *ep = _entry;

    [self leavePlayingMode];

    _startSlider.value = ep.start;
    _endSlider.value = ep.end;
}

- (void) tvActivate {
    // if the edit command did a remove, don't stay on the status
    // page, since the station doesn't exist anymore.
    [self leavePlayingMode];
    return;
}

- (void) startHelp: (id) junk {
}

- (void) endHelp: (id) junk {
}

- (void) promptFor: (NSString *) prompt
	   default:(NSString *) def
	   handler: (PromptContinuation) continueAt {
    UIAlertController *alert;
    alert = [UIAlertController alertControllerWithTitle: @"RadioGalaxy"
						message: prompt
					 preferredStyle: UIAlertControllerStyleAlert];

    [alert  addTextFieldWithConfigurationHandler:^(UITextField *textField) {
	    textField.text = def;
	    textField.placeholder = @"";
	    textField.secureTextEntry = NO; // Set to YES for passwords
	    NSLog(@"=6= setup %@", textField.text);
	}];

    UIAlertAction *action = [UIAlertAction actionWithTitle:@"Save"
						     style: UIAlertActionStyleDefault
						   handler:^(UIAlertAction *act) {
	    UITextField *field = alert.textFields.firstObject;
	    NSLog(@"=6=  %@ TODO call saveFile with right ep", field.text);
	    continueAt(field.text);
	    }];

    [alert addAction: action];

    action = [UIAlertAction actionWithTitle: @"Cancel"
				      style: UIAlertActionStyleDefault
				    handler:^(UIAlertAction *action) {
	}];
    [alert addAction: action];

    [_vc presentViewController: alert animated:YES completion: nil];
}

- (void) exportRangePressed: (id) junk {
    ExportEntry *ep;
    float startTime;
    float endTime;
    NSString *song;

    startTime = [_startSlider getValue];
    endTime = [_endSlider getValue];
    if (startTime >= endTime) {
	(void) [[TopAlert alloc]
		   initWithMessage: @"Start time (slider) must be earlier than end"
			  duration: 10.0
			  viewCont: _vc];
	return;
    }

    ep = [[ExportEntry alloc] initWithStartTime: startTime end: endTime];
    song = _entry.label;
    if ([song isEqualToString:@"[Unknown]"]) {
	song = @"";
    }
    ep.label = song;

    // I guess this is easier than building an entire screen to push
    // into the viewcontroller's stack, but I'm not sure.
#if 0
    [self promptFor: prompt default: song handler:^(NSString *value) {
	    NSLog(@"=6= in addrangepart2 with %@", value);
	    if ([value length] == 0)
		return;
	    ep.label = value;
	    [self->_recordings addObject: ep];
	    [self->_songTable reloadData];
	    [self saveFile: ep];
	}];
#endif
    int32_t code;
    code = [self saveFile: ep];

    (void) [[TopAlert alloc]
		   initWithMessage: (code == 0? @"File saved" : @"Failure saving file")
			  duration: 5.0
			  viewCont: _vc];
}

- (void) tvDeactivate {
    [_startSlider shutdown];
    _startSlider = nil;
    [_endSlider shutdown];
    _endSlider = nil;
    return;
}

- (bool) tvOk2Quit {
    return false;
}

#if 0
- (void) splitLabel: (NSString *) label
	      group: (NSString **) group
	       song: (NSString **) song
	      album: (NSString **) album {

    NSArray<NSString *> *parsed = [label componentsSeparatedByString: @"-"];
    uint64_t parsedCount = [parsed count];
    if (parsedCount == 0) {
	*group = @"Unknown";
	*song = @"Unknown";
	*album = @"Unknown";
    } else if (parsedCount == 1) {
	*group = @"Unknown";
	*song = [parsed[0] stringByTrimmingCharactersInSet:
			   NSCharacterSet.whitespaceCharacterSet];
	*album = @"";

    } else if (parsedCount == 2) {
	*group = [parsed[0] stringByTrimmingCharactersInSet:
			   NSCharacterSet.whitespaceCharacterSet];
	*song = [parsed[1] stringByTrimmingCharactersInSet:
			   NSCharacterSet.whitespaceCharacterSet];
	*album = @"";

    } else {
	*group = [parsed[0] stringByTrimmingCharactersInSet:
			    NSCharacterSet.whitespaceCharacterSet];
	*song = [parsed[1] stringByTrimmingCharactersInSet:
			   NSCharacterSet.whitespaceCharacterSet];
	*album = [parsed[2] stringByTrimmingCharactersInSet:
			    NSCharacterSet.whitespaceCharacterSet];
    }
}
#endif

- (NSString *) generateName: (ExportEntry *) ep isMp3: (bool) isMp3 {
    NSString *entryName;
    NSArray<NSString *> *parsed = [ep.label componentsSeparatedByString: @"-"];
    uint64_t parsedCount = [parsed count];

    if (parsedCount == 0)
	entryName = @"Unknown";
    else if (parsedCount == 1) {
	entryName = [parsed[0] stringByTrimmingCharactersInSet:
			       NSCharacterSet.whitespaceCharacterSet];
    } else {
	// in 2 element, it is group-song, in 3 element it is
	// group-song-album.
	entryName = [parsed[1] stringByTrimmingCharactersInSet:
			       NSCharacterSet.whitespaceCharacterSet];
    }
    entryName = [entryName stringByAppendingString: (isMp3? @".mp3" : @".aac")];
    entryName = [entryName stringByReplacingOccurrencesOfString: @"/" withString: @"-"];
    return fileNameForDoc(entryName);
}

- (int32_t) writeId3V2ToFile: (FILE *) filep
		    entry: (ExportEntry *) ep {

    NSString *groupName;
    NSString *songName;
    NSString *albumName;

    [ViewController splitLabel: ep.label
			 group: &groupName
			  song: &songName
			 album: &albumName];

    ExportId3V2 *id3Writer = [[ExportId3V2 alloc] initWithGroup: groupName
							   song: songName
							  album: albumName];
    NSData *idData = [id3Writer getId];
    uint64_t count = [idData length];
    const char *datap = (const char *) idData.bytes;
    int64_t code = fwrite(datap, 1, count, filep);
    if (code == count)
	return 0;
    else
	return -1;
}

- (void) finishMp3File: (FILE *) filep
		 entry: (ExportEntry *) ep {
    char tbuffer[130];
    char *tp;
    NSString *groupName;
    NSString *songName;
    NSString *albumName;

    [ViewController splitLabel: ep.label
			 group: &groupName
			  song: &songName
			 album: &albumName];

    /* put out old style MP3 trailer (easiest to do) */
    memset(tbuffer, 0, sizeof(tbuffer));
    tp = tbuffer;

    *tp++ = 'T';	/* id3v1 tag */
    *tp++ = 'A';
    *tp++ = 'G';

    strncpy(tp, [songName cStringUsingEncoding: NSUTF8StringEncoding], 30);
    tp += 30;
    strncpy(tp, [groupName cStringUsingEncoding: NSUTF8StringEncoding], 30);
    tp += 30;
    strncpy(tp, [albumName cStringUsingEncoding: NSUTF8StringEncoding], 30);
    tp += 30;

    /* write out recordingYear */
    uint32_t recordingYear = 2026;
    *tp++ = (recordingYear/1000) + '0';	/* fails at year 10000 */
    *tp++ = ((recordingYear/100) % 10) + '0';
    *tp++ = ((recordingYear/10) % 10) + '0';
    *tp++ = (recordingYear % 10) + '0';

    /* comment */
    strcpy(tp, "Saved by RadioGalaxy");
    tp += 30;

    *tp++ = 92;	// prog rock

    /* write out MP3 v1 tag */
    fwrite(tbuffer, 1, 128, filep);
}

- (int32_t) removeFile: (ExportEntry *) ep {
    NSString *fileName;
    AudioStreamBasicDescription dataFormat;
    bool isMp3;

    [_buffer getDataFormat: &dataFormat];
    isMp3 = (dataFormat.mFormatID == '.mp3');

    fileName = [self generateName: ep isMp3: isMp3];

    bool success = [[NSFileManager defaultManager] removeItemAtPath: fileName
							      error: nil];
    if (success) {
	ep.saved = false;
	return 0;
    } else {
	return -1;
    }
}

#if 0
- (void) monitorScan {
    _alert = [UIAlertController
		 alertControllerWithTitle: @"Scanning recordings"
				  message: @"Starting"
			   preferredStyle: UIAlertControllerStyleAlert];

    UIAlertAction *action = [UIAlertAction actionWithTitle:@"Cancel"
                                                     style: UIAlertActionStyleCancel
                                                   handler:^(UIAlertAction *act) {
	    self->_populateCanceled = true;
	}];

    [_alert addAction: action];

    [_vc presentViewController: _alert animated:YES completion: nil];

    _populateTimer = [NSTimer scheduledTimerWithTimeInterval: 0.5
						      target:self
						    selector:@selector(monitorUpdate:)
						    userInfo:nil
						     repeats: YES];
}

- (uint64_t) getCurrentIx {
    ExportEntry *entry;
    uint64_t ix;
    uint64_t count;

    count = [_recordings count];
    for(ix = 0; ix<count; ix++) {
	entry = _recordings[ix];
	if ([entry.label isEqualToString: _currentLabel]) {
	    return ix;
	}
    }
    return 0;
}
#endif

- (bool) tvPlayPauseSong {
    if (_playingMode) {
	if (_samplePlayer != nil) {
	    [self stopSample];
	} else {
	    [self playIndex];
	}
    }

    return false;
}

- (int32_t) saveFile: (ExportEntry *) ep {
    const char *fileNamep;
    NSString *fileName;
    FILE *filep = nullptr;
    MFANAqStreamReader *reader;
    MFANAqStreamPacket *p;
    AudioStreamBasicDescription dataFormat;
    bool isMp3;
    uint64_t bytesWritten;
    uint64_t byteCount;
    uint64_t packetSize;
    uint64_t endMs;
    int64_t code;
    NSMutableData *adtsHeader;

    [_buffer getDataFormat: &dataFormat];
    isMp3 = (dataFormat.mFormatID == '.mp3');

    reader = [[MFANAqStreamReader alloc]
		  initWithBuffer: _buffer];
    [reader seek: (uint64_t) (ep.start * 1000) whence: 0];
    reader.noWait = true;

    fileName = [self generateName: ep isMp3: isMp3];
    fileNamep = [fileName cStringUsingEncoding: NSUTF8StringEncoding];

    filep = fopen(fileNamep, "w");
    if (filep == nullptr) {
	return -1;
    }

    endMs = (uint64_t)(ep.end * 1000);

    // officially .aac files aren't support to have ID3 tags.
    [self writeId3V2ToFile: filep entry:ep];

    if (!isMp3 && !_station.warnedAac) {
	_station.warnedAac = true;
	(void) [[TopAlert alloc]
		   initWithMessage: @"Saving .aac file.  Can convert to .m4a with:\n"
		   @"ffmpeg -i foo.aac -c:a copy foo.m4a"
			  duration: 10.0
			  viewCont: _vc];
    }

    while(true) {
	p = [reader read];
	if (p == nil)
	    break;
	if (p.startMs >= endMs)
	    break;

	packetSize = [p getLength];

	// if AAC file, write out the ADTS header, which we retrieve from
	// the buffer.
	if (!isMp3) {
	    // must be aac
	    adtsHeader = [_buffer getAdtsHeaderForLength: (int32_t) packetSize];
	    byteCount = adtsHeader.length;
	    bytesWritten = fwrite([adtsHeader bytes], 1, byteCount, filep);
	    if (bytesWritten != byteCount)
		return -1;
	}

	bytesWritten = fwrite([p getData], 1, packetSize, filep);
	if (bytesWritten != packetSize) {
	    [reader close];
	    return -1;
	}
    }

    code = fflush(filep);
    fsync(fileno(filep));
    code = fclose(filep);

    ep.saved = true;

    return 0;
}

- (bool) tvIsPlaying {
    if (_samplePlayer == nil || ![_samplePlayer isPlaying])
	return false;
    else
	return true;
}

- (bool) tvPause {
    if (_samplePlayer != nil)
	[_samplePlayer pause];

    return false;
}

- (bool) tvResume {
    if (_samplePlayer != nil)
	[_samplePlayer resume];

    return false;
}

@end

@implementation ExportId3V2 {
    NSMutableData *_buffer;
}

- (uint64_t) frameSizeForString: (NSString *) ins {
    // we don't even append frames for empty strings
    if ([ins length] == 0)
	return 0;

    // 4 bytes frame ID, 4 bytes size, 2 bytes flags, 1 byte encoding,
    // one byte for terminating null in UTF-8 data (since we're
    // writing version 2.4 tags).
    return 12 + [ins length];
}

- (void) appendSyncSafe: (uint64_t) val {
    char tdata[4];
    tdata[0] = (val >> 21) & 0x7F;
    tdata[1] = (val >> 14) & 0x7F;
    tdata[2] = (val >> 7) & 0x7F;
    tdata[3] = val & 0x7F;
    [_buffer appendBytes: tdata length: 4];
}

- (void) appendFrameType: (const char *) type
		   value: (NSString *) value {

    const char *valueString = [value cStringUsingEncoding: NSUTF8StringEncoding];

    // NSString length may not count multibyte characters as N bytes.
    // the valueLength does include the null termination required for
    // C strings and id3 v2.4 string tags.
    uint64_t valueLength = strlen(valueString) + 1;

    // we don't even put out empty frames.
    if (valueLength <= 1)
	return;

    // the frame size includes the encoding byte (0x03 for UTF8), the
    // contents and a terminating null (already counted in
    // valueLength).
    uint64_t frameSize = valueLength + 1;
    [_buffer appendBytes: type length: 4];
    [self appendSyncSafe: frameSize];

    uint32_t zeroData = 0;

    // append flags
    [_buffer appendBytes: &zeroData length: 2];

    char encoding = 0x03;
    [_buffer appendBytes: &encoding length:  1];

    // valueString and valueLength both include the null terminating
    // byte required for v2.4 string frames.
    [_buffer appendBytes: valueString length: valueLength];
}

- (ExportId3V2 *) initWithGroup: (NSString *) group
			   song: (NSString *) song
			  album: (NSString *) album {
    self = [super init];
    if (self != nil) {
	_buffer = [[NSMutableData alloc] initWithCapacity: 256];

	char header[6];
	header[0] = 'I';
	header[1] = 'D';
	header[2] = '3';
	header[3] = 4;		// version 2.4.0
	header[4] = 0;
	header[5] = 0;		// flags
	[_buffer appendBytes: header length: sizeof(header)];

	// the size field doesn't include the 10 byte header (the 6 bytes above
	// plus the length of the entire tag.
	uint64_t totalLength;
	totalLength = [self frameSizeForString: group] + [self frameSizeForString: song] +
	    [self frameSizeForString: album];
	[self appendSyncSafe: totalLength];

	[self appendFrameType: "TIT2" value: song];
	[self appendFrameType: "TPE1" value: group];
	[self appendFrameType: "TALB" value: album];
    }
    return self;
}

- (NSData *) getId {
    return _buffer;
}
@end
