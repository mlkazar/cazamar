//
//  MFANPlayContext.m
//  MusicFan
//
//  Created by Michael Kazar on 4/26/14.
//  Copyright (c) 2014 Mike Kazar. All rights reserved.
//

#import <MediaPlayer/MediaPlayer.h>
#import "MFANTopLevel.h"
#import "MFANRButton.h"
#import "MFANPlayContext.h"
#import "MFANPlayerView.h"
#import "MFANSetList.h"
#import "MFANIndicator.h"
#import "MFANDownload.h"

#include "xgml.h"
#include "rpc.h"
#include "mfclient.h"

#include <string>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <inttypes.h>
#include <sys/stat.h>

@implementation MFANPlayContext {
    MFANTopLevel *_callback;
    SEL _action;
    MFANTopLevel *_parentView;
    CGRect _artFrame;
    NSNumber *_hitData;
    MFANPlayerView *_myPlayerView;
    MFANSetList *_setList;
    BOOL _isSel;
    BOOL _didLoadQuery;
    uint64_t _mtime;

    /* state for communicating with asynchronous state saving task */
    NSThread *_saveThread;
    NSCondition *_saveLock;
    Xgml::Node *_saveNode;

    /* when we load the state from a file, we load the query and set of ids,
     * and load them into the player when we send a play message to this
     * object.
     */
    NSMutableArray *_queryInfo;		/* of MFANScanItem objects */
    NSMutableArray *_idsToLoad;		/* of NSNumbers of IDs, or MFANMediaItems */

    /* when we've been playing and are switched away from,
     * currentIndex and _currentTime give the current state.  If we
     * haven't started playing yet and thus haven't loaded the query,
     * (didLoadQuery is false), we use playIndex and playOffset
     * instead, which come from the original restore state file.
     */
    long _playIndex;
    float _playOffset;

    NSTimeInterval _lastSavedTime;
    MFANDownload *_download;

    /* _currentIndex is property */
    /* _currentTime is property */
}

static const int _maxFileSize = 1024*1024;

- (void) setSelected: (BOOL) isSel
{
    _isSel = isSel;
}

- (BOOL) selected
{
    return _isSel;
}

- (MFANSetList *) setList
{
    return _setList;
}

- (long) playIndex
{
    return _playIndex;
}

- (MFANDownload *) download
{
    return _download;
}

- (void) setSetList: (MFANSetList *) setList
{
    _setList = setList;
}

- (void) setCallback: (MFANTopLevel *) callback withAction: (SEL) action
{
    _callback = callback;
    _action = action;
}

- (MFANPlayContext *) initWithButton: (MFANRButton *) button
		      withPlayerView: (MFANPlayerView *) playerView
			    withView: (MFANTopLevel *) parentView
			    withFile: (NSString *) assocFile
{
    self = [super init];
    if (self) {
	_mtime = 0;
	_myPlayerView = playerView;
	_parentView = parentView;
	_setList = [[MFANSetList alloc] init];
	_currentTime = 0.0;
	_currentIndex = -1;
	_associatedFile = assocFile;
	_queryInfo = [[NSMutableArray alloc] init];
	_saveNode = NULL;
	_saveLock = [[NSCondition alloc] init];
	_lastSavedTime = 0.0;
	_didLoadQuery = NO;
	_saveThread = [[NSThread alloc] initWithTarget: self
					selector: @selector(saveInit:)
					object: nil];
	[_saveThread start];

	_download = [[MFANDownload alloc] initWithPlayContext: self];

	[self restoreListFromFile];
    }

    return self;
}

- (void) play
{
    long newIndex;
    float newTime;

    if (!_didLoadQuery) {
	_didLoadQuery = YES;

	/* load playlist info sans current position info into setList */
	[_setList setQueryInfo: _queryInfo ids: _idsToLoad];

	/* and set the currentItemp and currentTime values, which were probably
	 * loaded from the playlist save file.
	 */
	_currentIndex = _playIndex;
	_currentTime = _playOffset;

	[self play];
	return;
    }

    /* pull the evaluated item collection out and stick it in the player; this doesn't
     * set the current song yet.
     */
    if (_currentIndex < 0)
	_currentIndex = _playIndex;

    if (_currentIndex < 0)
	_currentIndex = 0;

    /* save these, since setting the player's queue will update our playContext state */
    newIndex = _currentIndex;
    newTime = _currentTime;

    /* stop the player since we can't set the song while we're playing */
    [_myPlayerView pause];

    /* important to call this even if empty, since it also sets back pointer
     * from player to active context.
     */
    [_myPlayerView setQueueWithContext: self];

    /* set the current song now, and seek to the right position */
    [_myPlayerView setIndex: newIndex rollForward: YES];

    /* don't overwrite playbackTime set by setIndex, for items that track
     * their own time instead of using the playContext's position.
     */
    if ( ![[_myPlayerView currentMFANItem] trackPlaybackTime]) {
	[_myPlayerView setPlaybackTime: newTime];
    }

    /* and start playing */
    [_myPlayerView play];
}

- (void) pause
{
    long currentIndex;

    currentIndex = [_myPlayerView currentIndex];
    [_myPlayerView pause];
    if (currentIndex >= 0) {
	_currentIndex = currentIndex;
	_currentTime = [_myPlayerView currentPlaybackTime];
    }
}

- (float) currentPlaybackTime
{
    return [_myPlayerView currentPlaybackTime];
}

- (uint64_t) mtime
{
    return _mtime;
}

/* format is a version number (1), followed by a quoted string giving
 * the search string, followed by the srand seed for the playlist
 * (always 0 right now), followed by a count of the # of media items
 * in the playlist, followed by that many persistent IDs, followed by
 * the integer index of the song in the playlist, followed by a
 * floating point number giving the # of seconds into the song.
 */
- (void) restoreListFromFile
{
    FILE *filep;
    Xgml *xgmlp;
    Xgml::Node *rootNodep;
    Xgml::Attr *attrp;
    Xgml::Node *nodep;
    Xgml::Node *scanRoot;
    Xgml::Node *mediaRoot;
    char *tbufferp;
    char *origBufferp;
    int tc;
    int bytesRead;
    char *tp;
    NSString *queryStringp;
    unsigned long playIndex;
    float playOffset;
    int32_t code;
    MFANScanItem *scanItem;
    int overflow;
    struct stat tstat;

    xgmlp = new Xgml();
    rootNodep = NULL;

    code = stat([_associatedFile cStringUsingEncoding: NSUTF8StringEncoding], &tstat);
    if (code<0) {
	return;
    }
    _mtime = (((uint64_t) tstat.st_mtimespec.tv_sec) << 30) + tstat.st_mtimespec.tv_nsec;

    filep = fopen([_associatedFile cStringUsingEncoding: NSUTF8StringEncoding], "r");
    if (!filep)
	return;
    origBufferp = tbufferp = (char *) malloc(_maxFileSize);
    bytesRead = 0;
    tp = tbufferp;
    overflow = 0;
    while (1) {
	if (bytesRead >= _maxFileSize-1) {
	    overflow = 1;
	    break;
	}
	tc = fgetc(filep);
	if (tc < 0) {
	    break;
	}
	*tp++ = tc;
	bytesRead++;
    }
    if (overflow) {
	return;
    }
    *tp++ = 0;

    fclose(filep);
    filep = NULL;

    code = xgmlp->parse(&tbufferp, &rootNodep);
    if (code) {
	NSLog(@"!restoreFile faild to parse '%s'", origBufferp);
	return;
    }

    /* pull out the query string */
    queryStringp = NULL;
    playIndex = 0;
    playOffset = 0.0;
    for(attrp = rootNodep->_attrs.head(); attrp; attrp = attrp->_dqNextp) {
	if (strcmp(attrp->_name.c_str(), "playIndex") == 0) {
	    code = sscanf(attrp->_value.c_str(), "%ld", &playIndex);
	}
	else if (strcmp(attrp->_name.c_str(), "playTime") == 0) {
	    code = sscanf(attrp->_value.c_str(), "%f", &playOffset);
	}
    }

    scanRoot = NULL;
    mediaRoot = NULL;
    for(nodep = rootNodep->_children.head(); nodep; nodep=nodep->_dqNextp) {
	if (strcmp(nodep->_name.c_str(), "scanItems") == 0) {
	    scanRoot = nodep;
	}
	else if (strcmp(nodep->_name.c_str(), "mediaItems") == 0) {
	    mediaRoot = nodep;
	}
    }

    if (scanRoot != NULL) {
	_queryInfo = [[NSMutableArray alloc] init];
	for(nodep = scanRoot->_children.head(); nodep; nodep=nodep->_dqNextp) {
	    scanItem = [[MFANScanItem alloc] init];
	    for(attrp = nodep->_attrs.head(); attrp; attrp=attrp->_dqNextp) {
		/* parse title, scanFlags, minStars, cloud */
		if (strcmp(attrp->_name.c_str(), "title") == 0) {
		    scanItem.title = [NSString stringWithCString: attrp->_value.c_str()
					       encoding:NSUTF8StringEncoding];
		}
		else if (strcmp(attrp->_name.c_str(), "secondaryKey") == 0) {
		    scanItem.secondaryKey = [NSString stringWithCString: attrp->_value.c_str()
						      encoding:NSUTF8StringEncoding];
		}
		else if (strcmp(attrp->_name.c_str(), "stationDetails") == 0) {
		    scanItem.stationDetails = [NSString stringWithCString: attrp->_value.c_str()
								 encoding:NSUTF8StringEncoding];
		}
		else if (strcmp(attrp->_name.c_str(), "stationUrl") == 0) {
		    scanItem.stationUrl = [NSString stringWithCString: attrp->_value.c_str()
							     encoding:NSUTF8StringEncoding];
		}
		else if (strcmp(attrp->_name.c_str(), "scanFlags") == 0) {
		    scanItem.scanFlags = atoi(attrp->_value.c_str());
		}
		else if (strcmp(attrp->_name.c_str(), "minStars") == 0) {
		    scanItem.minStars = atoi(attrp->_value.c_str());
		}
		else if (strcmp(attrp->_name.c_str(), "cloud") == 0) {
		    scanItem.cloud = atoi(attrp->_value.c_str());
		}
		else if (strcmp(attrp->_name.c_str(), "podcastDate") == 0) {
		    scanItem.podcastDate = atoi(attrp->_value.c_str());
		}
	    }

	    [_queryInfo addObject: scanItem];
	}
    }

    /* if we've been passed songs or URLs to play explicitly, generate an array of
     * a mix of NSNumbers (for things in the iTunes library) and NSStrings (for URLs)
     * that we want to play.
     */
    if (mediaRoot != NULL) {
	_idsToLoad = [[NSMutableArray alloc] init];
	for(nodep = mediaRoot->_children.head(); nodep; nodep=nodep->_dqNextp) {
	    if ((attrp = nodep->_attrs.head()) != NULL) {
		if (strcmp(attrp->_name.c_str(), "id") == 0) {
		    NSNumber *number;
		    unsigned long long id;

		    id = strtoll(attrp->_value.c_str(), NULL, 10);
		    number = [NSNumber numberWithUnsignedLongLong: id];
		    [_idsToLoad addObject: number];
		}
		else {
		    MFANMediaItem *urlItem;
		    NSString *url;
		    NSString *urlRemote;
		    NSString *urlArtwork;
		    NSString *title;
		    NSString *albumTitle;
		    NSString *details;
		    long podcastDate;
		    int mustDownload;
		    float playbackTime;

		    title = @"";
		    albumTitle = @"";
		    details = nil;
		    podcastDate = 0;
		    playbackTime = 0.0;
		    mustDownload = -1;
		    for(attrp = nodep->_attrs.head(); attrp; attrp = attrp->_dqNextp) {
			if (strcmp(attrp->_name.c_str(), "url") == 0) {
			    url = [NSString stringWithCString: attrp->_value.c_str()
					    encoding:NSUTF8StringEncoding];
			}
			else if (strcmp(attrp->_name.c_str(), "urlRemote") == 0) {
			    urlRemote = [NSString stringWithCString: attrp->_value.c_str()
						  encoding:NSUTF8StringEncoding];
			}
			else if (strcmp(attrp->_name.c_str(), "urlArtwork") == 0) {
			    urlArtwork = [NSString stringWithCString: attrp->_value.c_str()
						   encoding:NSUTF8StringEncoding];
			}
			else if (strcmp(attrp->_name.c_str(), "title") == 0) {
			    title = [NSString stringWithCString: attrp->_value.c_str()
					      encoding:NSUTF8StringEncoding];
			}
			else if (strcmp(attrp->_name.c_str(), "albumTitle") == 0) {
			    albumTitle = [NSString stringWithCString: attrp->_value.c_str()
						   encoding:NSUTF8StringEncoding];
			}
			else if (strcmp(attrp->_name.c_str(), "podcastDate") == 0) {
			    podcastDate = atoi(attrp->_value.c_str());
			}
			else if (strcmp(attrp->_name.c_str(), "playbackTime") == 0) {
			    sscanf(attrp->_value.c_str(), "%f", &playbackTime);
			}
			else if (strcmp(attrp->_name.c_str(), "details") == 0) {
			    details = [NSString stringWithCString: attrp->_value.c_str()
						encoding:NSUTF8StringEncoding];
			}
			else if (strcmp(attrp->_name.c_str(), "mustDownload") == 0) {
			    mustDownload = atoi(attrp->_value.c_str());
			}
		    }
		    urlItem = [[MFANMediaItem alloc] initWithUrl: url
						     title: title
						     albumTitle: albumTitle];
		    if (podcastDate > 0)
			urlItem.podcastDate = podcastDate;
		    if (urlRemote != nil)
			urlItem.urlRemote = urlRemote;
		    if (urlArtwork != nil)
			urlItem.urlArtwork = urlArtwork;
		    if (details != nil)
			urlItem.details = details;
		    urlItem.mustDownload = mustDownload;
		    urlItem.playbackTime = playbackTime;
		    if (mustDownload == -1) {
			/* not set, use hack that says only podcasts have this set */
			urlItem.mustDownload = ((podcastDate > 0) ? 1 : 0);
		    }
		    else {
			urlItem.mustDownload = mustDownload;
		    }

		    [_idsToLoad addObject: urlItem];
		} /* not an ID */
	    } /* have attributes (should always be) */
	} /* for loop over media items */
    }

    delete rootNodep;
    delete xgmlp;
    free(origBufferp);

    /* we can't actually compute the items yet since the evaluation of
     * the query text hasn't happened yet, so the MFANSetList object
     * is old and may even be empty.
     */
    _playIndex = playIndex;
    _playOffset = playOffset;
}

- (NSMutableArray *) queryInfo
{
    return _queryInfo;
}

/* set the playcontext to play song in slot ix */
- (void) setIndex: (int) ix
{
    _playIndex = ix;
    _playOffset = 0;

    _currentIndex = _playIndex;
    _currentTime = _playOffset;
}

- (BOOL) hasAnyItems
{
    return ([_queryInfo count] > 0);
}

- (BOOL) hasPodcastItems
{
    MFANScanItem *scanItem;

    for(scanItem in _queryInfo) {
	if (scanItem.scanFlags & [MFANScanItem scanPodcast])
	    return YES;
    }
    return NO;
}

/* an array of MFANScanItems; this is called when we are given a new
 * definition of a playlist from the playlist editor.
 */
- (void) setQueryInfo: (NSMutableArray *) queryInfo
{
    _queryInfo = queryInfo;
    [_setList setQueryInfo: queryInfo ids: nil];

    _playIndex = 0;
    _playOffset = 0;
    _didLoadQuery = YES;	/* don't use the file's info any more */
    _currentIndex = _playIndex;
    _currentTime = _playOffset;
}

/* The format is an XML object with a root of "playlist" containing an
 * attr named "playIndex" giving a 0-based index of the song that's
 * playing (integer string), an attr named "playTime" giving a
 * floating point number giving the # of seconds into that song, and
 * an ordered set of "mediaItem" nodes each with an attr named
 * "persistentId" with a long long value giving the iPhone's
 * persistent ID for that song.  The top level attribute "queryString"
 * gives the initial query string that generated this.
 */
- (void) saveListToFile
{
    Xgml *xgmlp;
    Xgml::Node *rootNodep;
    Xgml::Attr *attrNodep;
    Xgml::Node *mediaItemsNodep;
    Xgml::Node *scanItemsNodep;
    Xgml::Node *tempNodep;
    char tbuffer[128];
    MFANScanItem *scanItem;
    long ix;
    long count;
    NSNumber *numberp;
    MFANMediaItem *mfanItem;
    NSObject *obj;
    int scanFlags;

    NSLog(@"- Starting save for file %@", _associatedFile);

    xgmlp = new Xgml;
    rootNodep = new Xgml::Node();
    rootNodep->init("playlist", /* needsEnd */ 1, /* !isLeaf */ 0);

    sprintf(tbuffer, "%6lu", (long) [_myPlayerView currentIndex]);
    attrNodep = new Xgml::Attr();
    attrNodep->init("playIndex", tbuffer);
    rootNodep->appendAttr(attrNodep);

    sprintf(tbuffer, "%10f", [_myPlayerView currentPlaybackTime]);
    attrNodep = new Xgml::Attr();
    attrNodep->init("playTime", tbuffer);
    rootNodep->appendAttr(attrNodep);

    /* this node contains a list of media item nodes, each of which has a value
     * of the persistent ID of the song in question.
     */
    scanItemsNodep = new Xgml::Node();
    scanItemsNodep->init("scanItems", /* needsEnd */ 1, /* isLeaf */ 0);
    rootNodep->appendChild(scanItemsNodep);

    /* put the scan items now */
    for(scanItem in _queryInfo) {
	tempNodep = new Xgml::Node();
	tempNodep->init("scanItem", /* needsEnd */ 1, /* isLeaf */ 0);
	scanItemsNodep->appendChild(tempNodep);

	attrNodep = new Xgml::Attr();
	attrNodep->init("title", [[scanItem title] cStringUsingEncoding: NSUTF8StringEncoding]);
	attrNodep->saveQuoted();
	tempNodep->appendAttr(attrNodep);

	if ([scanItem secondaryKey] != nil) {
	    attrNodep = new Xgml::Attr();
	    attrNodep->init("secondaryKey",
			    [[scanItem secondaryKey] cStringUsingEncoding:
							  NSUTF8StringEncoding]);
	    attrNodep->saveQuoted();
	    tempNodep->appendAttr(attrNodep);
	}

	if ([scanItem stationDetails] != nil) {
	    attrNodep = new Xgml::Attr();
	    attrNodep->init("stationDetails",
			    [[scanItem stationDetails] cStringUsingEncoding:
							   NSUTF8StringEncoding]);
	    attrNodep->saveQuoted();
	    tempNodep->appendAttr(attrNodep);
	}

	if ([scanItem stationUrl] != nil) {
	    attrNodep = new Xgml::Attr();
	    attrNodep->init("stationUrl",
			    [[scanItem stationUrl] cStringUsingEncoding:
						       NSUTF8StringEncoding]);
	    attrNodep->saveQuoted();
	    tempNodep->appendAttr(attrNodep);
	}

	scanFlags = [scanItem scanFlags];

	sprintf(tbuffer, "%d", scanFlags);
	attrNodep = new Xgml::Attr();
	attrNodep->init("scanFlags", tbuffer);
	tempNodep->appendAttr(attrNodep);

	if (scanFlags & [MFANScanItem scanPodcast]) {
	    sprintf(tbuffer, "%ld", [scanItem podcastDate]);
	    attrNodep = new Xgml::Attr();
	    attrNodep->init("podcastDate", tbuffer);
	    tempNodep->appendAttr(attrNodep);
	}

	sprintf(tbuffer, "%ld", [scanItem minStars]);
	attrNodep = new Xgml::Attr();
	attrNodep->init("minStars", tbuffer);
	tempNodep->appendAttr(attrNodep);

	sprintf(tbuffer, "%d", [scanItem cloud]);
	attrNodep = new Xgml::Attr();
	attrNodep->init("cloud", tbuffer);
	tempNodep->appendAttr(attrNodep);
    }

    /* this node contains a list of media item nodes, each of which has a value
     * of the persistent ID of the song in question.
     */
    mediaItemsNodep = new Xgml::Node();
    mediaItemsNodep->init("mediaItems", /* needsEnd */ 1, /* isLeaf */ 0);
    rootNodep->appendChild(mediaItemsNodep);

    count = [_setList count];
    for(ix = 0;ix<count;ix++) {
	tempNodep = new Xgml::Node();
	tempNodep->init("mediaItem", /* needsEnd */ 1, /* isLeaf */ 0);
	mediaItemsNodep->appendChild(tempNodep);
	obj = [_setList persistentIdWithIndex: ix];
	if ([obj isKindOfClass: [NSNumber class]]) {
	    numberp = (NSNumber *) obj;
	    sprintf(tbuffer, "%lld", [numberp longLongValue]);
	    attrNodep = new Xgml::Attr();
	    attrNodep->init("id", tbuffer);
	    tempNodep->appendAttr(attrNodep);
	}
	else if ([obj isKindOfClass: [MFANMediaItem class]]) {
	    mfanItem = (MFANMediaItem *) obj;

	    attrNodep = new Xgml::Attr();
	    attrNodep->saveQuoted();
	    attrNodep->init("url", [[mfanItem localUrl] cStringUsingEncoding: NSUTF8StringEncoding]);
	    tempNodep->appendAttr(attrNodep);

	    attrNodep = new Xgml::Attr();
	    attrNodep->saveQuoted();
	    attrNodep->init("title",
			    [[mfanItem title] cStringUsingEncoding: NSUTF8StringEncoding]);
	    tempNodep->appendAttr(attrNodep);

	    attrNodep = new Xgml::Attr();
	    attrNodep->saveQuoted();
	    attrNodep->init("albumTitle",
			    [[mfanItem albumTitle] cStringUsingEncoding: NSUTF8StringEncoding]);
	    tempNodep->appendAttr(attrNodep);

	    attrNodep = new Xgml::Attr();
	    attrNodep->init("mustDownload", ([mfanItem mustDownload]? "1" : "0"));
	    tempNodep->appendAttr(attrNodep);

	    if (mfanItem.details != nil) {
		attrNodep = new Xgml::Attr();
		attrNodep->saveQuoted();
		attrNodep->init("details",
				[mfanItem.details cStringUsingEncoding: NSUTF8StringEncoding]);
		tempNodep->appendAttr(attrNodep);
	    }

	    if (mfanItem.urlRemote != nil) {
		attrNodep = new Xgml::Attr();
		attrNodep->saveQuoted();
		attrNodep->init("urlRemote",
				[mfanItem.urlRemote cStringUsingEncoding: NSUTF8StringEncoding]);
		tempNodep->appendAttr(attrNodep);
	    }

	    if (mfanItem.urlArtwork != nil) {
		attrNodep = new Xgml::Attr();
		attrNodep->saveQuoted();
		attrNodep->init("urlArtwork",
				[mfanItem.urlArtwork cStringUsingEncoding: NSUTF8StringEncoding]);
		tempNodep->appendAttr(attrNodep);
	    }

	    if (mfanItem.podcastDate > 0) {
		attrNodep = new Xgml::Attr();
		sprintf(tbuffer, "%ld", mfanItem.podcastDate);
		attrNodep->init("podcastDate", tbuffer);
		tempNodep->appendAttr(attrNodep);
	    }

	    if ([mfanItem trackPlaybackTime]) {
		attrNodep = new Xgml::Attr();
		sprintf(tbuffer, "%f", mfanItem.playbackTime);
		attrNodep->init("playbackTime", tbuffer);
		tempNodep->appendAttr(attrNodep);
	    }
	}
    }

    _lastSavedTime = [NSDate timeIntervalSinceReferenceDate];

    /* pass this node to the save helper thread, which will take responsibility
     * for freeing the storage eventually.
     */
    [self saveSend: rootNodep];

    delete xgmlp;
}

- (NSTimeInterval) lastSavedTime
{
    return _lastSavedTime;
}

/* queue an Xgml::Node for saving; there may be work already queued, in which
 * case we replace the work to be queued next.
 */
- (void) saveSend: (Xgml::Node *) node
{
    Xgml::Node *oldNode;

    NSLog(@"-  about to queue save request");
    [_saveLock lock];
    oldNode = _saveNode;
    _saveNode = node;
    [_saveLock signal];
    [_saveLock unlock];
    NSLog(@"-  queued save request");

    /* cleanup old unprocessed node, if any */
    if (oldNode) {
	delete oldNode;
    }
}

- (long) getCurrentIndex
{
    return [_myPlayerView currentIndex];
}

/* async state saving thread starts execution here */
- (void) saveInit: (id) parm
{
    Xgml::Node *node;
    std::string cppString;
    FILE *filep;
    long code;

    while(1) {
	[_saveLock lock];
	node = _saveNode;
	if (node == NULL) {
	    [_saveLock wait];
	    [_saveLock unlock];
	    continue;
	}
	_saveNode = NULL;
	[_saveLock unlock];

	NSLog(@"-  save opening file %@", _associatedFile);

	/* save the file */
	cppString.clear();
	node->printToCPP(&cppString);

	filep = fopen([_associatedFile cStringUsingEncoding: NSASCIIStringEncoding], "w");
	if (!filep) {
	    delete node;
	    NSLog(@"!save failed to open file %@, skipping", _associatedFile);
	    continue;
	}

	code = fwrite(cppString.c_str(), cppString.length(), 1, filep);
	if (code != 1) {
	    NSLog(@"!WRITE FAILED");
	}
	code = fclose(filep);
	if (code != 0) {
	    NSLog(@"!FCLOSE FAILED");
	}
	delete node;
	NSLog(@"-  save finished with close for file %@", _associatedFile);

	/* we don't want to process more than one save every N seconds, so we
	 * accomplish this by just not going back to the head of the loop for
	 * that long.
	 */
	// [NSThread sleepForTimeInterval: 3.0];
	NSLog(@"- save sleep finished");
    }
}
@end
