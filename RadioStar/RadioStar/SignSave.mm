#import "MFANCGUtil.h"
#import "MFANFileWriter.h"
#import "SignStation.h"
#import "SignSave.h"

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#include "json.h"

@implementation SignSave

+ (int32_t) saveStationsToFile: (NSMutableOrderedSet *) allStations {
    Json jsys;
    Json::Node *rootNodep;
    std::string jsonData;

    rootNodep = new Json::Node();
    rootNodep->initArray();

    SignStation *station;
    for(station in allStations) {
	Json::Node *stationDefNodep;
	Json::Node *valNodep;

	stationDefNodep = new Json::Node();
	stationDefNodep->initStruct();

	valNodep = new Json::Node();
	valNodep->initString([station.stationName
				 cStringUsingEncoding: NSUTF8StringEncoding],
			     /* quote */ true);
	Json::Node *namedNodep;
	namedNodep = new Json::Node();
	namedNodep->initNamed("stationName", valNodep);
	stationDefNodep->appendChild(namedNodep);

	valNodep = new Json::Node();
	valNodep->initString([station.shortDescr
				 cStringUsingEncoding: NSUTF8StringEncoding],
			     /* quote */ true);
	namedNodep = new Json::Node();
	namedNodep->initNamed("shortDescr", valNodep);
	stationDefNodep->appendChild(namedNodep);

	valNodep = new Json::Node();
	valNodep->initString([station.streamUrl
				 cStringUsingEncoding: NSUTF8StringEncoding],
			     /* quote */ true);
	namedNodep = new Json::Node();
	namedNodep->initNamed("streamUrl", valNodep);
	stationDefNodep->appendChild(namedNodep);

	valNodep = new Json::Node();
	valNodep->initString([station.iconUrl
				 cStringUsingEncoding: NSUTF8StringEncoding],
			     /* quote */ true);
	namedNodep = new Json::Node();
	namedNodep->initNamed("iconUrl", valNodep);
	stationDefNodep->appendChild(namedNodep);

	rootNodep->appendChild(stationDefNodep);
    }

    rootNodep->unparse(&jsonData);

    NSLog(@"about to print station data");
    rootNodep->print();

    MFANFileWriter *writer;
    int32_t code;

    writer = [[MFANFileWriter alloc] initWithFile: fileNameForFile(@"stations.json")];
    code = (int32_t) fwrite(jsonData.c_str(), jsonData.length(), 1, [writer fileOf]);
    if (code != 1) {
	code = -1;
	[writer cleanup];
    } else {
	code = [writer flush];
    }

    return code;
}

+ (int32_t) restoreStationsFromFile: (NSMutableOrderedSet *) allStations {
    Json json;
    Json::Node *rootNodep = nullptr;
    int32_t code;
    const char *fileNamep;
    FILE *filep;

    fileNamep = [fileNameForFile(@"stations.json") cStringUsingEncoding: NSUTF8StringEncoding];

    filep = fopen(fileNamep, "r");
    if (!filep) {
	return -1;
    }
    code = json.parseJsonFile(filep, &rootNodep);
    fclose(filep);

    if (code != 0) {
	return code;
    }

    // walk through the results creating the stations again, and then
    Json::Node *stationNodep;
    for( stationNodep = rootNodep->_children.head();
	 stationNodep != nullptr;
	 stationNodep = stationNodep->_dqNextp) {
	Json::Node *tnodep;
	SignStation *station;

	station = [[SignStation alloc] init];

	tnodep = stationNodep->searchForChild("stationName", false);
	station.stationName =
	    [NSString stringWithUTF8String: tnodep->_children.head()->_name.c_str()];

	tnodep = stationNodep->searchForChild("shortDescr", false);
	station.shortDescr =
	    [NSString stringWithUTF8String: tnodep->_children.head()->_name.c_str()];

	tnodep = stationNodep->searchForChild("streamUrl", false);
	station.streamUrl =
	    [NSString stringWithUTF8String: tnodep->_children.head()->_name.c_str()];

	tnodep = stationNodep->searchForChild("iconUrl", false);
	station.iconUrl =
	    [NSString stringWithUTF8String: tnodep->_children.head()->_name.c_str()];

	[station setIconImageFromUrl];

	[allStations addObject: station];
    }

    return 0;
}

@end
