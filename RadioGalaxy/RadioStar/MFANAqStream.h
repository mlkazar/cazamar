#import <UIKit/UIKit.h>
#import <Foundation/Foundation.h>
#import <MediaPlayer/MediaPlayer.h>

#import "MFANAqStreamBuffer.h"

// MFANAqStreamPacket and MFANAqStreamBuffer are declared in
// MFANAqStreamBuffer.h (imported above).

// ---------------------------------------------------------------------------
// Reference-counting / lifecycle notes
//
// MFANAqStream owns the RadioStream download thread and an MFANAqStreamBuffer
// that accumulates the decoded audio packets.  The buffer's lifetime is
// independent: callers may retain the buffer after releasing the stream and
// continue reading already-downloaded data.
//
// When -shutdown is called the download thread is stopped, the audio-stream
// parser is closed, and buffer.downloadComplete is set to YES so that readers
// at the end of the buffer return nil rather than blocking for more data.
// The buffer (and its packet array) is NOT freed until all holders of the
// buffer reference release it.
//
// MFANAqStreamReader holds a strong reference to MFANAqStreamBuffer.
// During upcalls from the RadioStream pthread the stream holds an extra local
// strong reference to itself so it cannot be freed mid-callback.
// ---------------------------------------------------------------------------

@class MFANAqStreamBuffer;

// ---------------------------------------------------------------------------
// MFANAqStream
//
// Downloads a radio stream from a URL, parses the incoming AAC/MP3 data, and
// appends decoded packets to its MFANAqStreamBuffer.
// ---------------------------------------------------------------------------

@interface MFANAqStream : NSObject

// The buffer that accumulates decoded packets.  Callers may retain this
// reference beyond the lifetime of the MFANAqStream itself.
@property (readonly) MFANAqStreamBuffer *buffer;

// YES once shutdown has been initiated on the downloader.
// (For compatibility with callers such as SignStation that check this flag.)
@property bool shuttingDown;

// Convenience forwarder so that callers compiled against the old interface
// (e.g. MFANStreamPlayer) can still read stream.packetDuration directly.
@property (readonly) float packetDuration;

- (MFANAqStream *) initWithUrl: (NSString *) url buffer:(MFANAqStreamBuffer *) buffer;

- (NSString *) getDataFormatString;

- (NSString *) getPublicUrl;

- (NSString *) getFinalUrl;

- (void) getDataFormat: (AudioStreamBasicDescription *) format;

- (void) setFailureCallback: (id) callbackObj sel: (SEL) callbackSel;

// Stops the download thread and marks the buffer as downloadComplete.
// After this returns, the buffer and its packets remain accessible to
// existing readers until they release their reference to the buffer.
- (void) shutdown;

@end
