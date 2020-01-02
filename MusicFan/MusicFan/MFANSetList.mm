//  MFANSetList.m
//  MusicFan
//
//  Created by Michael Kazar on 4/28/14.
//  Copyright (c) 2014 Mike Kazar. All rights reserved.
//

#import <MediaPlayer/MediaPlayer.h>
#import <Foundation/Foundation.h>
#import "MFANSetList.h"
#import "MFANQueryResults.h"
#import "MFANIndicator.h"
#import "MFANTopSettings.h"
#import "MFANCGUtil.h"
#import "MFANPlayerView.h"
#import "MFANTopUpnp.h"
#import "MFANDownload.h"
#import "MFANSocket.h"

#include <stdlib.h>
#include <stdio.h>
#include <sys/time.h>
#include <sys/stat.h>
#include "xgml.h"
#include "json.h"
#include "upnp.h"
#include "radioscan.h"

/* represents one element of a playlist -- either an iPod playlist,
 * a song, all songs by an artist or all songs on an album.  Further
 * qualified by rating and cloud.
 */
@implementation MFANScanItem {
    /* array (may be nil if not filled in yet) of MFANMediaItems to return */
    NSArray *_items; 
}

+ (int) scanArtist { return 1; }
+ (int) scanSong { return 2; }
+ (int) scanAlbum {return 4;}
+ (int) scanPlaylist {return 8;}
+ (int) scanChildAlbum {return 0x10;}
+ (int) scanOldPodcast {return 0x20;}
+ (int) scanGenre {return 0x40;}
+ (int) scanRadio {return 0x80;}
+ (int) scanPodcast {return 0x100;}
+ (int) scanUpnpSong {return 0x200;}
+ (int) scanUpnpAlbum {return 0x400;}
+ (int) scanUpnpArtist {return 0x800;}
+ (int) scanUpnpGenre {return 0x1000;}
+ (int) scanRecording {return 0x2000;}

/* NSString *title is a property
 * NSString *secondaryKey is a property
 * int scanFlags is a property
 * int minStars is a property 
 * int cloud is a property
 * NSArray *items is a property
 */

+ (id<MFANMediaSel>) mediaSelFromFlags: (int) flags
{
    if (flags & [MFANScanItem scanArtist])
	return [MFANSetList artistsSel];
    else if (flags & [MFANScanItem scanAlbum])
	return [MFANSetList albumsSel];
    else if (flags & [MFANScanItem scanSong])
	return [MFANSetList songsSel];
    else if (flags & [MFANScanItem scanPlaylist])
	return [MFANSetList playlistsSel];
    else if (flags & [MFANScanItem scanChildAlbum]) {
	/* return a specially initialize childAlbumsSel object that's suitable
	 * only for calling mediaItemsForScan
	 */
	return [[MFANChildAlbumsSel alloc] initSimple];
    }
    else if (flags & [MFANScanItem scanPodcast]) {
	return [MFANSetList podcastsSel];
    }
    else if (flags & [MFANScanItem scanGenre]) {
	return [MFANSetList genresSel];
    }
    else if (flags & [MFANScanItem scanRadio]) {
	return [MFANSetList radioSel];
    }
    else if (flags & [MFANScanItem scanUpnpSong])
	return [MFANSetList upnpSongsSel];
    else if (flags & [MFANScanItem scanUpnpAlbum])
	return [MFANSetList upnpAlbumsSel];
    else if (flags & [MFANScanItem scanUpnpArtist])
	return [MFANSetList upnpArtistsSel];
    else if (flags & [MFANScanItem scanUpnpGenre])
	return [MFANSetList upnpGenresSel];
    else if (flags & [MFANScanItem scanRecording])
	return [MFANSetList recordingsSel];
    else
	return nil;
}

/* generate filtered media item list; this only filters iTunes media information; it 
 * passes other items through directly.
 */
- (NSArray *) performFilters: (NSArray *) itemsArray
{
    MPMediaItem *item;
    MFANMediaItem *mfanItem;
    int include;
    NSMutableArray *newArray;
    BOOL removeLongItems;

    if (!(_scanFlags & [MFANScanItem scanPodcast])) {
	removeLongItems = YES;
    }
    else
	removeLongItems = NO;

    /* filters allow everything */
    if (_cloud && _minStars == 0 && !removeLongItems)
	return itemsArray;

    newArray = [[NSMutableArray alloc] init];
    for(mfanItem in itemsArray) {
	include = 1;

	/* see if we have iTunes metadata for song/stream */
	item = [mfanItem item];
	if (item == nil) {
	    /* no metadata, just include */
	    [newArray addObject: mfanItem];
	    continue;
	}

	if (_minStars > 0) {
	    if (_minStars > [[item valueForProperty: MPMediaItemPropertyRating]
				unsignedIntegerValue]) {
		include = 0;
	    }
	}
	if (!_cloud) {
	    if ([[item valueForProperty: MPMediaItemPropertyIsCloudItem]
		    boolValue]) {
		include = 0;
	    }
	}
	if (removeLongItems) {
	    MPMediaType mediaType;

	    mediaType = [[item valueForProperty: MPMediaItemPropertyMediaType]
			    integerValue];
	    if ((mediaType & (MPMediaTypePodcast |
			      MPMediaTypeAudioBook))) {
		include = 0;
	    }
	}

	if (include) {
	    [newArray addObject: mfanItem];
	}
    }

    return newArray;
}

- (MFANScanItem *) initWithTitle: (NSString *) title type: (int) flags
{
    self = [super init];
    if (self) {
	_minStars = 0;
	_cloud = 0;
	_title = title;
	_podcastDate = 0;
	_scanFlags = flags;

	_items = nil;
    }
    return self;
}

- (MFANScanItem *) init
{
    self = [super init];
    if (self) {
	_minStars = 0;
	_cloud = 0;
	_title = nil;
	_scanFlags = 0;
    }
    return self;
}

- (NSArray *) mediaItems
{
    id<MFANMediaSel> sel;

    if (_items != nil)
	return _items;

    sel = [MFANScanItem mediaSelFromFlags: _scanFlags];
    return [sel mediaItemsForScan: self];
}

- (long) mediaCount
{
    if (_items != nil) {
	return [_items count];
    }
    else
	return 0;
}

@end /* MFANScanItem */

NSArray *
wrapMediaItems(NSArray *mediaArray)
{
    long i;
    long count;
    MPMediaItem *mediaItem;
    MFANMediaItem *mfanItem;
    NSMutableArray *avArray;

    avArray = [[NSMutableArray alloc] init];
    count = [mediaArray count];

    for(i=0;i<count;i++) {
	mediaItem = mediaArray[i];
	mfanItem = [[MFANMediaItem alloc] initWithMediaItem: mediaItem];
	[avArray addObject: mfanItem];
    }

    return avArray;
}

@implementation MFANAsyncInit {
    NSCondition *_initCond;
    BOOL _initDone;
}

- (MFANAsyncInit *) initWithObject: (id) object sel: (SEL) sel
{
    self = [super init];

    if (self) {
	_initCond = [[NSCondition alloc] init];
	_initDone = NO;
	[NSThread detachNewThreadSelector: sel
		  toTarget: object
		  withObject: self];
    }

    return self;
}

/* async thread should call this when done; note that it doesn't return */
- (void) initDone
{
    [_initCond lock];
    _initDone = YES;
    [_initCond unlock];
    [_initCond broadcast];
    NSLog(@"- SetList: async init done");
    [NSThread exit];
}

- (void) waitForInitDone
{
    [_initCond lock];
    while(!_initDone) {
	[_initCond wait];
    }
    [_initCond unlock];
}

@end /* MFANAsyncInit */

@implementation MFANPlaylistsSel {
    /* array of MPMediaItemCollection objects */
    NSArray *_playlistsArray;

    /* array of NSNumbers caching counts for display smoothness */
    NSMutableArray *_countsArray;
}

- (BOOL) supportsLocalAdd
{
    return NO;
}

- (int) localCount
{
    return 0;
}

- (BOOL) populateFromSearch: (NSString *)searchString async: (BOOL *) isAsyncp
{
    return NO;
}

- (int) rowHeight
{
    return 50;
}

- (MFANPlaylistsSel *) init
{
    MPMediaQuery *queryp;
    long count;
    long i;

    self = [super init];
    if (self) {
	queryp = [MPMediaQuery playlistsQuery];
	_playlistsArray = [queryp collections];

	_countsArray = [[NSMutableArray alloc] init];
	count = [_playlistsArray count];
	for(i=0;i<count;i++) {
	    [_countsArray addObject: [NSNumber numberWithLong: (-1)]];
	}
    }
    return self;
}

- (NSString *) nameByIx: (int) ix
{
    MPMediaPlaylist *playp;

    playp = _playlistsArray[ix];
    return [playp valueForProperty: MPMediaPlaylistPropertyName];
}

- (UIImage *) imageByIx: (int) ix size: (CGFloat) size
{
    return nil;
}

- (NSString *) subtitleByIx: (int) ix
{
    MPMediaPlaylist *play;
    long count;
    NSString *result;
    NSNumber *number;

    number = [_countsArray objectAtIndex: ix];
    count = [number longValue];

    if (count == -1) {
	/* cache miss */
	play = _playlistsArray[ix];
	count = [play count];
	[_countsArray insertObject: [NSNumber numberWithLong: count] atIndex: ix];
    }

    if (count == 1)
	result = @"1 song";
    else
	result = [NSString stringWithFormat: @"%ld songs", count];

    return result;
}

- (BOOL) hasChildByIx: (int) ix
{
    return YES;
}

- (id<MFANMediaSel>) childByIx: (int) ix
{
    id<MFANMediaSel> childSel;
    MFANScanItem *scan;
    NSArray *items;

    scan = [self scanItemByIx: ix];
    items = [scan items];

    childSel = [[MFANChildSongsSel alloc] initWithMFANItems: items];

    return childSel;
}

- (MFANScanItem *) scanItemByIx: (int) ix
{
    MPMediaPlaylist *playp;
    MFANScanItem *scan;

    playp = _playlistsArray[ix];
    scan = [[MFANScanItem alloc] init];
    scan.title = [playp valueForProperty: MPMediaPlaylistPropertyName];
    scan.scanFlags = [MFANScanItem scanPlaylist];
    scan.minStars = 0;
    scan.cloud = 0;

    /* fill in mediaItems */
    [self mediaItemsForScan: scan];

    return scan;
}

/* get the media items */
- (NSArray *) mediaItemsForScan: (MFANScanItem *) scan
{
    NSArray *itemsArray;
    MPMediaQuery *query;
    MPMediaPropertyPredicate *pred;

    if (scan.items == nil) {
	/* redo the query */
	query = [[MPMediaQuery alloc] init];
	pred = [MPMediaPropertyPredicate predicateWithValue: scan.title
					 forProperty: MPMediaPlaylistPropertyName];
	[query addFilterPredicate: pred];
	scan.items = itemsArray = wrapMediaItems([query items]);
    }
    else {
	itemsArray = scan.items;
    }

    return itemsArray;
}

- (long) count {
    return [_playlistsArray count];
}

- (BOOL) hasRssItems
{
    return NO;
}

@end

@implementation MFANArtistsSel {
    /* array of MPMediaItemCollection objects */
    NSArray *_artistsArray;
}

- (BOOL) supportsLocalAdd
{
    return NO;
}

- (int) localCount
{
    return 0;
}

- (BOOL) populateFromSearch: (NSString *)searchString async: (BOOL *) isAsyncp
{
    return NO;
}

- (int) rowHeight
{
    return 80;
}

- (MFANArtistsSel *) init
{
    MPMediaQuery *queryp;

    self = [super init];
    if (self != nil) {
	queryp = [MPMediaQuery artistsQuery];
	_artistsArray = [queryp collections];
    }
    return self;
}

- (NSString *) nameByIx: (int) ix
{
    MPMediaItemCollection *coll;
    MPMediaItem *item;

    coll = _artistsArray[ix];
    item = [coll representativeItem];
    return [item valueForProperty: MPMediaItemPropertyArtist];
}

- (UIImage *) imageByIx: (int) ix size: (CGFloat) size
{
    MPMediaItemCollection *coll;
    MPMediaItem *item;
    CGSize tsize;

    tsize.width = tsize.height = size;

    coll = _artistsArray[ix];
    item = [coll representativeItem];
    return mediaImageWithSize(item, tsize, nil);
}

- (NSString *) subtitleByIx: (int) ix
{
    return nil;
}

- (BOOL) hasChildByIx: (int) ix
{
    return YES;
}

- (id<MFANMediaSel>) childByIx: (int) ix
{
    id<MFANMediaSel> childSel;
    MFANScanItem *scan;
    NSArray *items;

    scan = [self scanItemByIx: ix];
    items = [scan items];

    childSel = [[MFANChildAlbumsSel alloc] init: items artist:[self nameByIx: ix]];

    return childSel;
}

- (MFANScanItem *) scanItemByIx: (int) ix
{
    MPMediaItemCollection *coll;
    MPMediaItem *item;
    MFANScanItem *scan;

    coll = _artistsArray[ix];
    item = [coll representativeItem];
    scan = [[MFANScanItem alloc] init];
    scan.title = [item valueForProperty: MPMediaItemPropertyArtist];
    scan.scanFlags = [MFANScanItem scanArtist];
    scan.minStars = 0;
    scan.cloud = 0;

    /* fill in items via mediaItemsForScan */
    [self mediaItemsForScan: scan];

    return scan;
}

/* get the media items */
- (NSArray *) mediaItemsForScan: (MFANScanItem *) scan
{
    NSArray *itemsArray;
    MPMediaQuery *query;
    MPMediaPropertyPredicate *pred;

    if (scan.items == nil) {
	/* redo the query */
	query = [[MPMediaQuery alloc] init];
	pred = [MPMediaPropertyPredicate predicateWithValue: scan.title
					 forProperty: MPMediaItemPropertyArtist];
	[query setGroupingType: MPMediaGroupingAlbum];
	[query addFilterPredicate: pred];
	scan.items = itemsArray = wrapMediaItems([query items]);
    }
    else {
	itemsArray = scan.items;
    }

    return itemsArray;
}

- (long) count {
    return [_artistsArray count];
}

- (BOOL) hasRssItems
{
    return NO;
}

@end

@implementation MFANAlbumsSel {
    /* array of album names */
    MFANAsyncInit *_asyncInit;
    MFANIndicator *_ind;

    NSMutableArray *_albumNames;

    /* corresponding array of arrays of mpmediaitems objects */
    NSMutableArray *_albumsArray;
}

- (BOOL) supportsLocalAdd
{
    return NO;
}

- (int) localCount
{
    return 0;
}

- (BOOL) populateFromSearch: (NSString *)searchString async: (BOOL *) isAsyncp
{
    return NO;
}
 
- (int) rowHeight
{
    return 80;
}
 
- (MFANAlbumsSel *) initWithInd: (MFANIndicator *) ind
{
    self = [super init];
    if (self) {
	_ind = ind;
	_asyncInit = [[MFANAsyncInit alloc] initWithObject: self sel: @selector(bkgInit:)];
    }

    return self;
}

- (void) bkgInit: (MFANAsyncInit *) async
{
    MPMediaQuery *queryp;
    NSArray *splitAlbumsArray;
    NSMutableArray *lastArray;
    NSString *lastName;
    NSString *tname;
    MPMediaItemCollection *coll;
    NSArray *albumItems;
    long count;
    long i;

    queryp = [[MPMediaQuery alloc] init];
    [queryp setGroupingType: MPMediaGroupingAlbum];
    splitAlbumsArray = [queryp collections];

    _albumsArray = [[NSMutableArray alloc] init];
    _albumNames = [[NSMutableArray alloc] init];

    /* splitAlbumsArray comes back with a separate album for each artist in
     * the album.  Now we have to merge them all together (barf).
     */
    lastName = @"";
    lastArray = nil;
    count = [splitAlbumsArray count];
    i=0;
    for(coll in splitAlbumsArray) {
	i++;
	albumItems = [coll items];
	tname = [albumItems[0] valueForProperty: MPMediaItemPropertyAlbumTitle];
	if (tname == nil)
	    continue;
	if (![lastName isEqualToString: tname]) {
	    /* new item */
	    lastName = tname;
	    [_albumNames addObject: tname];
	    
	    lastArray = [[NSMutableArray alloc] init];
	    [_albumsArray addObject: lastArray];
	}
	[lastArray addObjectsFromArray: albumItems];
    }

    [async initDone];
}

- (NSString *) nameByIx: (int) ix
{
    [_asyncInit waitForInitDone];
    return [_albumNames objectAtIndex: ix];
}

- (UIImage *) imageByIx: (int) ix size: (CGFloat) size
{
    NSArray *albumArray;
    MPMediaItem *item;
    CGSize tsize;

    [_asyncInit waitForInitDone];
    tsize.width = tsize.height = size;
    albumArray = _albumsArray[ix];
    item = [albumArray objectAtIndex: 0];
    return mediaImageWithSize(item, tsize, nil);
}

- (NSString *) subtitleByIx: (int) ix
{
    NSArray *albumArray;
    MPMediaItem *item;
    NSString *result;
    NSString *temp;
    long count;

    [_asyncInit waitForInitDone];
    albumArray = _albumsArray[ix];
    item = albumArray[0];
    
    temp = [item valueForProperty: MPMediaItemPropertyArtist];
    if (temp != nil)
	result = temp;
    else
	result = @"Various Artists";

    count = [albumArray count];
    if (count != 1) {
	result = [result stringByAppendingFormat: @", %ld songs", count];
    }
    else
	result = [result stringByAppendingString: @", 1 song"];

    return result;
}

- (BOOL) hasChildByIx: (int) ix
{
    return YES;
}

- (id<MFANMediaSel>) childByIx: (int) ix
{
    id<MFANMediaSel> childSel;
    NSArray *items;

    [_asyncInit waitForInitDone];
    items = _albumsArray[ix];
    childSel = [[MFANChildSongsSel alloc] initWithMPItems: items];

    return childSel;
}

- (MFANScanItem *) scanItemByIx: (int) ix
{
    NSArray *albumArray;
    MPMediaItem *item;
    MFANScanItem *scan;

    [_asyncInit waitForInitDone];
    albumArray = _albumsArray[ix];
    item = albumArray[0];
    scan = [[MFANScanItem alloc] init];
    scan.title = [item valueForProperty: MPMediaItemPropertyAlbumTitle];
    scan.scanFlags = [MFANScanItem scanAlbum];
    scan.minStars = 0;
    scan.cloud = 0;

    /* load up _items array */
    [self mediaItemsForScan: scan];

    return scan;
}

- (NSArray *) mediaItemsForScan: (MFANScanItem *) scan
{
    NSArray *itemsArray;
    MPMediaQuery *query;
    MPMediaPropertyPredicate *pred;

    [_asyncInit waitForInitDone];
    if (scan.items == nil) {
	/* redo the query */
	query = [[MPMediaQuery alloc] init];
	pred = [MPMediaPropertyPredicate predicateWithValue: scan.title
					 forProperty: MPMediaItemPropertyAlbumTitle];
	[query setGroupingType: MPMediaGroupingAlbum];
	[query addFilterPredicate: pred];
	scan.items = itemsArray = wrapMediaItems([query items]);
    }
    else {
	itemsArray = scan.items;
    }

    return itemsArray;
}

- (long) count {
    [_asyncInit waitForInitDone];
    return [_albumsArray count];
}

- (BOOL) hasRssItems
{
    return NO;
}

@end

@implementation MFANRadioSel {
    /* array of MFANMediaItem objects */
    NSMutableArray *_radioArray;
    NSMutableArray *_namesArray;
    FILE *_filep;
    RadioScan *_scanp;
    MFANSocketFactory *_factoryp;
    RadioScanQuery *_queryp;
    NSThread *_searchThread;
    NSString *_searchString;
    BOOL _asyncSearchRunning;

    /* count of the # of local entries added by the phone's owner, rather than
     * having come from stations.rsd.
     */
    int _localCount;
}

- (BOOL) supportsLocalAdd
{
    return YES;
}

- (int) localCount
{
    return _localCount;
}

- (void) searchAsync: (id) junk
{
    const char *searchStringp;
    RadioScanStation *stationp;
    RadioScanStation::Entry *streamp;
    RadioScanStation::Entry *bestStreamp;
    NSString *stationName;
    NSString *stationShortDescr;
    NSString *stationUrl;
    NSString *streamInfo;
    MFANMediaItem *radioItem;

    searchStringp = [_searchString cStringUsingEncoding: NSUTF8StringEncoding];
    NSLog(@"search is %s", searchStringp);
    _scanp->searchStation(std::string(searchStringp), &_queryp);
    NSLog(@"back from search -- %ld stations", _queryp->_stations.count());
    
    /* now add in all the stations we found */
    for(stationp = _queryp->_stations.head(); stationp; stationp=stationp->_dqNextp) {
	bestStreamp = NULL;
	for(streamp = stationp->_entries.head(); streamp; streamp=streamp->_dqNextp) {
	    if (bestStreamp == NULL || bestStreamp->_streamRateKb < streamp->_streamRateKb) {
		bestStreamp = streamp;
	    }
	}

	if (!bestStreamp)
	    continue;

	/* here, if we found a stream, record it */
	stationName = [NSString stringWithUTF8String: stationp->_stationName.c_str()];
	stationShortDescr = [NSString stringWithUTF8String: stationp->_stationShortDescr.c_str()];
	stationUrl = [NSString stringWithUTF8String: bestStreamp->_streamUrl.c_str()];
	streamInfo = [NSString stringWithFormat: @"%d Kb/s %s",
			       bestStreamp->_streamRateKb, bestStreamp->_streamType.c_str()];
	[_namesArray addObject: stationName];
	radioItem = [[MFANMediaItem alloc] initWithUrl: stationUrl
						 title: stationName
					    albumTitle: stationShortDescr];
	radioItem.details = streamInfo;

	[_radioArray addObject: radioItem];
    }

    /* don't turn this off until we're done messing with _radioArray and _namesArray,
     * so that MFANPopEdit doesn't let the user navigate to operations that
     * might access those variables
     */
    _asyncSearchRunning = NO;

    delete _queryp;
    _queryp = NULL;

    [NSThread exit];
}

- (BOOL) populateFromSearch: (NSString *)searchString async: (BOOL *) isAsyncp
{
    /* delete all items after the local search items, so that each
     * search presents results of new search plus manually entered
     * stations.
     */
    while([_radioArray count] > _localCount)
	[_radioArray removeLastObject];
    while([_namesArray count] > _localCount)
	[_namesArray removeLastObject];

    /* so our status update knows if we're really done */
    _asyncSearchRunning = YES;

    _searchString = searchString;

    _searchThread = [[NSThread alloc] initWithTarget: self
					    selector: @selector(searchAsync:)
					      object: nil];
    [_searchThread start];

    /* indicate that population occurs asynchronously, with current status of
     * search returned by getStatus method below.
     */
    *isAsyncp = YES;
    return YES;
}

- (NSString *) getStatus
{
    if (_asyncSearchRunning) {
	if (_queryp != nil) {
	    return [NSString stringWithUTF8String: _queryp->_baseStatus.c_str()];
	}
	else 
	    return @"Search initializing";
    }
    else {
	/* no longer running */
	return nil;
    }
}

- (void) abortSearch
{
    RadioScanQuery *qp;

    /* need to reference count these structures to avoid crash race condition */
    qp = _queryp;
    if (qp) 
	qp->abort();
}

- (int) rowHeight
{
    return 80;
}

NSString *
unparseRadioStation(NSString *stationName, NSString *details, NSString *url)
{
    NSString *result;

    result = [NSString stringWithFormat: @"%@\t%@\tUnknown\tUS\tEnglish\t%@\t-\t-\t-\t-\t-\n",
		       stationName, details, url];
    return result;
}

int
parseRadioStation(char *inp, NSString **namep, NSString **detailsp, NSString **urlp)
{
    NSString *nameStr;
    NSString *urlStr;
    NSArray *splitStr;
    long count;
    int i;
    long length;

    *namep = nil;
    *urlp = nil;

    nameStr = nil;
    urlStr = nil;

    splitStr = [[NSString stringWithCString: inp encoding:NSUTF8StringEncoding]
		   componentsSeparatedByString: @"\t"];
    count = [splitStr count];
    if (count < 6)
	return -1;

    /* The first 5 components are: (name, description, genre, country,
     * language), followed by URLs or "-".  We also ignore URLs ending
     * with .asx, since those are windows media streams, and I'm not
     * sure we can play them with iOS players.
     */
    nameStr = splitStr[0];

    for(i=5; i<count; i++) {
	urlStr = splitStr[i];
	length = [urlStr length];
	if (length >= 5) {
	    if ([urlStr hasSuffix: @".asx"])
		continue;
	}
	if ([urlStr isEqualToString: @"-"]) {
	    /* no useful URL */
	    return -1;
	}

	/* otherwise URL is good */
	break;
    }

    *namep = nameStr;
    *urlp = urlStr;
    *detailsp = splitStr[1];
    return 0;
}

- (BOOL) readRadioFile
{
    char *tp;
    char tbuffer[256];
    int32_t code;
    NSString *stationUrl;
    NSString *stationName;
    NSString *details;
    MFANMediaItem *radioItem;

    while(1) {
	tp = fgets(tbuffer, sizeof(tbuffer), _filep);
	if (!tp) {
	    break;
	}

	code = parseRadioStation(tp, &stationName, &details, &stationUrl);
	if (code == 0) {
	    [_namesArray addObject: stationName];
	    radioItem = [[MFANMediaItem alloc] initWithUrl: stationUrl
					       title: stationName
					       albumTitle: @"Radio"];
	    if (details != nil && [details length] > 0) {
		radioItem.details = details;
	    }

	    [_radioArray addObject: radioItem];
	}
    }
    return YES;
}

- (MFANRadioSel *) init
{
    NSString *localFile;

    self = [super init];
    if (self) {
	std::string dirPrefix;

	dirPrefix = std::string([fileNameForFile(@"") cStringUsingEncoding: NSUTF8StringEncoding]);

	_factoryp = new MFANSocketFactory();
	_scanp = new RadioScan();
	_scanp->init(_factoryp, dirPrefix);

	_radioArray = [[NSMutableArray alloc] init];
	_namesArray = [[NSMutableArray alloc] init];
	_localCount = 0;
	_asyncSearchRunning = NO;

	/* read the local updates file first, since its contents shows up first in the
	 * list.
	 */
	localFile = fileNameForFile(@"localradio.txt");
	_filep = fopen([localFile cStringUsingEncoding: NSUTF8StringEncoding], "r");
	if (_filep != NULL) {
	    [self readRadioFile];
	    fclose(_filep);
	    _filep = NULL;
	}

	/* remember the count of the # of entries we loaded from the local file, since
	 * we need that to display these items properly.
	 */
	_localCount = (int) [_radioArray count];

	_queryp = NULL;
    }
    return self;
}

/* create a locally added entry, and save it to an updated additions file in the
 * same format as stations.rsd.
 */
- (BOOL) localAddKey: (NSString *) key value: (NSString *) value
{
    MFANMediaItem *radioItem;
    long localCount = _localCount++;

    [_namesArray insertObject: key atIndex: localCount];
    radioItem = [[MFANMediaItem alloc] initWithUrl: value
				       title: key
				       albumTitle: @"Radio (local)"];
    [_radioArray insertObject: radioItem atIndex: localCount];

    [self saveLocal];

    return YES;
}

- (BOOL) localRemoveEntry: (long) slot
{
    /* don't let someone delete the persistent entries */
    if (slot >= _localCount)
	return NO;

    [_namesArray removeObjectAtIndex: slot];
    [_radioArray removeObjectAtIndex: slot];
    _localCount--;

    [self saveLocal];

    return YES;
}

- (BOOL) saveLocal
{
    FILE *filep;
    uint32_t i;
    NSString *line;
    MFANMediaItem *radioItem;
    int32_t code;
    NSString *path;

    path = fileNameForFile(@"localradio.txt");
    filep = fopen([path cStringUsingEncoding: NSUTF8StringEncoding], "w");
    if (!filep)
	return NO;
    for(i=0;i<_localCount;i++) {
	radioItem = _radioArray[i];
	line = unparseRadioStation( [radioItem title],
				    @"Locally_added_radio",
				    [radioItem localUrl]);
	code = fputs([line cStringUsingEncoding: NSUTF8StringEncoding], filep);
	if (code <= 0) {
	    /* something went wrong */
	    fclose(filep);
	    return NO;
	}
    }
    fclose(filep);
    return YES;
}

/* obtain the prompt information for a manual add */
- (BOOL) localAddPromptKey: (NSString **) keyPromptp value: (NSString **) valuep
{
    NSString *prompt;
    NSString *value;
    prompt = @"Radio Station Name";
    value = @"URL for stream";

    *keyPromptp = prompt;
    *valuep = value;
    
    return YES;
}

- (NSString *) nameByIx: (int) ix
{
    MFANMediaItem *mfanItem;

    if (ix < [_radioArray count]) {
	mfanItem = [_radioArray objectAtIndex: ix];
	return [NSString stringWithFormat: @"%@ (%@)", _namesArray[ix], mfanItem.details];
    }
    else {
	return _namesArray[ix];
    }
}

- (UIImage *) imageByIx: (int) ix size: (CGFloat) size
{
    return nil;
}

- (NSString *) subtitleByIx: (int) ix
{
    MFANMediaItem *mfanItem;

    if (ix < [_radioArray count]) {
	mfanItem = [_radioArray objectAtIndex: ix];
	return mfanItem.urlAlbumTitle;
    }
    else
	return nil;
}

- (BOOL) hasChildByIx: (int) ix
{
    return NO;
}

- (id<MFANMediaSel>) childByIx: (int) ix
{
    return nil;
}

- (MFANScanItem *) scanItemByIx: (int) ix
{
    MFANMediaItem *item;
    MFANScanItem *scan;

    item = _radioArray[ix];
    scan = [[MFANScanItem alloc] init];
    scan.title = item.urlTitle;
    scan.secondaryKey = item.urlAlbumTitle;
    scan.stationDetails = item.details;
    scan.stationUrl = item.url;
    scan.scanFlags = [MFANScanItem scanRadio];
    scan.minStars = 0;
    scan.cloud = 0;

    return scan;
}

- (NSArray *) mediaItemsForScan: (MFANScanItem *) scan
{
    NSArray *itemsArray;
    MFANMediaItem *item;

    if (scan.items == nil) {
	/* redo the query */
	item = [[MFANMediaItem alloc] init];
	item.urlTitle= scan.title;
	item.urlAlbumTitle = scan.secondaryKey;
	item.details = scan.stationDetails;
	item.url = scan.stationUrl;

	itemsArray = [NSArray arrayWithObject: item];
	scan.items = itemsArray;
    }
    else {
	itemsArray = scan.items;
    }

    return itemsArray;
}

- (long) count {
    return [_radioArray count];
}

- (BOOL) hasRssItems
{
    return NO;
}
@end

/* figure out the albums covering a set of media items (songs),
 * and build a selector for it.
 */
@implementation MFANChildAlbumsSel {
    /* array of NSString objects giving the name */
    NSMutableArray *_albumNamesArray;

    /* This is an array of arrays, one for each name.  Each subarray
     * is an array of MPMediaItems.
     */ 
    NSMutableArray *_mediaItemsArray;

    /* artist whose albums these are */
    NSString *_artistName;
}

- (BOOL) supportsLocalAdd
{
    return NO;
}

- (int) localCount
{
    return 0;
}

- (BOOL) populateFromSearch: (NSString *)searchString async: (BOOL *) isAsyncp
{
    return NO;
}

- (int) rowHeight{
    return 80;
}

/* simple init so we have a selector for calling mediaItemsForScan */
- (MFANChildAlbumsSel *) initSimple
{
    self = [super init];
    if (self) {
	_albumNamesArray = nil;
	_mediaItemsArray = nil;
	_artistName = nil;
    }

    return self;
}

- (MFANChildAlbumsSel *) init: (NSArray *) itemArray artist: (NSString *) artist;
{
    NSString *name;
    MFANMediaItem *mfanItem;
    MPMediaItem *item;
    long ix;
    NSMutableArray *array;

    self = [super init];
    if (self) {
	_albumNamesArray = [[NSMutableArray alloc] init];
	_mediaItemsArray = [[NSMutableArray alloc] init];

	_artistName = artist;

	/* maintain two arrays -- an array of album names, and a corresponding
	 * array of arrays, each giving the set of songs in the corresponding album.
	 * Thus, if albumNamesArray[0] is "Close To The Edge", then mediaItemsArray[0]
	 * is an array of all of the media items in that album.
	 */
	for(mfanItem in itemArray) {
	    item = [mfanItem item];
	    /* lookup album name */
	    name = [item valueForProperty: MPMediaItemPropertyAlbumTitle];
	    if (name == nil)
		continue;

	    /* find out if album name already in our set of album names, and if so,
	     * where.
	     */
	    ix = [_albumNamesArray indexOfObject: name];
	    if (ix == NSNotFound) {
		/* create a new album, and a new array to hold all of its songs */
		ix = [_albumNamesArray count];
		[_albumNamesArray addObject: name];
		[_mediaItemsArray addObject: [NSMutableArray arrayWithObject: item]];
	    }
	    else {
		/* add this song to the album's list of songs */
		[[_mediaItemsArray objectAtIndex: ix] addObject: item];
	    }
	} /* for all media items */

	/* now sort each album by song # */
	for(array in _mediaItemsArray) {
	    [MFANSetList sortAlbum: array auxItems: nil];
	}
    } /* if self */ 
    return self;
}

- (NSString *) nameByIx: (int) ix
{
    return _albumNamesArray[ix];
}

- (UIImage *) imageByIx: (int) ix size: (CGFloat) size
{
    MPMediaItem *item;
    CGSize tsize;
    NSArray *itemArray;

    itemArray = _mediaItemsArray[ix];

    if ([itemArray count] > 0) {
	item = [itemArray objectAtIndex: 0];
    }
    else {
	return nil;
    }

    tsize.width = tsize.height = size;
    return mediaImageWithSize(item, tsize, nil);
}

- (NSString *) subtitleByIx: (int) ix
{
    NSArray *items;
    NSString *result;
    long count;

    items = _mediaItemsArray[ix];
    count = [items count];
    if (count != 1) {
	result = [NSString stringWithFormat: @"%ld songs", count];
    }
    else
	result = @"1 song";

    return result;
}

- (BOOL) hasChildByIx: (int) ix
{
    return YES;
}

- (id<MFANMediaSel>) childByIx: (int) ix
{
    id<MFANMediaSel> childSel;
    NSArray *items;

    items = [_mediaItemsArray objectAtIndex: ix];
    childSel = [[MFANChildSongsSel alloc] initWithMPItems: items];

    return childSel;
}

/* used when walking down the media selection tree */
- (MFANScanItem *) scanItemByIx: (int) ix
{
    MFANScanItem *scan;

    scan = [[MFANScanItem alloc] init];
    scan.title = _albumNamesArray[ix];
    scan.secondaryKey = _artistName;
    scan.scanFlags = [MFANScanItem scanChildAlbum];
    scan.minStars = 0;
    scan.cloud = 0;
    scan.items = wrapMediaItems(_mediaItemsArray[ix]);

    return scan;
}

/* used when redoing selection after new playlist editing */
- (NSArray *) mediaItemsForScan: (MFANScanItem *) scan
{
    MPMediaQuery *query;
    MPMediaPropertyPredicate *pred;
    NSArray *artistItemsArray;
    NSMutableArray *itemsArray;
    MPMediaItem *item;
    MFANMediaItem *mfanItem;

    if (scan.items != nil) {
	return scan.items;
    }

    /* grab all songs by this artist */
    query = [[MPMediaQuery alloc] init];
    pred = [MPMediaPropertyPredicate predicateWithValue: scan.secondaryKey
				     forProperty: MPMediaItemPropertyArtist];
    [query setGroupingType: MPMediaGroupingAlbum];
    [query addFilterPredicate: pred];
    artistItemsArray = [query items];

    /* and select only those whose album matches */
    itemsArray = [[NSMutableArray alloc] init];
    for(item in artistItemsArray) {
	if ([[item valueForProperty: MPMediaItemPropertyAlbumTitle]
		isEqualToString: scan.title]) {
	    /* wrap MFAN Media item around media item */ 
	    mfanItem = [[MFANMediaItem alloc] initWithMediaItem: item];
	    [itemsArray addObject: mfanItem];
	}
    }
    scan.items = itemsArray;
    return itemsArray;
}

- (long) count {
    return [_albumNamesArray count];
}

- (BOOL) hasRssItems
{
    return NO;
}
@end /* MFANChildAlbumsSel */

/* figure out the albums covering a set of media items (songs),
 * and build a selector for it.
 */
@implementation MFANChildSongsSel {
    /* array of NSString objects giving the name */
    NSMutableArray *_songNamesArray;

    /* This is an array of MFANMediaItems, one for each name */ 
    NSArray *_mediaItemsArray;
}

- (BOOL) supportsLocalAdd
{
    return NO;
}

- (int) localCount
{
    return 0;
}

- (BOOL) populateFromSearch: (NSString *)searchString async: (BOOL *) isAsyncp
{
    return NO;
}

- (int) rowHeight{
    return 80;
}

- (MFANChildSongsSel *) initWithMPItems: (NSArray *) itemArray
{
    NSString *name;
    MPMediaItem *item;
    NSMutableArray *mfanItems;
    MFANMediaItem *mfanItem;

    self = [super init];
    if (self) {
	_mediaItemsArray = mfanItems = [[NSMutableArray alloc] init];
	_songNamesArray = [[NSMutableArray alloc] init];

	for(item in itemArray) {
	    mfanItem = [[MFANMediaItem alloc] initWithMediaItem: item];
	    [mfanItems addObject: mfanItem];

	    name = [item valueForProperty: MPMediaItemPropertyTitle];
	    if (name != nil)
		[_songNamesArray addObject: name];
	    else
		[_songNamesArray addObject: @"[None]"];
	}
    }

    return self;
}

- (MFANChildSongsSel *) initWithMFANItems: (NSArray *) itemArray
{
    NSString *name;
    MPMediaItem *item;
    MFANMediaItem *mfanItem;

    self = [super init];
    if (self) {
	_mediaItemsArray = itemArray;
	_songNamesArray = [[NSMutableArray alloc] init];

	for(mfanItem in itemArray) {
	    item = [mfanItem item];
	    name = [item valueForProperty: MPMediaItemPropertyTitle];
	    if (name != nil)
		[_songNamesArray addObject: name];
	    else
		[_songNamesArray addObject: @"[None]"];
	}
    }

    return self;
}

- (NSString *) nameByIx: (int) ix
{
    return _songNamesArray[ix];
}

- (UIImage *) imageByIx: (int) ix size: (CGFloat) size
{
    MPMediaItem *item;
    MFANMediaItem *mfanItem;
    CGSize tsize;

    mfanItem = _mediaItemsArray[ix];
    item = [mfanItem item];

    tsize.width = tsize.height = size;
    return mediaImageWithSize(item, tsize, nil);
}

- (NSString *) subtitleByIx: (int) ix
{
    MPMediaItem *item;
    MFANMediaItem *mfanItem;
    NSString *result;
    NSString *tstring;
    NSString *ustring;

    mfanItem = _mediaItemsArray[ix];
    item = [mfanItem item];

    tstring = [item valueForProperty: MPMediaItemPropertyArtist];
    if (tstring == nil)
	result = @"";
    else
	result = tstring;

    ustring = [item valueForProperty: MPMediaItemPropertyAlbumTitle];

    if (tstring != nil && ustring != nil) {
	result = [result stringByAppendingString: @" -- "];
    }

    if (ustring != nil)
	result = [result stringByAppendingString: ustring];

    return result;
}

- (BOOL) hasChildByIx: (int) ix
{
    return NO;
}

- (id<MFANMediaSel>) childByIx: (int) ix
{
    return nil;
}

- (MFANScanItem *) scanItemByIx: (int) ix
{
    MFANScanItem *scan;

    scan = [[MFANScanItem alloc] init];
    scan.title = _songNamesArray[ix];
    scan.scanFlags = [MFANScanItem scanSong];
    scan.minStars = 0;
    scan.cloud = 0;
    scan.items = [NSArray arrayWithObject: _mediaItemsArray[ix]];

    return scan;
}

- (NSArray *) mediaItemsForScan: (MFANScanItem *) scan
{
    NSArray *itemsArray;
    MPMediaQuery *query;
    MPMediaPropertyPredicate *pred;

    if (scan.items == nil) {
	/* redo the query */
	query = [[MPMediaQuery alloc] init];
	pred = [MPMediaPropertyPredicate predicateWithValue: scan.title
					 forProperty: MPMediaItemPropertyTitle];
	[query addFilterPredicate: pred];
	scan.items = itemsArray = wrapMediaItems([query items]);
    }
    else {
	itemsArray = scan.items;
    }

    return itemsArray;
}

- (long) count {
    return [_songNamesArray count];
}

- (BOOL) hasRssItems
{
    return NO;
}
@end /* MFANChildSongsSel */

@implementation MFANSongsSel {
    /* array of MPMediaItemCollection objects */
    NSArray *_songsArray;
}

- (BOOL) supportsLocalAdd
{
    return NO;
}

- (int) localCount
{
    return 0;
}

- (BOOL) populateFromSearch: (NSString *)searchString async: (BOOL *) isAsyncp
{
    return NO;
}

- (int) rowHeight
{
    return 80;
}

- (MFANSongsSel *) init
{
    MPMediaQuery *query;

    self = [super init];
    if (self) {
	query = [MPMediaQuery songsQuery];
	_songsArray = [query items];
    }
    return self;
}

- (NSString *) nameByIx: (int) ix
{
    MPMediaItem *item;

    item = [_songsArray objectAtIndex: ix];
    return [item valueForProperty: MPMediaItemPropertyTitle];
}

- (UIImage *) imageByIx: (int) ix size: (CGFloat) size
{
    MPMediaItem *item;
    CGSize tsize;

    tsize.width = tsize.height = size;
    item = [_songsArray objectAtIndex: ix];
    return mediaImageWithSize(item, tsize, nil);
}

- (NSString *) subtitleByIx: (int) ix
{
    MPMediaItem *item;
    NSString *result;
    NSString *tstring;
    NSString *ustring;

    item = [_songsArray objectAtIndex: ix];

    tstring = [item valueForProperty: MPMediaItemPropertyArtist];
    if (tstring == nil)
	result = @"";
    else
	result = tstring;

    ustring = [item valueForProperty: MPMediaItemPropertyAlbumTitle];

    if (tstring != nil && ustring != nil) {
	result = [result stringByAppendingString: @" -- "];
    }

    if (ustring != nil)
	result = [result stringByAppendingString: ustring];

    return result;
}

- (BOOL) hasChildByIx: (int) ix
{
    return NO;
}

- (id<MFANMediaSel>) childByIx: (int) ix
{
    return nil;
}

- (MFANScanItem *) scanItemByIx: (int) ix
{
    MPMediaItem *item;
    MFANScanItem *scan;

    item = _songsArray[ix];
    scan = [[MFANScanItem alloc] init];
    scan.title = [item valueForProperty: MPMediaItemPropertyTitle];
    scan.scanFlags = [MFANScanItem scanSong];
    scan.minStars = 0;
    scan.cloud = 0;

    return scan;
}

- (NSArray *) mediaItemsForScan: (MFANScanItem *) scan
{
    NSArray *itemsArray;
    MPMediaQuery *query;
    MPMediaPropertyPredicate *pred;

    if (scan.items == nil) {
	/* redo the query */
	query = [[MPMediaQuery alloc] init];
	pred = [MPMediaPropertyPredicate predicateWithValue: scan.title
					 forProperty: MPMediaItemPropertyTitle];
	[query addFilterPredicate: pred];
	scan.items = itemsArray = wrapMediaItems([query items]);
    }
    else {
	itemsArray = scan.items;
    }

    return itemsArray;
}

- (long) count {
    return [_songsArray count];
}

- (BOOL) hasRssItems
{
    return NO;
}
@end


@implementation MFANPodcastsSel {
    /* array of MFANMediaItem objects */
    NSMutableArray *_podcastsArray;
    NSURLSession *_urlSession;
    NSOperationQueue *_urlQueue;
    Xgml *_xgmlp;
    Json *_jsonp;
}

- (BOOL) supportsLocalAdd
{
    return NO;
}

- (int) localCount
{
    return 0;
}

- (BOOL) populateFromSearch: (NSString *)searchString async: (BOOL *) isAsyncp
{
    NSArray *comps;
    NSString *tstr;
    NSMutableString *searchUrl;
    int first;
    __block NSString *result;
    dispatch_semaphore_t sema;
    NSURLSessionDataTask *dataTask;
    Json::Node *rootNodep;
    Json::Node *resultsChildrenNodep;
    Json::Node *resultsArrayNodep;
    Json::Node *feedNodep;
    Json::Node *feedUrlNodep;
    Json::Node *genreNodep;
    NSString *feedUrlStr;
    NSString *artistName;
    NSString *genreName;
    NSString *subName;
    Json::Node *artistNameNodep;
    Json::Node *collectionNameNodep;
    NSString *collectionNameStr;
    Json::Node *artworkUrl100Nodep;
    char *jsonDatap;
    int32_t code;
    MFANMediaItem *mfanItem;
    int foundGenre;

    /* build URL string from search key */
    first = 1;
    comps = [searchString componentsSeparatedByString: @" "];
    searchUrl = [[NSMutableString alloc] initWithString: @"https://itunes.apple.com/search?media=podcast&limit=50&term="];
    for(tstr in comps) {
	if ([tstr length] == 0)
	    continue;
	if (!first)
	    [searchUrl appendString: @"+"];
	first = 0;
	[searchUrl appendString: tstr];
    }

    sema = dispatch_semaphore_create(0);
    _urlSession = [NSURLSession sessionWithConfiguration:
				    [NSURLSessionConfiguration ephemeralSessionConfiguration]
				delegate: nil
				delegateQueue: _urlQueue];
    dataTask = [_urlSession dataTaskWithURL: [NSURL URLWithString: searchUrl]
			    completionHandler: ^(NSData *data,
						 NSURLResponse *resp,
						 NSError *callError) {
	    result = [[NSString alloc] initWithData: data encoding: NSUTF8StringEncoding];
	    dispatch_semaphore_signal(sema);
	}];
    [dataTask resume ];
    dispatch_semaphore_wait(sema, DISPATCH_TIME_FOREVER);

    jsonDatap = (char *) [result cStringUsingEncoding: NSUTF8StringEncoding];
    InStreamString jsonStream(jsonDatap);
    code = _jsonp->parseJsonValue(&jsonStream, &rootNodep);
    if (code != 0)
	NSLog(@"!parseJson code = %d", code);
    if (code != 0) {
	return NO;
    }

    /* iterate over all items, looking for titles and RSS URLs */
    resultsArrayNodep = rootNodep->searchForChild("results", 0);
    if (!resultsArrayNodep) {
	delete rootNodep;
	return NO;
    }

    _podcastsArray = [[NSMutableArray alloc] init];

    resultsChildrenNodep = resultsArrayNodep->_children.head();
    for( feedNodep = resultsChildrenNodep->_children.head();
	 feedNodep;
	 feedNodep = feedNodep->_dqNextp) {
	collectionNameNodep = feedNodep->searchForChild("collectionName", 0);
	if (collectionNameNodep != NULL) {
	    collectionNameStr =
		[NSString stringWithUTF8String:
			      collectionNameNodep->_children.head()->_name.c_str()];
	    artistNameNodep = feedNodep->searchForChild("artistName");
	    if (artistNameNodep != NULL) {
		artistName = [NSString stringWithUTF8String:
					   artistNameNodep->_children.head()->_name.c_str()];
	    }

	    foundGenre = 0;
	    genreNodep = feedNodep->searchForChild("genres");
	    if (genreNodep) {
		NSLog(@"found genre parent %s", genreNodep->_name.c_str());
		genreNodep = genreNodep->searchForLeaf();
		if (genreNodep) {
		    genreName = [NSString stringWithUTF8String: genreNodep->_name.c_str()];
		    NSLog(@"genre name is %@", genreName);
		    foundGenre = 1;
		}
	    }
	    feedUrlNodep = feedNodep->searchForChild("feedUrl", 0);
	    if (feedUrlNodep != NULL) {
		feedUrlStr = [NSString stringWithUTF8String:
					   feedUrlNodep->_children.head()->_name.c_str()];
		artworkUrl100Nodep = feedNodep->searchForChild("artworkUrl100", 0);
		if (foundGenre) {
		    subName = [NSString stringWithFormat: @"%@ [%@]", artistName, genreName];
		}
		else
		    subName = artistName;
		mfanItem = [[MFANMediaItem alloc]
			       initWithUrl: feedUrlStr
			       title: collectionNameStr
			       albumTitle: subName];
		[_podcastsArray addObject: mfanItem];
	    }
	}
    }

    *isAsyncp = NO;
    return YES;
}

- (int) rowHeight
{
    return 80;
}

- (MFANPodcastsSel *) init
{
    self = [super init];
    if (self) {
	_xgmlp = new Xgml();
	_jsonp = new Json();
	_podcastsArray = nil;
	_urlQueue = [[NSOperationQueue alloc] init];	/* for background URL operations */
    }
    return self;
}

- (NSString *) nameByIx: (int) ix
{
    MFANMediaItem *item;

    item = [_podcastsArray objectAtIndex: ix];
    return [item title];
}

- (UIImage *) imageByIx: (int) ix size: (CGFloat) size
{
    return nil;
}

- (NSString *) subtitleByIx: (int) ix
{
    MFANMediaItem *item;

    item = _podcastsArray[ix];
    return [item albumTitle];
}

- (BOOL) hasChildByIx: (int) ix
{
    return NO;
}

- (id<MFANMediaSel>) childByIx: (int) ix
{
    return nil;
}

- (MFANScanItem *) scanItemByIx: (int) ix
{
    MFANMediaItem *item;
    MFANScanItem *scan;

    item = _podcastsArray[ix];
    scan = [[MFANScanItem alloc] init];
    scan.title = [item title];
    scan.secondaryKey = item.localUrl;
    scan.scanFlags = [MFANScanItem scanPodcast];
    scan.minStars = 0;
    scan.cloud = 0;
    scan.items = [NSArray arrayWithObject: item];

    [self mediaItemsForScan: scan];

    return scan;
}

- (NSArray *) mediaItemsForScan: (MFANScanItem *) scan
{
    __block NSString *result;
    int32_t code;
    dispatch_semaphore_t sema;
    NSURLSessionDataTask *dataTask;
    Xgml::Node *rootNodep;
    NSString *rssUrl;
    char *rssDatap;
    Xgml::Node *channelNodep;
    Xgml::Node *titleNodep;
    Xgml::Node *enclosureNodep;
    Xgml::Node *inodep;
    Xgml::Attr *attrNodep;
    Xgml::Node *pubDateNodep;
    Xgml::Node *subtitleNodep;
    uint32_t podcastDate;
    char *pubDatep;
    Xgml::Node *childNodep;
    MFANMediaItem *mfanItem;
    char *titlep;
    char *urlp;
    char *detailsp;
    NSMutableArray *scanArray;

    /* take the RSS feed and us it to populate an array of MFANMediaItems representing
     * the individual podcasts.
     */
    rssUrl = scan.secondaryKey;
    sema = dispatch_semaphore_create(0);
    _urlSession = [NSURLSession sessionWithConfiguration:
				    [NSURLSessionConfiguration ephemeralSessionConfiguration]
				delegate: nil
				delegateQueue: _urlQueue];
    dataTask = [_urlSession dataTaskWithURL: [NSURL URLWithString: rssUrl]
			    completionHandler: ^(NSData *data,
						 NSURLResponse *resp,
						 NSError *callError) {
	    result = [[NSString alloc] initWithData: data encoding: NSUTF8StringEncoding];
	    dispatch_semaphore_signal(sema);
	}];
    [dataTask resume ];
    dispatch_semaphore_wait(sema, DISPATCH_TIME_FOREVER);

    rssDatap = (char *) [result cStringUsingEncoding: NSUTF8StringEncoding];
    NSLog(@"- Parsing '%s'", rssDatap);
    code = _xgmlp->parse(&rssDatap, &rootNodep);
    if (code != 0) {
	NSLog(@"!Parse of '%s' failed codee=%d", rssDatap, code);
	return scan.items;
    }

    rootNodep->print();

    channelNodep = rootNodep->searchForChild("channel", 0);
    scanArray = [[NSMutableArray alloc] init];
    if (channelNodep) {
	for(inodep = channelNodep->_children.tail(); inodep; inodep = inodep->_dqPrevp) {
	    if (inodep->_name == "item") {
		titleNodep = inodep->searchForChild("title", 0);
		enclosureNodep = inodep->searchForChild("enclosure", 0);
		if (titleNodep)
		    titlep = (char *) titleNodep->_children.head()->_name.c_str();
		else
		    titlep = const_cast<char *>("[No title]");
		urlp = NULL;
		if (enclosureNodep) {
		    for( attrNodep = enclosureNodep->_attrs.head();
			 attrNodep;
			 attrNodep = attrNodep->_dqNextp) {
			if (attrNodep->_name == "url") {
			    urlp = const_cast<char *>(attrNodep->_value.c_str());
			    break;
			}
		    }
		}

		pubDateNodep = inodep->searchForChild("pubDate", 0);
		if (pubDateNodep && (childNodep = pubDateNodep->_children.head())) {
		    pubDatep = const_cast<char *>(childNodep->_name.c_str());
		    podcastDate = parseRssDate(pubDatep);
		}
		else
		    podcastDate = 0;

		detailsp = NULL;
		subtitleNodep = inodep->searchForChild("itunes:subtitle", 0);
		if (subtitleNodep) {
		    subtitleNodep = subtitleNodep->dive();
		    detailsp = const_cast<char *>(subtitleNodep->_name.c_str());
		    NSLog(@"- details='%s'", detailsp);
		}

		if (urlp != NULL) {
		    mfanItem = [[MFANMediaItem alloc]
				   initWithUrl: @""
				   title:  [NSString stringWithUTF8String: titlep]
				   albumTitle: [scan title]];
		    mfanItem.podcastDate = podcastDate;
		    mfanItem.urlRemote = [NSString stringWithUTF8String: urlp];
		    mfanItem.mustDownload = YES;
		    if (detailsp)
			mfanItem.details = [NSString stringWithUTF8String: detailsp];
		    [scanArray addObject: mfanItem];
		    scan.podcastDate = podcastDate;
		}
		else {
		    NSLog(@"!Missing URL for item");
		}
	    }
	}
    }
    
    scan.items = scanArray;
    return scan.items;
}

- (long) count {
    return [_podcastsArray count];
}

- (BOOL) hasRssItems
{
    return YES;
}
@end

@implementation MFANGenresSel {
    /* array of MPMediaItemCollection objects */
    NSArray *_genresArray;
}

- (BOOL) supportsLocalAdd
{
    return NO;
}

- (int) localCount
{
    return 0;
}

- (BOOL) populateFromSearch: (NSString *)searchString async: (BOOL *) isAsyncp
{
    return NO;
}

- (int) rowHeight
{
    return 80;
}

- (MFANGenresSel *) init
{
    MPMediaQuery *queryp;

    self = [super init];
    if (self != nil) {
	queryp = [MPMediaQuery genresQuery];
	_genresArray = [queryp collections];
    }
    return self;
}

- (NSString *) nameByIx: (int) ix
{
    MPMediaItemCollection *coll;
    MPMediaItem *item;

    coll = _genresArray[ix];
    item = [coll representativeItem];
    return [item valueForProperty: MPMediaItemPropertyGenre];
}

- (UIImage *) imageByIx: (int) ix size: (CGFloat) size
{
    MPMediaItemCollection *coll;
    MPMediaItem *item;
    CGSize tsize;

    tsize.width = tsize.height = size;

    coll = _genresArray[ix];
    item = [coll representativeItem];
    return mediaImageWithSize(item, tsize, nil);
}

- (NSString *) subtitleByIx: (int) ix
{
    return nil;
}

- (BOOL) hasChildByIx: (int) ix
{
    return YES;
}

- (id<MFANMediaSel>) childByIx: (int) ix
{
    id<MFANMediaSel> childSel;
    MFANScanItem *scan;
    NSArray *items;

    scan = [self scanItemByIx: ix];
    items = [scan items];

    childSel = [[MFANChildAlbumsSel alloc] init: items artist:[self nameByIx: ix]];

    return childSel;
}

- (MFANScanItem *) scanItemByIx: (int) ix
{
    MPMediaItemCollection *coll;
    MPMediaItem *item;
    MFANScanItem *scan;

    coll = _genresArray[ix];
    item = [coll representativeItem];
    scan = [[MFANScanItem alloc] init];
    scan.title = [item valueForProperty: MPMediaItemPropertyGenre];
    scan.scanFlags = [MFANScanItem scanGenre];
    scan.minStars = 0;
    scan.cloud = 0;

    /* fill in items via mediaItemsForScan */
    [self mediaItemsForScan: scan];

    return scan;
}

/* get the media items */
- (NSArray *) mediaItemsForScan: (MFANScanItem *) scan
{
    NSArray *itemsArray;
    MPMediaQuery *query;
    MPMediaPropertyPredicate *pred;

    if (scan.items == nil) {
	/* redo the query */
	query = [[MPMediaQuery alloc] init];
	pred = [MPMediaPropertyPredicate predicateWithValue: scan.title
					 forProperty: MPMediaItemPropertyGenre];
	[query addFilterPredicate: pred];
	scan.items = itemsArray = wrapMediaItems([query items]);
    }
    else {
	itemsArray = scan.items;
    }

    return itemsArray;
}

- (long) count {
    return [_genresArray count];
}

- (BOOL) hasRssItems
{
    return NO;
}
@end

@implementation MFANUpnpItem
- (MFANUpnpItem *) init
{
    self = [super init];
    if (self) {
	/* nothing */
    }
    return self;
}
@end

@implementation MFANUpnpSongsSel {
    /* array of MFANUpnpItem objects */
    NSMutableArray *_songsArray;
    UpnpDBase *_dbasep;
    long _processedVersion;
    UIImage *_defaultImage;
    NSString *_albumNameFilter;
    NSString *_artistNameFilter;
    NSString *_genreNameFilter;
}

- (BOOL) supportsLocalAdd
{
    return NO;
}

- (int) localCount
{
    return 0;
}

int32_t
addHelper(void *callbackContextp, void *recordContextp)
{
    MFANUpnpSongsSel *songsSel;
    MFANUpnpItem *newItem;
    const char *albumNameFilterp;
    const char *albumNamep;
    const char *artistNameFilterp;
    const char *artistNamep;
    const char *genreNameFilterp;
    const char *genreNamep;

    UpnpDBase::Record *recordp = (UpnpDBase::Record *) recordContextp;
    songsSel = (__bridge MFANUpnpSongsSel *) callbackContextp;

    if (songsSel->_albumNameFilter != nil) {
	albumNameFilterp = [(songsSel->_albumNameFilter)
			       cStringUsingEncoding: NSUTF8StringEncoding];
	albumNamep = recordp->_album.c_str();
	if (strcmp(albumNameFilterp, albumNamep) != 0)
	    return 0;
    }

    if (songsSel->_artistNameFilter != nil) {
	artistNameFilterp = [songsSel->_artistNameFilter
				     cStringUsingEncoding: NSUTF8StringEncoding];
	artistNamep = recordp->_artist.c_str();
	if (strcmp(artistNameFilterp, artistNamep) != 0)
	    return 0;
    }

    if (songsSel->_genreNameFilter != nil) {
	genreNameFilterp = [songsSel->_genreNameFilter
				    cStringUsingEncoding: NSUTF8StringEncoding];
	genreNamep = recordp->_genre.c_str();
	if (strcmp(genreNameFilterp, genreNamep) != 0)
	    return 0;
    }

    newItem = [[MFANUpnpItem alloc] init];
    newItem.title = [NSString stringWithUTF8String: recordp->_title.c_str()];
    newItem.artist = [NSString stringWithUTF8String: recordp->_artist.c_str()];
    newItem.album = [NSString stringWithUTF8String: recordp->_album.c_str()];
    newItem.url = [NSString stringWithUTF8String: recordp->_url.c_str()];
    newItem.urlArtwork = [NSString stringWithUTF8String: recordp->_artUrl.c_str()];
    
    [(songsSel->_songsArray) addObject: newItem];
    NSLog(@"in callback %s %p", recordp->_title.c_str(), songsSel->_songsArray);
    return 0;
}

- (BOOL) populateFromSearch: (NSString *)searchString async: (BOOL *) isAsyncp
{
    return NO;
}

- (int) rowHeight
{
    return 80;
}

- (void) checkInit
{
    MFANTopUpnp *upnp;
    long newVersion;

    upnp = [MFANTopUpnp getGlobalUpnp];
    if (!upnp)
	return;
    _dbasep = (UpnpDBase *) [upnp getDBase];
    if (!_dbasep)
	return;

    newVersion = _dbasep->getVersion();
    if (newVersion != _processedVersion) {
	/* copy the updated database contents into our state */
	NSLog(@"array is at %p", _songsArray);
	[_songsArray removeAllObjects];
	_dbasep->apply(addHelper, (__bridge void *) self);
	_processedVersion = newVersion;
    }
}

- (MFANUpnpSongsSel *) init
{
    self = [super init];
    if (self) {
	_songsArray = [[NSMutableArray alloc] init];
	_processedVersion = 0;
	_defaultImage = [UIImage imageNamed: @"recordfield.jpg"];
	_artistNameFilter = nil;
	_albumNameFilter = nil;
    }
    return self;
}

- (MFANUpnpSongsSel *) initWithGenreName: (NSString *)genreName
{
    self = [super init];
    if (self) {
	_songsArray = [[NSMutableArray alloc] init];
	_processedVersion = 0;
	_defaultImage = [UIImage imageNamed: @"recordfield.jpg"];
	_genreNameFilter = genreName;
    }
    return self;
}

- (MFANUpnpSongsSel *) initWithArtistName: (NSString *) artistName
				albumName: (NSString *) albumName
{
    self = [super init];
    if (self) {
	_songsArray = [[NSMutableArray alloc] init];
	_processedVersion = 0;
	_defaultImage = [UIImage imageNamed: @"recordfield.jpg"];
	_artistNameFilter = artistName;
	_albumNameFilter = albumName;
    }
    return self;
}

- (NSString *) nameByIx: (int) ix
{
    MFANUpnpItem *item;

    item = [_songsArray objectAtIndex: ix];
    return item.title;
}

- (UIImage *) imageByIx: (int) ix size: (CGFloat) size
{
    /* can we do better? */
    return resizeImage(_defaultImage, size);
}

- (NSString *) subtitleByIx: (int) ix
{
    MFANUpnpItem *item;
    NSString *result;
    NSString *tstring;
    NSString *ustring;

    item = [_songsArray objectAtIndex: ix];

    tstring = item.artist;
    if (tstring == nil)
	result = @"";
    else
	result = tstring;

    ustring = item.album;

    if (tstring != nil && ustring != nil) {
	result = [result stringByAppendingString: @" -- "];
    }

    if (ustring != nil)
	result = [result stringByAppendingString: ustring];

    return result;
}

- (BOOL) hasChildByIx: (int) ix
{
    return NO;
}

- (id<MFANMediaSel>) childByIx: (int) ix
{
    return nil;
}

- (MFANScanItem *) scanItemByIx: (int) ix
{
    MFANUpnpItem *item;
    MFANScanItem *scan;

    item = _songsArray[ix];
    scan = [[MFANScanItem alloc] init];
    scan.title = item.title;
    if (item.artist != nil)
	scan.secondaryKey = item.artist;
    scan.scanFlags = [MFANScanItem scanUpnpSong];
    scan.minStars = 0;
    scan.cloud = 0;

    return scan;
}

- (NSArray *) mediaItemsForScan: (MFANScanItem *) scan
{
    NSMutableArray *itemsArray;
    MFANMediaItem *mediaItem;
    MFANUpnpItem *upnpItem;

    [self checkInit];

    if (scan.items == nil) {
	itemsArray = [[NSMutableArray alloc] init];
	for(upnpItem in _songsArray) {
	    if ( [scan.title isEqualToString: upnpItem.title] &&
		 (upnpItem.artist == nil || scan.secondaryKey == nil ||
		  [scan.secondaryKey isEqualToString: upnpItem.artist])) {
		mediaItem = [[MFANMediaItem alloc]
				initWithUrl: @""
				title: upnpItem.title
				albumTitle: upnpItem.album];
		mediaItem.urlRemote = upnpItem.url;
		mediaItem.urlArtwork = [MFANDownload artUrlForHash: upnpItem.urlArtwork
						     extension: @"jpg"];
		[itemsArray addObject: mediaItem];
	    }
	}
	scan.items = itemsArray;
	return itemsArray;
    }
    else {
	return scan.items;
    }
}

/* when a new PopEdit is fired up, it calls count early on, which gives us a chance
 * to check that we're running with the results from the latest UPNP database; that's
 * what we're doing with checkInit.
 */
- (long) count {
    [self checkInit];

    return [_songsArray count];
}

- (BOOL) hasRssItems
{
    return NO;
}
@end

@implementation MFANUpnpAlbumsSel {
    /* array of MFANUpnpItem objects */
    NSMutableArray *_songsArray;

    /* array of NSStrings giving album names */
    NSMutableArray *_albumsArray;

    UpnpDBase *_dbasep;
    long _processedVersion;
    UIImage *_defaultImage;
    NSString *_artistNameFilter;
}

- (BOOL) supportsLocalAdd
{
    return NO;
}

- (int) localCount
{
    return 0;
}

int32_t
addAlbumsHelper(void *callbackContextp, void *recordContextp)
{
    UpnpDBase::Record *recordp = (UpnpDBase::Record *) recordContextp;
    MFANUpnpAlbumsSel *albumsSel;
    const char *artistFilterp;
    const char *artistp;

    albumsSel = (__bridge MFANUpnpAlbumsSel *) callbackContextp;

    /* watch for representative item not matching filter */
    if (albumsSel->_artistNameFilter != nil) {
	artistp = recordp->_artist.c_str();
	artistFilterp = [(albumsSel->_artistNameFilter)
			    cStringUsingEncoding: NSUTF8StringEncoding];
	if (strcmp(artistp, artistFilterp) != 0)
	    return 0;
    }

    [(albumsSel->_albumsArray)
	addObject: [NSString stringWithUTF8String: recordp->_album.c_str()]];
    return 0;
}

int32_t
addSongsForAlbumsHelper(void *callbackContextp, void *recordContextp)
{
    MFANUpnpAlbumsSel *albumsSel;
    MFANUpnpItem *newItem;

    UpnpDBase::Record *recordp = (UpnpDBase::Record *) recordContextp;

    albumsSel = (__bridge MFANUpnpAlbumsSel *) callbackContextp;
    newItem = [[MFANUpnpItem alloc] init];
    newItem.title = [NSString stringWithUTF8String: recordp->_title.c_str()];
    newItem.artist = [NSString stringWithUTF8String: recordp->_artist.c_str()];
    newItem.album = [NSString stringWithUTF8String: recordp->_album.c_str()];
    newItem.url = [NSString stringWithUTF8String: recordp->_url.c_str()];
    newItem.urlArtwork = [NSString stringWithUTF8String: recordp->_artUrl.c_str()];
    
    [(albumsSel->_songsArray) addObject: newItem];
    NSLog(@"in callback %s %p", recordp->_title.c_str(), albumsSel->_songsArray);
    return 0;
}

- (BOOL) populateFromSearch: (NSString *)searchString async: (BOOL *) isAsyncp
{
    return NO;
}

- (int) rowHeight
{
    return 80;
}

- (void) checkInit
{
    MFANTopUpnp *upnp;
    long newVersion;

    upnp = [MFANTopUpnp getGlobalUpnp];
    if (!upnp)
	return;
    _dbasep = (UpnpDBase *) [upnp getDBase];
    if (!_dbasep)
	return;

    newVersion = _dbasep->getVersion();
    if (newVersion != _processedVersion) {
	/* copy the updated database contents into our state */
	NSLog(@"array is at %p", _songsArray);
	[_songsArray removeAllObjects];
	[_albumsArray removeAllObjects];
	_dbasep->applyTree(_dbasep->albumTree(), addAlbumsHelper, (__bridge void *) self, 1);
	_dbasep->apply(addSongsForAlbumsHelper, (__bridge void *) self, 0);
	_processedVersion = newVersion;
    }
}

- (MFANUpnpAlbumsSel *) init
{
    self = [super init];
    if (self) {
	_songsArray = [[NSMutableArray alloc] init];
	_albumsArray = [[NSMutableArray alloc] init];
	_processedVersion = 0;
	_defaultImage = [UIImage imageNamed: @"recordfield.jpg"];
	_artistNameFilter = nil;
    }
    return self;
}

- (MFANUpnpAlbumsSel *) initWithArtistName: (NSString *) artistName
{
    self = [super init];
    if (self) {
	_songsArray = [[NSMutableArray alloc] init];
	_albumsArray = [[NSMutableArray alloc] init];
	_processedVersion = 0;
	_defaultImage = [UIImage imageNamed: @"recordfield.jpg"];
	_artistNameFilter = artistName;
    }
    return self;
}

- (NSString *) nameByIx: (int) ix
{
    return [_albumsArray objectAtIndex: ix];
}

- (UIImage *) imageByIx: (int) ix size: (CGFloat) size
{
    /* can we do better? */
    return resizeImage(_defaultImage, size);
}

- (NSString *) subtitleByIx: (int) ix
{
    return @"";
}

- (BOOL) hasChildByIx: (int) ix
{
    return YES;
}

- (id<MFANMediaSel>) childByIx: (int) ix
{
    MFANUpnpSongsSel *songsSel;

    songsSel = [[MFANUpnpSongsSel alloc] initWithArtistName: _artistNameFilter
					 albumName: _albumsArray[ix]];

    return songsSel;
}

- (MFANScanItem *) scanItemByIx: (int) ix
{
    MFANScanItem *scan;

    scan = [[MFANScanItem alloc] init];
    scan.title = _albumsArray[ix];
    scan.scanFlags = [MFANScanItem scanUpnpAlbum];
    scan.minStars = 0;
    scan.cloud = 0;

    return scan;
}

- (NSArray *) mediaItemsForScan: (MFANScanItem *) scan
{
    NSMutableArray *itemsArray;
    MFANMediaItem *mediaItem;
    MFANUpnpItem *upnpItem;

    [self checkInit];

    if (scan.items == nil) {
	itemsArray = [[NSMutableArray alloc] init];
	for(upnpItem in _songsArray) {
	    if ([scan.title isEqualToString: upnpItem.album]) {
		mediaItem = [[MFANMediaItem alloc]
				initWithUrl: @""
				title: upnpItem.title
				albumTitle: upnpItem.album];
		mediaItem.urlRemote = upnpItem.url;
		mediaItem.urlArtwork = [MFANDownload artUrlForHash: upnpItem.urlArtwork
						     extension: @"jpg"];
		[itemsArray addObject: mediaItem];
	    }
	}
	scan.items = itemsArray;
	return itemsArray;
    }
    else {
	return scan.items;
    }
}

/* when a new PopEdit is fired up, it calls count early on, which gives us a chance
 * to check that we're running with the results from the latest UPNP database; that's
 * what we're doing with checkInit.
 */
- (long) count {
    [self checkInit];

    return [_albumsArray count];
}

- (BOOL) hasRssItems
{
    return NO;
}
@end

@implementation MFANUpnpArtistsSel {
    /* array of MFANUpnpItem objects */
    NSMutableArray *_songsArray;

    /* array of NSStrings giving artist names */
    NSMutableArray *_artistsArray;

    UpnpDBase *_dbasep;
    long _processedVersion;
    UIImage *_defaultImage;
}

- (BOOL) supportsLocalAdd
{
    return NO;
}

- (int) localCount
{
    return 0;
}

int32_t
addArtistsHelper(void *callbackContextp, void *recordContextp)
{
    UpnpDBase::Record *recordp = (UpnpDBase::Record *) recordContextp;
    MFANUpnpArtistsSel *artistsSel;

    artistsSel = (__bridge MFANUpnpArtistsSel *) callbackContextp;
    [(artistsSel->_artistsArray)
	addObject: [NSString stringWithUTF8String: recordp->_artist.c_str()]];
    return 0;
}

int32_t
addSongsForArtistsHelper(void *callbackContextp, void *recordContextp)
{
    MFANUpnpArtistsSel *artistsSel;
    MFANUpnpItem *newItem;

    UpnpDBase::Record *recordp = (UpnpDBase::Record *) recordContextp;

    artistsSel = (__bridge MFANUpnpArtistsSel *) callbackContextp;
    newItem = [[MFANUpnpItem alloc] init];
    newItem.title = [NSString stringWithUTF8String: recordp->_title.c_str()];
    newItem.artist = [NSString stringWithUTF8String: recordp->_artist.c_str()];
    newItem.album = [NSString stringWithUTF8String: recordp->_album.c_str()];
    newItem.url = [NSString stringWithUTF8String: recordp->_url.c_str()];
    newItem.urlArtwork = [NSString stringWithUTF8String: recordp->_artUrl.c_str()];
    
    [(artistsSel->_songsArray) addObject: newItem];
    NSLog(@"in callback %s %p", recordp->_title.c_str(), artistsSel->_songsArray);
    return 0;
}

- (BOOL) populateFromSearch: (NSString *)searchString async: (BOOL *) isAsyncp
{
    return NO;
}

- (int) rowHeight
{
    return 80;
}

- (void) checkInit
{
    MFANTopUpnp *upnp;
    long newVersion;

    upnp = [MFANTopUpnp getGlobalUpnp];
    if (!upnp)
	return;
    _dbasep = (UpnpDBase *) [upnp getDBase];
    if (!_dbasep)
	return;

    newVersion = _dbasep->getVersion();
    if (newVersion != _processedVersion) {
	/* copy the updated database contents into our state */
	NSLog(@"array is at %p", _songsArray);
	[_songsArray removeAllObjects];
	[_artistsArray removeAllObjects];
	_dbasep->applyTree( _dbasep->artistTree(),
			    addArtistsHelper,
			    (__bridge void *) self,
			    1);
	_dbasep->apply(addSongsForArtistsHelper, (__bridge void *) self, 1);
	_processedVersion = newVersion;
    }
}

- (MFANUpnpArtistsSel *) init
{
    self = [super init];
    if (self) {
	_songsArray = [[NSMutableArray alloc] init];
	_artistsArray = [[NSMutableArray alloc] init];
	_processedVersion = 0;
	_defaultImage = [UIImage imageNamed: @"recordfield.jpg"];
    }
    return self;
}

- (NSString *) nameByIx: (int) ix
{
    return [_artistsArray objectAtIndex: ix];
}

- (UIImage *) imageByIx: (int) ix size: (CGFloat) size
{
    /* can we do better? */
    return resizeImage(_defaultImage, size);
}

- (NSString *) subtitleByIx: (int) ix
{
    return @"";
}

- (BOOL) hasChildByIx: (int) ix
{
    return YES;
}

- (id<MFANMediaSel>) childByIx: (int) ix
{
    MFANUpnpAlbumsSel *albumsSel;

    albumsSel = [[MFANUpnpAlbumsSel alloc] initWithArtistName: _artistsArray[ix]];

    return albumsSel;

}

- (MFANScanItem *) scanItemByIx: (int) ix
{
    MFANScanItem *scan;

    scan = [[MFANScanItem alloc] init];
    scan.title = _artistsArray[ix];
    scan.scanFlags = [MFANScanItem scanUpnpArtist];
    scan.minStars = 0;
    scan.cloud = 0;

    return scan;
}

- (NSArray *) mediaItemsForScan: (MFANScanItem *) scan
{
    NSMutableArray *itemsArray;
    MFANMediaItem *mediaItem;
    MFANUpnpItem *upnpItem;

    [self checkInit];

    if (scan.items == nil) {
	itemsArray = [[NSMutableArray alloc] init];
	for(upnpItem in _songsArray) {
	    if ([scan.title isEqualToString: upnpItem.artist]) {
		mediaItem = [[MFANMediaItem alloc]
				initWithUrl: @""
				title: upnpItem.title
				albumTitle: upnpItem.album];
		mediaItem.urlRemote = upnpItem.url;
		mediaItem.urlArtwork = [MFANDownload artUrlForHash: upnpItem.urlArtwork
						     extension: @"jpg"];
		[itemsArray addObject: mediaItem];
	    }
	}
	scan.items = itemsArray;
	return itemsArray;
    }
    else {
	return scan.items;
    }
}

/* when a new PopEdit is fired up, it calls count early on, which gives us a chance
 * to check that we're running with the results from the latest UPNP database; that's
 * what we're doing with checkInit.
 */
- (long) count {
    [self checkInit];

    return [_artistsArray count];
}

- (BOOL) hasRssItems
{
    return NO;
}
@end

@implementation MFANUpnpGenresSel {
    /* array of MFANUpnpItem objects */
    NSMutableArray *_songsArray;

    /* array of NSStrings giving artist names */
    NSMutableArray *_genresArray;

    UpnpDBase *_dbasep;
    long _processedVersion;
    UIImage *_defaultImage;
}

- (BOOL) supportsLocalAdd
{
    return NO;
}

- (int) localCount
{
    return 0;
}

int32_t
addGenresHelper(void *callbackContextp, void *recordContextp)
{
    UpnpDBase::Record *recordp = (UpnpDBase::Record *) recordContextp;
    MFANUpnpGenresSel *genresSel;

    genresSel = (__bridge MFANUpnpGenresSel *) callbackContextp;
    [(genresSel->_genresArray)
	addObject: [NSString stringWithUTF8String: recordp->_genre.c_str()]];
    return 0;
}

int32_t
addSongsForGenresHelper(void *callbackContextp, void *recordContextp)
{
    MFANUpnpGenresSel *genresSel;
    MFANUpnpItem *newItem;

    UpnpDBase::Record *recordp = (UpnpDBase::Record *) recordContextp;

    genresSel = (__bridge MFANUpnpGenresSel *) callbackContextp;
    newItem = [[MFANUpnpItem alloc] init];
    newItem.title = [NSString stringWithUTF8String: recordp->_title.c_str()];
    newItem.artist = [NSString stringWithUTF8String: recordp->_artist.c_str()];
    newItem.album = [NSString stringWithUTF8String: recordp->_album.c_str()];
    newItem.url = [NSString stringWithUTF8String: recordp->_url.c_str()];
    newItem.genre = [NSString stringWithUTF8String: recordp->_genre.c_str()];
    newItem.urlArtwork = [NSString stringWithUTF8String: recordp->_artUrl.c_str()];
    
    [(genresSel->_songsArray) addObject: newItem];
    NSLog(@"in callback %s %p", recordp->_title.c_str(), genresSel->_songsArray);
    return 0;
}

- (BOOL) populateFromSearch: (NSString *)searchString async: (BOOL *) isAsyncp
{
    return NO;
}

- (int) rowHeight
{
    return 80;
}

- (void) checkInit
{
    MFANTopUpnp *upnp;
    long newVersion;

    upnp = [MFANTopUpnp getGlobalUpnp];
    if (!upnp)
	return;
    _dbasep = (UpnpDBase *) [upnp getDBase];
    if (!_dbasep)
	return;

    newVersion = _dbasep->getVersion();
    if (newVersion != _processedVersion) {
	/* copy the updated database contents into our state */
	NSLog(@"array is at %p", _songsArray);
	[_songsArray removeAllObjects];
	[_genresArray removeAllObjects];
	_dbasep->applyTree( _dbasep->genreTree(),
			    addGenresHelper,
			    (__bridge void *) self,
			    1);
	_dbasep->apply(addSongsForGenresHelper, (__bridge void *) self, 1);
	_processedVersion = newVersion;
    }
}

- (MFANUpnpGenresSel *) init
{
    self = [super init];
    if (self) {
	_songsArray = [[NSMutableArray alloc] init];
	_genresArray = [[NSMutableArray alloc] init];
	_processedVersion = 0;
	_defaultImage = [UIImage imageNamed: @"recordfield.jpg"];
    }
    return self;
}

- (NSString *) nameByIx: (int) ix
{
    return [_genresArray objectAtIndex: ix];
}

- (UIImage *) imageByIx: (int) ix size: (CGFloat) size
{
    /* can we do better? */
    return resizeImage(_defaultImage, size);
}

- (NSString *) subtitleByIx: (int) ix
{
    return @"";
}

- (BOOL) hasChildByIx: (int) ix
{
    return YES;
}

- (id<MFANMediaSel>) childByIx: (int) ix
{
    MFANUpnpSongsSel *songsSel;

    songsSel = [[MFANUpnpSongsSel alloc] initWithGenreName: _genresArray[ix]];

    return songsSel;

}

- (MFANScanItem *) scanItemByIx: (int) ix
{
    MFANScanItem *scan;

    scan = [[MFANScanItem alloc] init];
    /* so the genre gets displayed as visible representation of scanItem */
    scan.title = _genresArray[ix];

    /* key actually used for searching */
    scan.genreKey = _genresArray[ix];
    scan.scanFlags = [MFANScanItem scanUpnpGenre];
    scan.minStars = 0;
    scan.cloud = 0;

    return scan;
}

- (NSArray *) mediaItemsForScan: (MFANScanItem *) scan
{
    NSMutableArray *itemsArray;
    MFANMediaItem *mediaItem;
    MFANUpnpItem *upnpItem;

    [self checkInit];

    if (scan.items == nil) {
	itemsArray = [[NSMutableArray alloc] init];
	for(upnpItem in _songsArray) {
	    if ([scan.genreKey isEqualToString: upnpItem.genre]) {
		mediaItem = [[MFANMediaItem alloc]
				initWithUrl: @""
				title: upnpItem.title
				albumTitle: upnpItem.album];
		mediaItem.urlRemote = upnpItem.url;
		mediaItem.urlArtwork = [MFANDownload artUrlForHash: upnpItem.urlArtwork
						     extension: @"jpg"];
		[itemsArray addObject: mediaItem];
	    }
	}
	scan.items = itemsArray;
	return itemsArray;
    }
    else {
	return scan.items;
    }
}

/* when a new PopEdit is fired up, it calls count early on, which gives us a chance
 * to check that we're running with the results from the latest UPNP database; that's
 * what we're doing with checkInit.
 */
- (long) count {
    [self checkInit];

    return [_genresArray count];
}

- (BOOL) hasRssItems
{
    return NO;
}
@end

@implementation MFANRecordingsSel {
    /* array of MFANMediaItem objects */
    long _dirMTime;
    NSArray *_recordingsArray;
}

- (BOOL) supportsLocalAdd
{
    return NO;
}

- (int) localCount
{
    return (int) [_recordingsArray count];
}

- (BOOL) populateFromSearch: (NSString *)searchString async: (BOOL *) isAsyncp
{
    return NO;
}

- (int) rowHeight
{
    return 80;
}

- (void) loadDir
{
    struct stat tstat;
    NSString *docDir;
    int code;

    /* stat dir to see if it changed, and re-read contents if it did change */
    docDir = fileNameForDoc(@".");
    code = stat([docDir cStringUsingEncoding: NSUTF8StringEncoding], &tstat);
    if (_dirMTime != tstat.st_mtimespec.tv_sec) {
	_recordingsArray = [[NSFileManager defaultManager] contentsOfDirectoryAtPath: docDir
							   error: NULL];
	if (_recordingsArray == nil)
	    _recordingsArray = [[NSArray alloc] init];
	_dirMTime = tstat.st_mtimespec.tv_sec;
    }
}

- (MFANRecordingsSel *) init
{
    self = [super init];
    if (self) {
	_dirMTime = 0;
	[self loadDir];
    }
    return self;
}

- (NSString *) nameByIx: (int) ix
{
    return _recordingsArray[ix];
}

- (UIImage *) imageByIx: (int) ix size: (CGFloat) size
{
    return nil;
}

- (NSString *) subtitleByIx: (int) ix
{
    return nil;
}

- (BOOL) hasChildByIx: (int) ix
{
    return NO;
}

- (id<MFANMediaSel>) childByIx: (int) ix
{
    return nil;
}

- (MFANScanItem *) scanItemByIx: (int) ix
{
    MFANScanItem *scan;

    [self loadDir];

    scan = [[MFANScanItem alloc] init];
    scan.title = _recordingsArray[ix];
    scan.secondaryKey = @"[Inet Recording]";
    scan.scanFlags = [MFANScanItem scanRecording];
    scan.minStars = 0;
    scan.cloud = 0;
    scan.items = nil;

    [self mediaItemsForScan: scan];

    return scan;
}

- (NSArray *) mediaItemsForScan: (MFANScanItem *) scan
{
    NSMutableArray *itemsArray;
    NSString *fileName;
    MFANMediaItem *item;

    if (scan.items == nil) {
	itemsArray = [[NSMutableArray alloc] init];
	fileName = scan.title;
	item = [[MFANMediaItem alloc]
		   initWithUrl: @""
		   title: scan.title
		   albumTitle: @"Recording"];
	item.url = [NSString stringWithFormat: @"mld://%@", scan.title];
	item.urlArtwork = nil;
	[itemsArray addObject: item];
	scan.items = itemsArray;
	return itemsArray;
    }
    else
	return scan.items;
}

- (long) count {
    return [_recordingsArray count];
}

- (BOOL) hasRssItems
{
    return NO;
}
@end

/* A SetList is a list of selected music, and also contains a pointer
 * to the query that generated the list (not sure that's used).
 */
@implementation MFANSetList {
    NSArray *_scanArray;			/* of scanItems: basically, our query */
    MPMediaItemCollection *_itemCollectionp;	/* evaluated query */
    NSMutableArray *_itemArray;			/* array of MFANMediaItem entries */
}

/* once per system, each a dictionary of MFANQueryResults objects.
 * Each query result object has a name and a set of media items; the
 * name is the key (returned by the dictionary's iterator).
 */
#define MFANSETLIST_ITEMCOUNT				2
MFANArtistsSel *_artistsSel;
MFANPlaylistsSel *_playlistsSel;
MFANAlbumsSel *_albumsSel;
MFANSongsSel *_songsSel;
MFANPodcastsSel *_podcastsSel;
MFANGenresSel *_genresSel;
MFANRadioSel *_radioSel;
MFANUpnpSongsSel *_upnpSongsSel;
MFANUpnpAlbumsSel *_upnpAlbumsSel;
MFANUpnpArtistsSel *_upnpArtistsSel;
MFANUpnpGenresSel *_upnpGenresSel;
MFANRecordingsSel *_recordingsSel;
MPMediaItem *_items[MFANSETLIST_ITEMCOUNT];	/* two distinct items */

NSMutableDictionary *_idToItemMap;
NSMapTable *_itemToIdMap;

BOOL _didInit;
BOOL _didNotificationInit;

/* time library last updated */
BOOL _libraryChanged;

UIAlertView *_alertView;

/* sort an array of mfanItems, based on MPMediaItem's album order
 * field (or 0 if none present), Perform the same permutation on the
 * peer1 array, if present.
 */
+ (void) sortAlbum: (NSMutableArray *) mpItems
	  auxItems: (NSMutableArray *) peer1
{
    uint32_t count;
    uint16_t *valuesp;
    uint32_t i;
    uint32_t didAny;
    MPMediaItem *item;
    NSObject *tobj;
    uint16_t tshort;
    NSNumber *numberp;

    count = (uint16_t) [mpItems count];
    valuesp = new uint16_t [count];
    for(i=0;i<count; i++) {
	item = mpItems[i];
	if (item == nil)
	    valuesp[i] = 0;
	else {
	    numberp = [item valueForProperty: MPMediaItemPropertyAlbumTrackNumber];
	    if (numberp == nil) {
		valuesp[i] = 0;
	    }
	    else {
		valuesp[i] = [numberp unsignedIntegerValue];
	    }
	}
    }

    /* OK, it's a bubble sort, but for N == 20, doesn't matter, and
     * this is for sorting arrays of albums.
     */
    didAny = 1;
    while(didAny) {
	didAny = 0;
	for(i=0;i<count-1;i++) {
	    if (valuesp[i] > valuesp[i+1]) {
		/* swap i and i+1 */
		tshort = valuesp[i+1];
		valuesp[i+1] = valuesp[i];
		valuesp[i] = tshort;

		/* swap mfanItems */
		tobj = mpItems[i+1];
		mpItems[i+1] = mpItems[i];
		mpItems[i] = tobj;

		/* and perform the corresponding swap to peer1 array, if one
		 * is provided.
		 */
		if (peer1 != nil) {
		    tobj = peer1[i+1];
		    peer1[i+1] = peer1[i];
		    peer1[i] = tobj;
		}
		didAny = 1;
	    }
	}
    }
}

/* called with an array of MFANMediaItems, returns true iff there is at least
 * one RSS feed item (e.g. a podcast).
 */
+ (BOOL) arrayHasRssItems: (NSArray *) scanArray
{
    MFANScanItem *scanItem;
    BOOL rvalue;
    int podcastFlags;


    rvalue = NO;
    podcastFlags = [MFANScanItem scanPodcast];
    for(scanItem in scanArray) {
	if ([scanItem scanFlags] & podcastFlags) {
	    rvalue = YES;
	    break;
	}
    }
    return rvalue;
}

- (BOOL) hasRssItems
{
    return [MFANSetList arrayHasRssItems: _scanArray];
}

- (NSArray *) queryInfo
{
    return _scanArray;
}

- (NSMutableArray *) itemArray
{
    return _itemArray;
}

- (long) count
{
    return (_itemArray != nil? [_itemArray count] : 0);
}

- (void) randomizeSeed
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    srand((int) tv.tv_sec);
}

/* just set the raw media array, from the media list editor */
- (void) setMediaArray: (NSMutableArray *) itemArray
{
    _itemArray = itemArray;

    if ([_itemArray count] > 0)
	_itemCollectionp = [[MPMediaItemCollection alloc] initWithItems: _itemArray];
    else
	_itemCollectionp = nil;
}

- (NSMutableArray *) mergeItems: (NSArray *) aitems
		      withItems: (NSArray *) bitems
		       fromDate: (long) podcastDate
{
    long acount;
    long bcount;
    long aix;
    long bix;
    NSMutableArray *outArray;
    MFANMediaItem *aitem;
    MFANMediaItem *bitem;

    acount = [aitems count];
    bcount = [bitems count];
    aix = 0;
    bix = 0;
    outArray = [[NSMutableArray alloc] init];
    while( aix < acount || bix < bcount) {
	if (aix < acount)
	    aitem = [aitems objectAtIndex: aix];
	else
	    aitem = nil;

	if (bix < bcount)
	    bitem = [bitems objectAtIndex: bix];
	else
	    bitem = nil;

	/* skip bitems that are earlier than our podcast date; these
	 * represent podcasts we've heard already and deleted and don't
	 * want to add in again.  Argument podcastDate will be 0 if
	 * we want to restore all.
	 */
	if (bitem && bitem.podcastDate <= podcastDate) {
	    bix++;
	    continue;
	}

	if (aitem == nil) {
	    [outArray addObject: bitem];
	    bix++;
	}
	else if (bitem == nil) {
	    [outArray addObject: aitem];
	    aix++;
	}
	else {
	    if (aitem.podcastDate < bitem.podcastDate) {
		[outArray addObject: aitem];
		aix++;
	    }
	    else if (aitem.podcastDate > bitem.podcastDate) {
		[outArray addObject: bitem];
		bix++;
	    }
	    else if ([aitem.title isEqualToString: bitem.title]) {
		/* equal date and name, so same object */
		[outArray addObject: bitem];
		aix++;
		bix++;
	    }
	    else {
		/* same time, but different titles, so just pick one, say aitem */
		[outArray addObject: aitem];
		aix++;
	    }
	}
    }

    return outArray;
}

- (int) setQueryInfo: (NSArray *) scanArray ids: (NSArray *) ids
{
    MFANScanItem *scanItem;
    NSArray *itemsArray;
    int podcastFlags;
    BOOL isPodcastList;

    podcastFlags = [MFANScanItem scanPodcast];

    /* save for later */
    _scanArray = scanArray;

    /* initialize our query and query results to empty */
    _itemArray = [[NSMutableArray alloc] init];

    isPodcastList = NO;
    for(scanItem in scanArray) {
	if ([scanItem scanFlags] & podcastFlags) {
	    isPodcastList = YES;
	    break;
	}
    }
    /* make sure we have the list of artists, playlists, etc setup */
    [MFANSetList doSetup: nil force: NO];

    if (ids == nil) {
	if (!isPodcastList) {
	    for(scanItem in scanArray) {
		itemsArray = [scanItem performFilters: [scanItem mediaItems]];
		[_itemArray addObjectsFromArray: itemsArray];
	    }
	}
	else {
	    /* podcasts, sort by date */
	    for (scanItem in scanArray) {
		itemsArray = [scanItem performFilters: [scanItem mediaItems]];
		_itemArray = [self mergeItems: _itemArray
				   withItems: itemsArray
				   fromDate: 0];
	    }
	}
    }
    else {
	/* we're passed a set of specific songs in a specific order to use */
	NSObject *obj;
	NSNumber *number;
	MPMediaItem *item;
	MFANMediaItem *mfanItem;

	for(obj in ids) {
	    if ([obj isKindOfClass: [NSNumber class]]) {
		number = (NSNumber *) obj;
		item = [_idToItemMap objectForKey: number];
		if (item != nil) {
		    mfanItem = [[MFANMediaItem alloc] initWithMediaItem: item];
		    [_itemArray addObject: mfanItem];
		}
	    }
	    else if ([obj isKindOfClass: [MFANMediaItem class]]) {
		mfanItem = (MFANMediaItem *) obj;
		[_itemArray addObject: mfanItem];
	    }
	}
    }

    _itemCollectionp = nil;

    return 0;
}

- (void) randomize
{
    long count;
    long modulus;
    int i;
    int j;
    MPMediaItem *song;
    MPMediaItem *otherSong;

    /* now, randomize the set of songs */
    count = [_itemArray count];
    modulus = (long) primeLE(count);
    [self randomizeSeed];
    for(i=0;i<count;i++) {
	j = rand() % count;
	song = _itemArray[j];
	otherSong = _itemArray[i];
	_itemArray[i] = song;
	_itemArray[j] = otherSong;
    }
}

/* match something like "p.fl" against "pink floyd" -- assume both the query
 * and the name string have been lowercased already.
 */
+ (BOOL) matchNameAtom: (NSString *) queryAtomp name: (NSString *) namep
{
    unichar tn;
    unichar tq;
    int iq;
    int jn;
    int kn;
    long qcount;
    long ncount;
    int match;
    int sawSpace;

    qcount = [queryAtomp length];
    ncount = [namep length];

    if (ncount == 0 || qcount == 0)
	return NO;

    /* note that the body must adjust jn */
    for(jn=0; jn<ncount;) {
	match = 1;
	kn = jn;
	/* at this point, try to match the query against the subset
	 * of the name string starting at jn.
	 */
	for(iq=0; iq<qcount&&kn<ncount; iq++) {
	    tq = [queryAtomp characterAtIndex:iq];
	    if (tq == '.') {
		/* skip the rest of this name token, and then any trailing spaces */
		sawSpace = 0;
		for(; kn<ncount; kn++) {
		    tn = [namep characterAtIndex: kn];
		    if (!sawSpace) {
			if (tn != ' ') {
			    continue;
			}
			else {
			    sawSpace = 1;
			    continue;
			}
		    }
		    else {
			/* we've seen a space so all we want to do is skip over the remainder
			 * of the spaces.
			 */
			if (tn == ' ')
			    continue;
			break;
		    }
		} /* for loop over characters in name matching '.' */

		/* at this point, we've skipped the chars matching the '.', so we have
		 * to skip the '.' too.
		 */
		continue;
	    }
	    if (tq != [namep characterAtIndex:kn]) {
		/* we have a mismatch in the name */
		match = 0;
		break;
	    }
	    else {
		/* characters match, continue on.  iq will be incremented above;
		 * we have to do kn ourselves.
		 */
		kn++;
	    }
	} /* loop over entire query string at name position jn */

	/* if we didn't use the whole query string, then we didn't match */
	if (iq < qcount)
	    match = 0;

	/* if we matched at any position, we have success */
	if (match)
	    return YES;

	/* at this point, we've failed to match due to a problem at
	 * namep[kn].  Skip name charas we until a space, and then we
	 * skip the remainder of the spaces.
	 */
	while(kn < ncount) {
	    tn = [namep characterAtIndex:kn];
	    if (tn != ' ') {
		kn++;
		continue;
	    }
	    break;
	}
	while(kn < ncount) {
	    tn = [namep characterAtIndex:kn];
	    if (tn == ' ') {
		kn++;
		continue;
	    }
	    break;
	}

	/* now adjust jn so we continue searching at the next token after
	 * the token we failed at.
	 */
	jn = kn;
    } /* loop over all possible positions */

    /* if we make it here, we were unable to make a match of the query string against
     * the name string at any position.
     */
    return NO;
}

- (MPMediaItemCollection *) itemCollection
{
    return _itemCollectionp;
}

- (BOOL) isEmpty
{
    return [_itemArray count] == 0;
}

- (MPMediaItem *) getFirst
{
    if ([_itemArray count] > 0)
	return _itemArray[0];
    else
	return nil;
}

+ (void) doSetup: (MFANIndicator *) ind force: (BOOL) forced
{
    MPMediaQuery *myQueryp;
    MPMediaItem *item;
    NSArray *songsArray;
    NSArray *podcastsArray;
    NSNumber *idNumber;
    long count;
    int i;
    MPMediaLibrary *library;

    if (forced || (_didInit == NO)) {
	[ind setTitle: @"Reading songs..."];
	_playlistsSel = [[MFANPlaylistsSel alloc] init];
	_artistsSel = [[MFANArtistsSel alloc] init];
	_songsSel = [[MFANSongsSel alloc] init];
	_podcastsSel = [[MFANPodcastsSel alloc] init];
	_genresSel = [[MFANGenresSel alloc] init];
	_radioSel = [[MFANRadioSel alloc] init];
	_upnpSongsSel = [[MFANUpnpSongsSel alloc] init];
	_upnpAlbumsSel = [[MFANUpnpAlbumsSel alloc] init];
	_upnpArtistsSel = [[MFANUpnpArtistsSel alloc] init];
	_upnpGenresSel = [[MFANUpnpGenresSel alloc] init];
	_recordingsSel = [[MFANRecordingsSel alloc] init];

	/* and lookup persistentId for songs; this is the biggest time
	 * user in initialization, but we can't do it asynchronously because
	 * we can't read playlists without it, and we have to at least read the
	 * first playlist to start things going.
	 */
	myQueryp = [MPMediaQuery songsQuery];
	songsArray = [myQueryp items];
	i = 0;
	count = [songsArray count];
	_idToItemMap = [[NSMutableDictionary alloc] init];
	_itemToIdMap = [[NSMapTable alloc] init];
	for(item in songsArray) {
	    if (i<MFANSETLIST_ITEMCOUNT)
		_items[i] = item;
	    [ind setProgress: (float) i / count];
	    idNumber = [item valueForProperty: MPMediaItemPropertyPersistentID];

	    [_idToItemMap setObject: item forKey: idNumber];
	    [_itemToIdMap setObject: idNumber forKey: item];
	    i++;
	}

	myQueryp = [MPMediaQuery podcastsQuery];
	podcastsArray = [myQueryp items];
	for(item in podcastsArray) {
	    idNumber = [item valueForProperty: MPMediaItemPropertyPersistentID];
	    [_idToItemMap setObject: item forKey: idNumber];
	    [_itemToIdMap setObject: idNumber forKey: item];
	}

	/* do this last so it doesn't interfere with song processing */
	_albumsSel = [[MFANAlbumsSel alloc] initWithInd: ind];

	NSLog(@"- All done with synchronous inits");

	_didInit = YES;
	_libraryChanged = NO;
    }

    if (!_didNotificationInit) {
	[[NSNotificationCenter defaultCenter]
	    addObserver: self
	    selector: @selector(libraryChanged:)
	    name: MPMediaLibraryDidChangeNotification
	    object: library];
	[library beginGeneratingLibraryChangeNotifications];
	_didNotificationInit = YES;

	/* reset this again in case a spurious notification sent at the start */
	_libraryChanged = NO;
    }
}

/* used to get a song from the library, just for padding the list */
+ (MPMediaItem *) getItem: (int) ix
{
    if (ix < MFANSETLIST_ITEMCOUNT)
	return _items[ix];
    else
	return nil;
}

+ (void) libraryChanged: (id) junk
{
    _libraryChanged = YES;
}

- (MFANSetList *) init {

    self = [super init];
    if (self) {
	/* need to do this once in higher level app */
	_itemArray = [[NSMutableArray alloc] init];
    }

    return self;
}

- (MFANMediaItem *) itemWithIndex: (long) ix
{
    if ([_itemArray count] > ix) {
	return [_itemArray objectAtIndex: ix];
    }
    else {
	return nil;
    }
}

- (NSObject *) persistentIdWithIndex: (long) ix
{
    MFANMediaItem *mfanItem;
    MPMediaItem *item;
    NSNumber *number;

    if ([_itemArray count] > ix) {
	mfanItem = [_itemArray objectAtIndex: ix];
	item = [mfanItem item];
	if (item != nil) {
	    number = [_itemToIdMap objectForKey: item];
	    return number;
	}
	else {
	    return mfanItem;
	}
    }

    return nil;
}

+ (id<MFANMediaSel>) albumsSel
{
    return _albumsSel;
}

+ (id<MFANMediaSel>) artistsSel
{
    return _artistsSel;
}

+ (id<MFANMediaSel>) playlistsSel
{
    return _playlistsSel;
}

+ (id<MFANMediaSel>) songsSel
{
    return _songsSel;
}

+ (id<MFANMediaSel>) radioSel
{
    return _radioSel;
}

+ (id<MFANMediaSel>) podcastsSel
{
    return _podcastsSel;
}

+ (id<MFANMediaSel>) genresSel
{
    return _genresSel;
}

+ (id<MFANMediaSel>) upnpSongsSel
{
    return _upnpSongsSel;
}

+ (id<MFANMediaSel>) upnpAlbumsSel
{
    return _upnpAlbumsSel;
}

+ (id<MFANMediaSel>) upnpArtistsSel
{
    return _upnpArtistsSel;
}

+ (id<MFANMediaSel>) upnpGenresSel
{
    return _upnpGenresSel;
}

+ (id<MFANMediaSel>) recordingsSel
{
    return _recordingsSel;
}

+ (void) forceResync
{
    _libraryChanged = YES;

    [MFANSetList checkLibrary];
}

+ (void) checkLibrary
{
    NSTimer *timer;

    if (_libraryChanged) {
	_alertView = [[UIAlertView alloc]
			 initWithTitle:@"Resyncing Library"
			 message:@"iPod Library has changed -- reprocessing"
			 delegate:nil 
			 cancelButtonTitle:nil
			 otherButtonTitles:nil];

	timer = [NSTimer scheduledTimerWithTimeInterval: 0.2
			 target:self
			 selector:@selector(checkLibraryPart2:)
			 userInfo:nil
			 repeats: NO];

	[_alertView show];
    }
}

+ (void) checkLibraryPart2: (id) junk
{
    /* doSetup turns off _libraryChanged */
    [MFANSetList doSetup: nil force: YES];

    [_alertView dismissWithClickedButtonIndex: 0 animated: YES];

    _alertView = nil;
}

@end

// how to set a user rating key:
// [mediaItem setValue:[NSNumber numberWithInteger:rating] forKey:@"rating"];
