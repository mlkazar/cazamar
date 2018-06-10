//
//  MFANMediaSel.h
//  MusicFan
//
//  Created by Michael Kazar on 6/4/2014
//  Copyright (c) 2014 Mike Kazar. All rights reserved.
//

/* one of these for each line describing what we're scanning for */

@protocol MFANMediaSel;

@interface MFANScanItem : NSObject

@property NSString *title;      /* this is primary key */
@property NSString *secondaryKey;
@property NSString *genreKey;
@property int scanFlags;
@property long minStars;
@property int cloud;
@property long podcastDate;	/* date of latest podcast seen */
@property NSArray *items;

+ (int) scanArtist;
+ (int) scanSong;
+ (int) scanAlbum;
+ (int) scanPlaylist;
+ (int) scanChildAlbum;
+ (int) scanRadio;
+ (int) scanPodcast;
+ (int) scanUpnpSong;
+ (int) scanUpnpArtist;
+ (int) scanUpnpAlbum;
+ (int) scanUpnpGenre;
+ (int) scanRecording;

- (MFANScanItem *) initWithTitle: (NSString *) title type: (int) flags;

- (MFANScanItem *) init;

- (NSArray *) mediaItems;

- (NSArray *) performFilters: (NSArray *) array;

- (long) mediaCount;

+ (id<MFANMediaSel>) mediaSelFromFlags: (int) flags;

@end

/* very simple protocol for adding an scan item */
@protocol MFANAddScan

- (void) addScanItem: (MFANScanItem *) scan;

@end /* MFANAddItem */

/* One of these for a music menu level.  It can supply data to a table
 * view.  We typically return an array of these to represent a new table view's
 * data. 
 */
@protocol MFANMediaSel
- (BOOL) populateFromSearch: (NSString *) searchString;

- (NSString *) nameByIx: (int) ix;

- (UIImage *) imageByIx: (int) ix size: (CGFloat) size;

- (NSString *) subtitleByIx: (int) ix;

- (id<MFANMediaSel>) childByIx: (int) ix;

- (int) rowHeight;

- (BOOL) hasChildByIx: (int) ix;

/* this is called as we walk down the media tree.  It should fill in the _items array
 * at that time.
 */ 
- (MFANScanItem *) scanItemByIx: (int) ix;

/* this function is used when reexecuting a query after restarting the
 * app and adding new scanItems to the playlist.  It should load the
 * _items array as well, so that the items property is valid after
 * performing this operation.
 *
 * Normal users who just called scanItemByIx and want the associated
 * items should use the items property on the scanItem, not this call.
 */
- (NSArray *) mediaItemsForScan: (MFANScanItem *) scan;

- (long) count;

- (BOOL) hasRssItems;

/* if supports localAdd is set, we can add new entries, using the
 * prompt returned for the key and value from localAddPrompKey, and
 * setting the result in localAddKey.
 *
 * The UI in MFANPopEdit replaces the cloud and stars part of the
 * screen with the add button.
 */
- (BOOL) supportsLocalAdd;

/* count of editable media selections */
- (int) localCount;

@optional
- (BOOL) localAddPromptKey: (NSString **) keyPrompt value: (NSString **) value;

@optional
- (BOOL) localAddKey: (NSString *) key value: (NSString *) value;

@optional
- (BOOL) localRemoveEntry: (long) slot;
@end
