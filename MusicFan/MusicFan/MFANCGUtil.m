//
//  MFANCGUtil.m
//  MusicFan
//
//  Created by Michael Kazar on 5/13/14.
//  Copyright (c) 2014 Mike Kazar. All rights reserved.
//

#import "MFANCGUtil.h"
#import "MFANTopSettings.h"
#include <CommonCrypto/CommonDigest.h>
#include <stdlib.h>

static char *months[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep",
			   "Oct", "Nov", "Dec"};
uint32_t
parseRssDate(char *datep)
{
    uint32_t total=0;
    int i;
    int tc;

    /* format is "Sun, 11 Aug 2015 07:00:00 +0000"; we return
     * 20150811 for the above.
     */
    datep = strchr(datep, ',');
    if (!datep)
	return 0;
    datep++;

    /* skip spaces */
    while((tc = *datep) != 0) {
	if (tc != ' ')
	    break;
	datep++;
    }
    if (tc == 0)	/* didn't find character */
	return 0;

    total = atoi(datep);

    /* skip parsed number */
    datep = strchr(datep, ' ');
    if (datep == NULL)
	return 0;

    /* skip spaces */
    while((tc = *datep) != 0) {
	if (tc != ' ')
	    break;
	datep++;
    }
    if (tc == 0)	/* didn't find character */
	return 0;

    for(i=0;i<12;i++) {
	if (strncasecmp(months[i], datep, 3) == 0)
	    break;
    }
    if (i == 12)	/* not found */
	return 0;

    total += (i+1)*100;

    datep += 4;

    total += 10000 * atoi(datep);
    return total;
}

/* try using CC_SHA1 */
uint64_t
signature(NSString *str)
{
    uint64_t sum = 1;
    int len;
    int i;
    char *strp = (char *) [str UTF8String];
    len = (int) strlen(strp);
    for(i=0; i<len; i++) {
	sum *= 101;
	sum += (strp[i] & 0xff);
    }
    return sum;
}

NSString *
fileNameForFile(NSString *fileName)
{
    NSArray *paths;
    NSString *libdir;
    NSString *value;

    paths = NSSearchPathForDirectoriesInDomains(NSLibraryDirectory, NSUserDomainMask, YES);
    libdir = [paths objectAtIndex:0];
    value = [NSString stringWithFormat: @"%@/%@", libdir, fileName];

    return value;
}

NSString *
fileNameForDoc(NSString *fileName)
{
    NSArray *paths;
    NSString *libdir;
    NSString *value;

    paths = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES);
    libdir = [paths objectAtIndex:0];
    value = [NSString stringWithFormat: @"%@/%@", libdir, fileName];

    return value;
}

/* return an NSData object consisting of a .WAV file with seconds seconds of mono silence */
NSData *
silentData(int seconds)
{
    NSData *datap;
    char *newDatap;
    static const int sampleRate = 8000;
    int nbytes;
    char *tp;
    long temp;
    int i;

    nbytes = 44 + sampleRate * seconds;
    tp = newDatap = malloc(nbytes);
    memset(tp, 0, nbytes);

    strncpy(tp, "RIFF", 4); tp += 4;

    temp = nbytes - 8;
    *tp++ = temp & 0xFF;
    *tp++ = (temp >> 8) & 0xff;
    *tp++ = (temp >> 16) & 0xff;
    *tp++ = 0;

    strncpy(tp, "WAVE", 4); tp += 4;

    strncpy(tp, "fmt ", 4); tp += 4;

    /* subchunk1size */
    *tp++ = 16;
    *tp++ = 0;
    *tp++ = 0;
    *tp++ = 0;

    /* audio format: uncompressed PCM */
    *tp++ = 1;
    *tp++ = 0;

    /* num channels */
    *tp++ = 1;	/* mono */
    *tp++ = 0;
    
    /* sample rate */
    *tp++ = sampleRate & 0xFF;
    *tp++ = (sampleRate >> 8) & 0xFF;
    *tp++ = 0;
    *tp++ = 0;

    /* byte rate: same as sample rate if bits/sample is 8 and we're in mono */
    *tp++ = sampleRate & 0xFF;
    *tp++ = (sampleRate >> 8) & 0xFF;
    *tp++ = 0;
    *tp++ = 0;

    /* block align = numchannels * bits/sample/8 */
    *tp++ = 1;
    *tp++ = 0;

    /* bits/sample */
    *tp++ = 8;
    *tp++ = 0;

    strncpy(tp, "data", 4); tp += 4;

    temp = sampleRate * seconds;
    *tp++ = temp & 0xFF;
    *tp++ = (temp >> 8) & 0xff;
    *tp++ = (temp >> 16) & 0xff;
    *tp++ = 0;

    /* rest of data is 0 from memset above */
    for(i=0;i<sampleRate*seconds;i++) {
	tp[i] = 0;
    }

    datap = [NSData dataWithBytesNoCopy: newDatap length: nbytes freeWhenDone: NO];

    return datap;
}

UIImage *
mediaImageWithSize(MPMediaItem *item, CGSize newSize, UIImage *defaultImage)
{
    MPMediaItemArtwork *art;
    UIImage *image;

    art = [item valueForProperty: MPMediaItemPropertyArtwork];
    if (art == nil || MFANTopSettings_forceNoArt) {
	return defaultImage;
    }
    else {
	image = [art imageWithSize: [art bounds].size];
	return resizeImage2(image, newSize);
    }
}

UIImage *
resizeImage(UIImage* image, CGFloat newSide) 
{
    CGSize newSize;
    newSize.width = newSize.height = newSide;

    CGRect newRect = CGRectIntegral(CGRectMake(0, 0, newSide, newSide));
    CGImageRef imageRef = image.CGImage;

    UIGraphicsBeginImageContextWithOptions(newSize, NO, 0);
    CGContextRef context = UIGraphicsGetCurrentContext();

    // Set the quality level to use when rescaling
    CGContextSetInterpolationQuality(context, kCGInterpolationHigh);
    CGAffineTransform flipVertical = CGAffineTransformMake(1, 0, 0, -1, 0, newSize.height);

    CGContextConcatCTM(context, flipVertical);  
    // Draw into the context; this scales the image
    CGContextDrawImage(context, newRect, imageRef);

    UIImage *newImage = UIGraphicsGetImageFromCurrentImageContext();

    UIGraphicsEndImageContext();    

    return newImage;
}

UIImage *
resizeImage2(UIImage* image, CGSize newSize) 
{
    CGRect newRect = CGRectIntegral(CGRectMake(0, 0, newSize.width, newSize.height));
    CGImageRef imageRef = image.CGImage;

    UIGraphicsBeginImageContextWithOptions(newSize, NO, 0);
    CGContextRef context = UIGraphicsGetCurrentContext();

    // Set the quality level to use when rescaling
    CGContextSetInterpolationQuality(context, kCGInterpolationHigh);
    CGAffineTransform flipVertical = CGAffineTransformMake(1, 0, 0, -1, 0, newSize.height);

    CGContextConcatCTM(context, flipVertical);  
    // Draw into the context; this scales the image
    CGContextDrawImage(context, newRect, imageRef);

    UIImage *newImage = UIGraphicsGetImageFromCurrentImageContext();

    UIGraphicsEndImageContext();    

    return newImage;
}

/* rect is the rectangle to draw, colors is an array of multiple R,
 * G, B, alpha values in the range of 0.0-1.0.  The colorSets variable
 * tells you how many of these 4 element sets there are, i.e. you
 * provide colorSets=2 if you provide two RGB<alpha> sets.
 */
void
drawGradRectangle(CGContextRef cxp, CGRect rect, CGFloat *colors, int colorSets)
{
    CGColorSpaceRef cspaceRef;
    CGGradientRef gradient;

    cspaceRef = CGColorSpaceCreateDeviceRGB();
    gradient = CGGradientCreateWithColorComponents( cspaceRef,
						    colors,
						    /* default gradLocations */ NULL,
						    colorSets);
    CGContextSaveGState(cxp);

    /* start to end from the mid top of the rectangle to the mid bottom; note that
     * coordinates are in CG space, where the origin is the bottom left.
     */
    CGFloat midX = rect.origin.x + rect.size.width/2;
    CGPoint startPoint = CGPointMake(midX, rect.origin.y);
    CGPoint endPoint = CGPointMake(midX, rect.origin.y+rect.size.height);

    CGContextAddRect(cxp, rect);
    CGContextClip(cxp);
    CGContextDrawLinearGradient(cxp, gradient, startPoint, endPoint, 0);
    CGContextRestoreGState(cxp);

    CGColorSpaceRelease(cspaceRef);
    CGGradientRelease(gradient);
}

void
drawGlossy(CGContextRef cxp, CGRect rect, CGFloat *colors, int colorSets)
{
    CGRect glossRect;
    CGFloat glossColors[] = {1.0, 1.0, 1.0, 0.7,
			     1.0, 1.0, 1.0, 0.1};

    drawGradRectangle(cxp, rect, colors, colorSets);

    glossRect = rect;
    glossRect.size.height = rect.size.height/2;
    drawGradRectangle(cxp, glossRect, glossColors, 2);
}

UIImage *
tintImage(UIImage *image, UIColor *color)
{
    UIImage *newImage;
    CGRect rect;

    rect.origin.x = 0;
    rect.origin.y = 0;
    rect.size = [image size];

    UIGraphicsBeginImageContext(rect.size);

    [color set];
    UIRectFill(rect);

    [image drawInRect: rect
	   blendMode: kCGBlendModeDestinationIn
	   alpha: 1.0];

    newImage = UIGraphicsGetImageFromCurrentImageContext();
    UIGraphicsEndImageContext();

    return newImage;
}

/* use patternImage for the texture of the button, and the shape
 * as the outline.
 */
UIImage *
traceImage(UIImage *patternImage, UIImage *shapeImage)
{
    UIImage *newImage;
    CGRect rect;

    rect.origin.x = 0;
    rect.origin.y = 0;
    rect.size = shapeImage.size;

    UIGraphicsBeginImageContext(rect.size);

    [patternImage drawInRect: rect];

    [shapeImage drawInRect: rect
		blendMode: kCGBlendModeDestinationIn
		alpha: 1.0];

    newImage = UIGraphicsGetImageFromCurrentImageContext();
    UIGraphicsEndImageContext();

    return newImage;
}

void
drawBackground(CGRect rect)
{
    static UIImage *_backImage;
    static CGRect newRect;
    CGContextRef cx;
    CGSize imageSize;
    CGFloat scalingFactor;

    if (!_backImage) {
	_backImage = [UIImage imageNamed: @"silver.jpg"];
	imageSize = [_backImage size];

	/* scale image horizontally to fit, while keeping
	 * proportions (that is, clip vertically).
	 *
	 * generate new rectange such to have the same proportions as
	 * imageSize, but a width of rect.size.width, i.e. scale it
	 * by a factor of rect.size.width / imageSize.width.
	 */
	scalingFactor = rect.size.height / imageSize.height;
	newRect.size.width = imageSize.width * scalingFactor;
	newRect.size.height = rect.size.height;
	newRect.origin.y = 0;
	newRect.origin.x = (rect.size.width - newRect.size.width)/2;
	_backImage = resizeImage2(_backImage, newRect.size);
    }

    cx = UIGraphicsGetCurrentContext();

    CGContextSetFillColorWithColor(cx, [UIColor blackColor].CGColor);
    CGContextFillRect(cx, rect);

    CGContextDrawImage(cx, newRect, _backImage.CGImage);
}

uint64_t
primeLE(uint64_t max)
{
    uint64_t i;
    uint64_t sq;
    int found;

    for(sq=1;sq<max;sq *= 2) {
	if (sq*sq >= max) break;
    }

    if ((max & 1) == 0)
	max--;

    while(1) {
	found = 0;
	for(i=2;i<sq;i++) {
	    if ((max % i) == 0) {
		found = 1;
		break;
	    }
	}
	if (!found) {
	    NSLog(@"%ld is prime", (long) max);
	    return max;
	}
	max -= 2;
    }
}
