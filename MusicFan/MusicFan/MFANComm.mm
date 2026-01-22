#import "MFANTopLevel.h"
#import "MFANPlayContext.h"
#import "MFANPlayerView.h"
#import "MFANSetList.h"
#import "MFANIndicator.h"
#import "MFANComm.h"

#include "rpc.h"
#include "mfclient.h"
#include "osp.h"

uint64_t
osp_get_ms()
{
    return osp_time_ms();
}

uint64_t
osp_get_sec()
{
    return osp_time_sec();
}

@implementation MFANCommEntry {
    /* _who, _song, _artist are all properties */
}

- (MFANCommEntry *) initWithWho: (NSString *) who
			   song: (NSString *) song
			 artist: (NSString *) artist
		       songType: (uint32_t) songType
			    acl: (uint32_t) acl
{
    self = [super init];
    if (self) {
	_who = who;
	_song = song;
	_artist = artist;
	_songType = songType;
	_acl = acl;
    }

    return self;
}

@end

@implementation MFANComm {
    Rpc *_rpcp;

    /* one client can only do a single RPC at a time, so make sure we have a separate
     * one for each potential concurrent call.
     */
    MfClient *_mfClientp;

    /* for coordination with the main task, so it never waits for an RPC
     * in either direction.
     */
    NSThread *_commThread;
    NSCondition *_commLock;

    /* set if announce variables have been set but not yet sent to the aggregator
     * process.
     */
    uint8_t _pendingAnnounce;
    uint8_t _pendingGetAll;

    NSString *_announceWho;
    NSString *_announceSong;
    NSString *_announceArtist;
    uint32_t _announceType;
    uint32_t _announceAcl;

    dqueue<MfEntry> _entries;
}

- (MFANComm *) init
{
    self = [super init];
    if (self) {
	_pendingAnnounce = NO;
	_pendingGetAll = NO;
	_announceWho = nil;
	_announceSong = nil;
	_announceArtist = nil;
	_announceType = 0;
	_announceAcl = 0;
	_commLock = [[NSCondition alloc] init];
	
	_commThread = [[NSThread alloc] initWithTarget: self
					selector: @selector(bkgInit:)
					object: nil];
	[_commThread start];
    }

    return self;
}

+ (uint32_t) aclAll
{
    return MfClient::_aclAll;
}

- (int32_t) getAllPlaying: (NSMutableArray *) playing
{
    MfEntry *ep;
    MfEntry *nep;

    [_commLock lock];
    _pendingGetAll = YES;
    [_commLock broadcast];

    for(ep = _entries.head(); ep; ep=nep) {
	nep = ep->_dqNextp;

	[playing addObject: [[MFANCommEntry alloc]
				initWithWho: [[NSString alloc] initWithCString: ep->_whop
							       encoding: NSUTF8StringEncoding]
				song: [[NSString alloc] initWithCString: ep->_songp
							encoding: NSUTF8StringEncoding]
				artist: [[NSString alloc] initWithCString: ep->_artistp
							  encoding: NSUTF8StringEncoding]
				songType: ep->_songType
				acl: ep->_acl]];
    }
    [_commLock unlock];

    return 0;
}

- (void) announceWho: (NSString *) who
		song: (NSString *) song
	      artist: (NSString *) artist
	    songType: (uint32_t) songType
		 acl: (uint32_t) acl
{
    NSLog(@"- Announcement queue start");
    [_commLock lock];
    _announceWho = who;
    _announceSong = song;
    _announceArtist = artist;
    _announceType = songType;
    _announceAcl = acl;
    _pendingAnnounce = YES;
    [_commLock broadcast];
    [_commLock unlock];
    NSLog(@"- Announcement queue done");
}

- (void) bkgInit: (id) arg
{
    NSString *who;
    NSString *song;
    NSString *artist;
    char *whop;
    char *songp;
    char *artistp;
    dqueue<MfEntry> entries;
    uint32_t acl;
    uint32_t songType;

    whop = nil;
    songp = nil;
    artistp = nil;

    _rpcp = new Rpc();
    _mfClientp = new MfClient(_rpcp);
    _mfClientp->init();

    while(1) {
	[_commLock lock];

	if (_pendingAnnounce) {
	    _pendingAnnounce = NO;
	    /* copy to local string since string may be freed once we drop commLock */
	    who = _announceWho;
	    song = _announceSong;
	    artist = _announceArtist;
	    songType = _announceType;
	    acl = _announceAcl;
	    [_commLock unlock];

	    whop = (char *) [who cStringUsingEncoding: NSUTF8StringEncoding];
	    songp = (char *) [song cStringUsingEncoding: NSUTF8StringEncoding];
	    artistp = (char *) [artist cStringUsingEncoding: NSUTF8StringEncoding];
	    NSLog(@"- MFANComm: starting announce of %s", whop);
#if 0
	    _mfClientp->announceSong(whop, songp, artistp, songType, acl);
#endif
	    NSLog(@"- MFANComm: announce done");
	    [_commLock lock];
	}

#if 0
	if (_pendingGetAll) {
	    int32_t code;
	    MfEntry *ep;
	    MfEntry *nep;
	    _pendingGetAll = NO;

	    [_commLock unlock];
	    NSLog(@"- MFANComm: start getAllPlaying");
	    code = _mfClientp->getAllPlaying(10, &entries);
	    [_commLock lock];

	    for(ep = _entries.head(); ep; ep=nep) {
		nep = ep->_dqNextp;
		delete ep;
	    }

	    _entries.init();
	    _entries.concat(&entries);
	}
#endif

	whop = nil;
	songp = nil;
	artistp = nil;
	[_commLock wait];
	[_commLock unlock];
    }
}

@end
