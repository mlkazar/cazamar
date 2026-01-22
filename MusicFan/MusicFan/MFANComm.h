#import <UIKit/UIKit.h>
#import <Foundation/Foundation.h>

/* callable without osp.h */
#ifdef __cplusplus
extern "C" {
#endif

uint64_t osp_get_ms(void);

uint64_t osp_get_sec(void);

#ifdef __cplusplus
}
#endif

@interface MFANCommEntry : NSObject

@property NSString *who;
@property NSString *song;
@property NSString *artist;
@property uint32_t songType;
@property uint32_t acl;

- (MFANCommEntry *) initWithWho: (NSString *) who
			   song: (NSString *) song
			 artist: (NSString *) artist
		       songType: (uint32_t) songType
			    acl: (uint32_t) acl;

@end

@interface MFANComm: NSObject

- (MFANComm *) init;

+ (uint32_t) aclAll;

- (void) announceWho: (NSString *)who
		song: (NSString *)song
	      artist: (NSString *)artist
	    songType: (uint32_t) songType
		 acl: (uint32_t) acl;

- (int32_t) getAllPlaying: (NSMutableArray *) playing;

@end
