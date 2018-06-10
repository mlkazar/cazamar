//
//  MFANDownload.h
//  DJ To Go
//
//  Created by Michael Kazar on 1/28/15.
//  Copyright (c) 2015 Mike Kazar. All rights reserved.
//

#ifndef DJ_To_Go_MFANDownload_h
#define DJ_To_Go_MFANDownload_h

#import <UIKit/UIKit.h>
#import <Foundation/Foundation.h>
#import <MediaPlayer/MediaPlayer.h>

@class MFANPlayContext;
@class MFANMediaItem;

@interface MFANDownloadReq : NSObject
/* we pass in a remote URL and a file to put the data in.  We have an optional
 * mediaItem whose _url field is set to the internal URL for a downloaded file
 * on a download success.
 */
@property NSString *urlRemote;
@property NSString *localPath;
@property MFANMediaItem *mediaItem;

- (MFANDownloadReq *) initWithUrlRemote: (NSString *) urlRemote localPath: (NSString *) path;

@end

@interface MFANDownload : NSObject <NSURLSessionDelegate>

- (MFANDownload *) initWithPlayContext: (MFANPlayContext *) playContext;

+ (NSString *) fileUrlFromMlkUrl: (NSString *) mlkUrl;

+ (NSString *) docUrlFromMlkUrl: (NSString *) mlkUrl;

+ (NSString *) fileNameFromMlkUrl: (NSString *) mlkUrl;

+ (void) deleteDownloadedContent;

- (BOOL) unloadItem: (MFANMediaItem *) item;

- (BOOL) loadItem: (MFANMediaItem *) item;

- (BOOL) loadUrlInternal: (MFANDownloadReq *) downloadReq wait: (BOOL) waitFlag;

- (BOOL) loadItemForUrl: (NSString *) remoteUrl name: (NSString *) name;

- (int) percentForItem: (MFANMediaItem *) mfanItem;

- (BOOL) isLoading: (MFANMediaItem *) mfanItem;

- (void) checkDownloaded: (MFANMediaItem *) mfanItem;

+ (NSString *) fileNameForFile: (NSString *) fileName;

+ (NSString *) artFileNameForHash: (NSString *) remoteUrl extension: (NSString *) ext;

+ (BOOL) artFileNamePresentForHash: (NSString *) remoteUrl;

+ (NSString *) artUrlForHash: (NSString *) remoteUrl extension: (NSString *) ext;

- (void) setDownloadAll: (BOOL) downloadAll;

- (long) unloadedCount;

- (int) downloadCount;

- (BOOL) downloadAll;

- (BOOL) isIdle;

- (void) checkDownloadedArray: (NSArray *) itemArray;
@end

#endif
