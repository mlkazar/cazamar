//
//  MFANDownload.m
//  DJ To Go
//
//  Created by Michael Kazar on 1/28/15.
//  Copyright (c) 2015 Mike Kazar. All rights reserved.
//

#import "MFANDownload.h"
#import "MFANPlayContext.h"
#import "MFANSetList.h"
#import "MFANCGUtil.h"
#import "MFANPlayerView.h"
#import "MFANTopSettings.h"
#import "MFANWarn.h"

@implementation MFANDownloadReq {
}

- (MFANDownloadReq *) initWithUrlRemote: (NSString *) urlRemote localPath: (NSString *) path
{
    self = [super init];
    if (self) {
	_urlRemote = urlRemote;
	_localPath = path;
	_mediaItem = nil;
    }

    return self;
}
@end

@implementation MFANDownload {
    MFANPlayContext __weak *_playContext;
    NSThread *_thread;
    MFANSetList *_setList;
    NSURLSessionDownloadTask *_downloadTask;
    NSString *_loadingFilePath;	/* /var/.... */
    NSString *_loadingFileUrl;	/* http://mit.edu/foo */
    BOOL _loadingNow;
    BOOL _loadingFailed;
    MFANDownloadReq *_loadingReq;
    int _loadingPercent;	/* percent of way guy above is loaded */
    NSMutableArray *_prioItems;	/* MFANMediaItems for high priority loading */
    NSCondition *_commLock;
    int _downloadCount;
    BOOL _downloadAll;
}

- (BOOL) isIdle
{
    BOOL ret;

    [_commLock lock];
    if ( [_prioItems count] > 0 || _loadingNow)
	ret = NO;
    else
	ret = YES;
    [_commLock unlock];

    return ret;
}

/* initialize the download system, typically for a particular playContext */
- (MFANDownload *) initWithPlayContext: (MFANPlayContext *) playContext
{
    self = [super init];

    if (self) {
	_playContext = playContext;
	_downloadCount = 0;
	_downloadAll = NO;
	_loadingNow = NO;
	_loadingReq = nil;
	_commLock = [[NSCondition alloc] init];
	if (_playContext != nil)
	    _setList = [_playContext setList];
	_prioItems = [[NSMutableArray alloc] init];

	_thread = [[NSThread alloc] initWithTarget: self
				    selector: @selector(bkgOp:)
				    object: nil];
	[_thread start];
    }

    return self;
}

- (void) setDownloadAll: (BOOL) downloadAll
{
    _downloadAll = downloadAll;
}

- (BOOL) downloadAll
{
    return _downloadAll;
}

- (BOOL) unloadItem: (MFANMediaItem *) mfanItem
{
    NSString *url;
    NSString *fileUrl;
    BOOL value;

    if (mfanItem != nil) {
	[_commLock lock];
	url = [mfanItem localUrl];
	if (url == nil || [url length] == 0) {
	    [_commLock unlock];
	    return YES;
	}
	/* we have something to unload */
	fileUrl = [MFANDownload fileUrlFromMlkUrl: url];
	value = [[NSFileManager defaultManager]
		    removeItemAtURL: [NSURL URLWithString: fileUrl]
		    error: nil];
	[mfanItem setUrl: @""];
	[_commLock unlock];
	return value;
    }
    else {
	return YES;
    }
}

/* queue a load request for a name (within the libdir), using the
 * specified remote URL.
 */
- (BOOL) loadItemForUrl: (NSString *) remoteUrl name: (NSString *) name
{
    MFANDownloadReq *req;
    NSArray *paths;
    NSString *libdir;
    NSString *value;

    paths = NSSearchPathForDirectoriesInDomains(NSLibraryDirectory, NSUserDomainMask, YES);
    libdir = [paths objectAtIndex:0];
    value = [NSString stringWithFormat: @"%@/%@", libdir, name];

    req = [[MFANDownloadReq alloc] initWithUrlRemote: remoteUrl
				   localPath: value];
    [_commLock lock];
    [_prioItems addObject: req];
    [_commLock unlock];

    return YES;
}

- (BOOL) loadItem: (MFANMediaItem *) mfanItem
{
    MFANDownloadReq *downloadReq;

    downloadReq = [[MFANDownloadReq alloc] initWithUrlRemote: mfanItem.urlRemote
					   localPath: [MFANDownload
							  fileNameForHash: mfanItem.urlRemote]];
    downloadReq.mediaItem = mfanItem;	/* so .url field can get set on success */
    
    [_commLock lock];
    [_prioItems addObject: downloadReq];
    [_commLock unlock];

    return YES;
}

/* typically runs in a background task, setting _loadingFailed
 * if something goes wrong. 
 */
- (BOOL) loadUrlInternal: (MFANDownloadReq *) downloadReq wait: (BOOL) waitFlag
{
    _loadingFilePath = downloadReq.localPath;
    _loadingFileUrl = downloadReq.urlRemote;

    if ([[NSFileManager defaultManager] fileExistsAtPath: _loadingFilePath]) {
	/* already downloaded */
	return YES;
    }

    /* so we can access this from callbacks */
    _loadingReq = downloadReq;
    _loadingPercent = 0;

    NSURLSessionConfiguration *ephemeralConfig =
	[NSURLSessionConfiguration ephemeralSessionConfiguration];
    ephemeralConfig.allowsCellularAccess = ([MFANTopSettings useCellForDownloads] &&
					    ![MFANTopSettings neverUseCellData]);

    NSURLSession *session =
	[NSURLSession sessionWithConfiguration: ephemeralConfig
		      delegate: self
		      delegateQueue: [NSOperationQueue mainQueue]];

    _loadingFailed = NO;

    _downloadTask = [session downloadTaskWithURL:
				 [NSURL URLWithString: _loadingFileUrl]];
    [_downloadTask resume];

    if (waitFlag) {
	while(1) {
	    sleep(3);
	    if ([_downloadTask state] != NSURLSessionTaskStateRunning) {
		/* download appears to be done */
		break;
	    }
	}

	if (downloadReq.mediaItem != nil) {
	    if (!_loadingFailed) {
		downloadReq.mediaItem.url = [MFANDownload urlForHash: downloadReq.urlRemote];
		downloadReq.mediaItem.playbackTime = 0.0;	/* reset on load */
	    }
	    else {
		downloadReq.mediaItem.url = @"";
	    }
	}
    }

    _downloadCount++;
    _loadingReq = NULL;

    return YES;
}

+ (NSString *) urlForHash: (NSString *) remoteUrl
{
    uint64_t sig;
    NSString *value;

    sig = signature(remoteUrl);
    value = [NSString stringWithFormat: @"mlk://podcast-%08llx.mp3", sig];
    return value;
}

+ (NSString *) fileNameForHash: (NSString *) remoteUrl
{
    NSArray *paths;
    NSString *libdir;
    uint64_t sig;
    NSString *value;

    sig = signature(remoteUrl);

    paths = NSSearchPathForDirectoriesInDomains(NSLibraryDirectory, NSUserDomainMask, YES);
    libdir = [paths objectAtIndex:0];
    value = [NSString stringWithFormat: @"%@/podcast-%08llx.mp3",
		      libdir, (long long) sig];

    return value;
}

+ (NSString *) fileNameForFile: (NSString *) fileName
{
    NSArray *paths;
    NSString *libdir;
    NSString *value;

    paths = NSSearchPathForDirectoriesInDomains(NSLibraryDirectory, NSUserDomainMask, YES);
    libdir = [paths objectAtIndex:0];
    value = [NSString stringWithFormat: @"%@/%@", libdir, fileName];

    return value;
}

/* extension should be @"jpg" or @"png" */
+ (NSString *) artFileNameForHash: (NSString *) remoteUrl extension: (NSString *) ext
{
    NSArray *paths;
    NSString *libdir;
    uint64_t sig;
    NSString *value;

    sig = signature(remoteUrl);

    paths = NSSearchPathForDirectoriesInDomains(NSLibraryDirectory, NSUserDomainMask, YES);
    libdir = [paths objectAtIndex:0];
    value = [NSString stringWithFormat: @"%@/artwork-%08llx.%@",
		      libdir, (long long) sig, ext];

    return value;
}

+ (NSString *) artUrlForHash: (NSString *) remoteUrl extension: (NSString *) ext
{
    uint64_t sig;
    NSString *value;

    sig = signature(remoteUrl);

    value = [NSString stringWithFormat: @"mlk://artwork-%08llx.%@", (long long) sig, ext];

    return value;
}

+ (BOOL) artFileNamePresentForHash: (NSString *) remoteUrl
{
    NSString *filePath;

    /* look for the possible files */
    filePath = [MFANDownload artFileNameForHash: remoteUrl extension: @"jpg"];
    if ([[NSFileManager defaultManager] fileExistsAtPath: filePath]) {
	return YES;
    }
    filePath = [MFANDownload artFileNameForHash: remoteUrl extension: @"png"];
    if ([[NSFileManager defaultManager] fileExistsAtPath: filePath]) {
	return YES;
    }

    return NO;
}

/* remove all downloaded content */
+ (void) deleteDownloadedContent
{
    NSArray *paths;
    NSString *libdir;
    NSArray *dirArray;
    NSString *entry;
    NSString *path;

    paths = NSSearchPathForDirectoriesInDomains(NSLibraryDirectory, NSUserDomainMask, YES);
    libdir = [paths objectAtIndex:0];
    dirArray = [[NSFileManager defaultManager]  contentsOfDirectoryAtPath: libdir
						error:nil];
    if (dirArray != nil) {
	for(entry in dirArray) {
	    if ([entry length] >= 8 &&
		[[entry substringToIndex: 7] isEqualToString: @"podcast"]) {
		path = [NSString stringWithFormat: @"%@/%@", libdir, entry];
		if (![[NSFileManager defaultManager] removeItemAtPath: path
						     error: nil]) {
		    NSLog(@"!Remove of %@ failed", path);
		}
	    }
	}
    }
}

+ (void) deleteTempFiles
{
    NSString *libdir;
    NSArray *dirArray;
    NSString *entry;
    NSString *path;

    libdir = NSTemporaryDirectory();
    dirArray = [[NSFileManager defaultManager] contentsOfDirectoryAtPath: libdir error:nil];
    if (dirArray != nil) {
	for(entry in dirArray) {
	    if (1) {
		path = [NSString stringWithFormat: @"%@/%@", libdir, entry];
		if (![[NSFileManager defaultManager] removeItemAtPath: path
						     error: nil]) {
		    NSLog(@"Failed to remove temp file %@", path);
		}
	    }
	}
    }
}

+ (NSString *) fileNameFromMlkUrl: (NSString *) mlkUrl
{
    NSArray *paths;
    NSString *libdir;
    NSString *value;

    /* starts with mlk:// followed by podcast-%08llx.mp3.  We want to replace
     * mlk:// with /<library-path> (which includes a leading '/')
     */
    paths = NSSearchPathForDirectoriesInDomains(NSLibraryDirectory, NSUserDomainMask, YES);
    libdir = [paths objectAtIndex:0];
    value = [NSString stringWithFormat: @"%@/%@", libdir, [mlkUrl substringFromIndex: 6]];
    return value;
}

+ (NSString *) fileUrlFromMlkUrl: (NSString *) mlkUrl
{
    NSArray *paths;
    NSString *libdir;
    NSString *value;

    /* starts with mlk:// followed by podcast-%08llx.mp3.  We want to replace
     * mlk:// with file://<library-path> (which includes a leading '/')
     */
    paths = NSSearchPathForDirectoriesInDomains(NSLibraryDirectory, NSUserDomainMask, YES);
    libdir = [paths objectAtIndex:0];
    value = [NSString stringWithFormat: @"file://%@/%@", libdir, [mlkUrl substringFromIndex: 6]];
    return value;
}

+ (NSString *) docUrlFromMlkUrl: (NSString *) mlkUrl
{
    NSArray *paths;
    NSString *libdir;
    NSString *value;

    /* starts with mlk:// followed by podcast-%08llx.mp3.  We want to replace
     * mlk:// with file://<document-path> (which includes a leading '/')
     */
    paths = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES);
    libdir = [paths objectAtIndex:0];
    value = [NSString stringWithFormat: @"file://%@/%@", libdir, [mlkUrl substringFromIndex: 6]];
    return value;
}

/* called with an array of podcast MFANMediaItems */
- (void) checkDownloadedArray: (NSArray *) itemArray
{
    MFANMediaItem *mfanItem;

    for(mfanItem in itemArray) {
	if (mfanItem.urlRemote != nil)
	    [self checkDownloaded: mfanItem];
    }
}

- (void) checkDownloaded: (MFANMediaItem *) mfanItem
{
    NSString *filePath;

    /* if we don't have a remote URL, this isn't a podcast */
    if (mfanItem.urlRemote == nil)
	return;

    filePath = [MFANDownload fileNameForHash: mfanItem.urlRemote];
    if ([[NSFileManager defaultManager] fileExistsAtPath: filePath]) {
	/* already downloaded */
	mfanItem.url = [MFANDownload urlForHash: mfanItem.urlRemote];
    }
    else {
	mfanItem.url = @"";
    }
}

/* get count of unloaded entries */
- (long) unloadedCount
{
    NSMutableArray *itemArray;
    MFANMediaItem *mfanItem;
    long count;

    itemArray = [_setList itemArray];
    count = 0;
    for(mfanItem in itemArray) {
	/* we have a remote URL, but no local one */
	if ( [mfanItem.urlRemote length] > 0 &&
	     [[mfanItem localUrl] length] == 0)
	    count++;
    }

    return count;
}

- (void) bkgOp: (id) cx
{
    NSMutableArray *itemArray;
    MFANDownloadReq *downloadReq;
    MFANMediaItem *mfanItem;

    while(1) {
	downloadReq = nil;
	[_commLock lock];
	_loadingNow = NO;
	[_commLock unlock];
	sleep(1);

	[_commLock lock];
	if ([_prioItems count] > 0) {
	    downloadReq = _prioItems[0];
	    [_prioItems removeObjectAtIndex: 0];
	    _loadingNow = YES;
	    [_commLock unlock];
	    [self loadUrlInternal: downloadReq wait: YES];
	    continue;
	}
	[_commLock unlock];

	/* if not supposed to download stuff, don't */
	if (![MFANTopSettings autoDownload] && !_downloadAll) {
	    continue;
	}

	itemArray = [_setList itemArray];
	/* LOCK */
	for(mfanItem in itemArray) {
	    if (!_downloadAll && ![MFANTopSettings autoDownload]) {
		break;
	    }

	    if ([_prioItems count] > 0) {
		/* someone added something at highre prio for us to load */
		break;
	    }

	    /* if nothing to download, or already downloaded */
	    if ( mfanItem.urlRemote == nil || mfanItem.urlRemote.length == 0 ||
		 [mfanItem.localUrl length] > 0)
		continue;

	    downloadReq = [[MFANDownloadReq alloc]
			      initWithUrlRemote: mfanItem.urlRemote
			      localPath: [MFANDownload fileNameForHash: mfanItem.urlRemote]];
	    downloadReq.mediaItem = mfanItem;	/* for _url assignment on success */

	    _loadingNow = YES;
	    [self loadUrlInternal: downloadReq wait:YES];
	}
	_downloadAll = NO;
	/* UNLOCK */
    }
}

		
- (void)  URLSession:(NSURLSession *)session
		task:(NSURLSessionTask *)task
didCompleteWithError:(NSError *)error
{
    MFANWarn *warn;

    /* leave loadingReq set until we're back in bkgOp */

    if (error != nil) {
	_loadingFailed = YES;
	NSLog(@"!Download task completed error-%p", error);
	warn = [[MFANWarn alloc] initWithTitle: @"Download Failed"
				 message: @"Download of item failed.\nPerhaps cell data disabled?"
				 secs: 2.0];
    }
}

/* called when download finishes */
- (void) URLSession: (NSURLSession *)session
       downloadTask:(NSURLSessionDownloadTask *)task
didFinishDownloadingToURL:(NSURL *)downloadedURL
{
    NSString *downloadedFile;
    NSFileManager *fileManager = [NSFileManager defaultManager];
    NSURL *permUrl;
    NSError *error;

    downloadedFile = [downloadedURL absoluteString];

    /* copy instead of move is OK, since the file is a temp file that should be
     * deleted when we return.
     */
    permUrl = [NSURL fileURLWithPath: _loadingFilePath];
    if ([fileManager copyItemAtURL: downloadedURL
		     toURL: permUrl
		     error: &error]) {
    }
    else {
	NSLog(@"!DOWNLOAD COPY FAILED to %@!!!", _loadingFileUrl);
	_loadingFailed = YES;
    }

    /* leave loadingReq set unti we're back at bkgOp; this is callback run from
     * that task from the download task.
     */
}

- (BOOL) isLoading: (MFANMediaItem *) mfanItem
{
    MFANDownloadReq *req;

    [_commLock lock];
    if (_loadingReq && _loadingReq.mediaItem == mfanItem) {
	[_commLock unlock];
	return YES;
    }
    for(req in _prioItems) {
	if (req.mediaItem == mfanItem) {
	    [_commLock unlock];
	    return YES;
	}
    }
    [_commLock unlock];
    return NO;
}

- (int) percentForItem: (MFANMediaItem *) mfanItem
{
    if (_loadingReq && mfanItem == _loadingReq.mediaItem)
	return _loadingPercent;
    else
	return -1;
}

- (int) downloadCount
{
    return _downloadCount;
}

-(void)URLSession:(NSURLSession *)session
     downloadTask:(NSURLSessionDownloadTask *)task
     didWriteData:(int64_t)bytesThisTime
totalBytesWritten:(int64_t)totalBytes
totalBytesExpectedToWrite:(int64_t)totalBytesExpected
{
    _loadingPercent = (int) ((100*totalBytes) / totalBytesExpected);
    return;
}
@end
