//
//  MFANSetList.h
//  MusicFan
//
//  Created by Michael Kazar on 4/28/14.
//  Copyright (c) 2014 Mike Kazar. All rights reserved.
//

#import <Foundation/Foundation.h>
#import "MFANMediaSel.h"

@class MPMediaItemCollection;
@class MPMediaItem;
@class MFANIndicator;
@class MFANMediaItem;

@interface MFANUpnpItem : NSObject {
    /* blah */
}

@property NSString *title;
@property NSString *album;
@property NSString *artist;
@property NSString *genre;
@property NSString *url;
@property NSString *urlArtwork;

- (MFANUpnpItem *) init;

@end

@interface MFANAsyncInit : NSObject
- (MFANAsyncInit *) initWithObject: (id) object sel: (SEL) sel;

- (void) initDone;

- (void) waitForInitDone;
@end

@interface MFANChildAlbumsSel : NSObject<MFANMediaSel>

- (MFANChildAlbumsSel *) init: (NSArray *) items artist: (NSString *) artist;

- (MFANChildAlbumsSel *) initSimple;

@end

@interface MFANChildSongsSel : NSObject<MFANMediaSel> 
- (MFANChildSongsSel *) initWithMFANItems: (NSArray *) items;

- (MFANChildSongsSel *) initWithMPItems: (NSArray *) items;
@end

@interface MFANPlaylistsSel : NSObject <MFANMediaSel>
@end

@interface MFANArtistsSel : NSObject <MFANMediaSel>
@end

@interface MFANAlbumsSel : NSObject <MFANMediaSel>
- (void) bkgInit: (MFANAsyncInit *) async;
@end

@interface MFANSongsSel : NSObject <MFANMediaSel>
@end

@interface MFANPodcastsSel : NSObject <MFANMediaSel>
@end

@interface MFANGenresSel : NSObject <MFANMediaSel>
@end

@interface MFANRadioSel : NSObject <MFANMediaSel>
@end

@interface MFANUpnpSongsSel : NSObject <MFANMediaSel>
@end

@interface MFANUpnpArtistsSel : NSObject <MFANMediaSel>
@end

@interface MFANUpnpAlbumsSel : NSObject <MFANMediaSel>
@end

@interface MFANUpnpGenresSel : NSObject <MFANMediaSel>
@end

@interface MFANRecordingsSel : NSObject <MFANMediaSel>
@end

@interface MFANSetList : NSObject

+ (void) sortAlbum: (NSMutableArray *) mfanItems
	  auxItems: (NSMutableArray *) peer1;

+ (void) doSetup: (MFANIndicator *) ind force: (BOOL) forced;

+ (void) checkLibrary;

+ (void) checkLibraryPart2: (id) junk;

+ (void) forceResync;

+ (BOOL) arrayHasRssItems: (NSArray *) scanArray;

- (BOOL) hasRssItems;

- (int) setQueryInfo: (NSArray *) scanArray ids: (NSArray *) ids;

- (void) setMediaArray: (NSMutableArray *) itemArray;

- (NSArray *) queryInfo;

- (MPMediaItemCollection *) itemCollection;

- (long) count;

- (MPMediaItem *) getFirst;

- (MFANMediaItem *) itemWithIndex: (long) ix;

- (NSObject *) persistentIdWithIndex: (long) ix;

- (NSMutableArray *) itemArray;

- (BOOL) isEmpty;

+ (MPMediaItem *) getItem: (int) ix;

- (NSMutableArray *) mergeItems: (NSArray *) aitems
		      withItems: (NSArray *) bitems
		       fromDate: (long) podcastDate;

+ (id<MFANMediaSel>) albumsSel;

+ (id<MFANMediaSel>) artistsSel;

+ (id<MFANMediaSel>) playlistsSel;

+ (id<MFANMediaSel>) songsSel;

+ (id<MFANMediaSel>) podcastsSel;

+ (id<MFANMediaSel>) genresSel;

+ (id<MFANMediaSel>) radioSel;

+ (id<MFANMediaSel>) upnpSongsSel;

+ (id<MFANMediaSel>) upnpAlbumsSel;

+ (id<MFANMediaSel>) upnpArtistsSel;

+ (id<MFANMediaSel>) upnpGenresSel;

+ (id<MFANMediaSel>) recordingsSel;

- (void) randomize;

@end
