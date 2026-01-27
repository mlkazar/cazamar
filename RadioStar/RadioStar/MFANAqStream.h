#import <UIKit/UIKit.h>
#import <Foundation/Foundation.h>
#import <MediaPlayer/MediaPlayer.h>

// One of these streams contains a sequence of AAC or MP3 records that
// can be randomly accessed to a good degree of precision, so that we
// can select specific song boundaries for saving files.
//
// Actual .aac or .mp3 file formats do not provide an easy way to seek
// to an arbitrary record, since given an audio file, the only record
// boundary you're guaranteed to be able to find is the one at the
// start of the file.  Thus, our file format will start new records at
// a known block boundary, and each record will be preceeded by a
// header giving the record's creation time and length.  The creation
// time will only be approximately useful, since it will represent the
// time at which the record was first received from the radio stream,
// and buffering affects may make it a poor approximation to the
// actual playing time for the record.
//
// Each file will start with a 64 byte header containing a magic
// number (32 bits), a header length (32 bits containing 64), the
// block size (32 bits), the start time for the file (64 bits)
// relative to the start of the first file created (even if it has
// been pruned), and the file's base position in the stream (64 bits).
//
// Each audio record is preceeded by a 16 bit record length (including
// this header), a 16 bit magic number (0x7711), and a 32 bit time
// stamp in ms relative to the start of the file.

// Reference counting between AqStream and its target is a little
// complicated.  When not shutdown, the AqStream keeps a reference
// from its RadioStream pthread to the AqStream, and a reference back
// to its AqStreamTarget.  When AqStream shutdown is called, we stop
// the RadioStream, which forces the pthread to exit quickly and drop
// the pthread's (and RadioStream's effective) reference to the
// AqStream.  It also shuts down the target, which releases the
// reference to the target.
//
// Because the AqStream performs upcalls to the target, it keeps a
// strong reference to the MFANAqStreamTarget.  When the shutdown:
// method is called, the stream object clears the strong back
// reference.  During the upcall, an extra local strong reference is
// maintained to the target, so that a shutdown won't free the target
// until any upcalls have completed.
//
// Upcalls come from the RadioStream's pthread, so the reference to
// the AqStream from the pthread ensure that the AqStream itself
// doesn't disappear if someone shuts down the AqStream while it is
// making an upcall to the AqStream target.
// 

@class MFANAqStream;

@interface MFANAqStreamTarget
- (int32_t) notify: (MFANAqStream *) stream;
- (int32_t) deliverPacket: (char *) data
		   length: (uint32_t) length
		   stream: (MFANAqStream *) stream;
@end

@interface MFANAqStreamPacket : NSObject
- (int32_t) addData: (char *) data descr: (AudioStreamPacketDescription *) descr;

- (char *) getData;

- (void) getDescr: (AudioStreamPacketDescription *) descr;

- (uint64_t) getLength;

- (uint64_t) getMs;
@end

@interface MFANAqStreamReader : NSObject
@property MFANAqStream *aqStream;

- (MFANAqStreamPacket *) read;

- (bool) hasData;

// returns absolute seek position.  If whence is 0, it is an
// absolute time.
- (uint64_t) seek:(uint64_t) ms whence: (int) whence;

// create new reader, position it at current end
- (MFANAqStreamReader *) initWithStream: (MFANAqStream *) stream;

- (void) close;
@end

@interface MFANAqStream : NSObject

@property NSMutableArray *packetArray;
@property uint64_t packetStreamVersion;

+ (pthread_mutex_t *) streamMutex;

- (pthread_cond_t *) packetArrayCv;

- (MFANAqStream *) initWithUrl: (NSString *) url;

- (NSString *) getDataFormatString;

- (NSString *) getUrl;

- (void) getDataFormat: (AudioStreamBasicDescription *) format;

// We can only have one target at a time, but we can attach and detach
// it without affecting the actual stream receiver.
- (int32_t) attachTarget: (MFANAqStreamTarget *) target;

- (void) detachTarget;

// When shutdown, the pthread that does the RadioStream work exits and
// releases its reference to the AqStream.
- (void) shutdown;

@end
