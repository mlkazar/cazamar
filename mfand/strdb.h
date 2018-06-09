#ifndef __STRDB_H_ENV__
#define __STRDB_H_ENV__ 1

#include "jsdb.h"

/* User database consists of a user's screen name, an email address (used to login)
 * and a password, always stored encoded.
 */
class UserDb : public Jsdb {
    int64_t _nextUid;

 public:
    int32_t init();

    int32_t createUser(const char *userNamep, const char *emailp, const char *passwordp);

    int32_t lookupUser( const char *keyp,     /* user's email address */
                        std::string *emailStrp,
                        std::string *passwordStrp,
                        std::string *uidStrp);

    int32_t deleteUser( std::string *emailStrp);
};

/* The schedule database consists of a web address, which should be
 * the first address in the stations database for the radio station,
 * followed by a schedule item, consisting of a time range; a set of
 * days, each represented by an integer; and a period, one of daily,
 * weekly, or monthly.
 *
 * For example, a show that airs from 7 AM to 9 AM every Monday would
 * have a startTime=7:00, an endTime=09:00, an array of days = {1}
 * (Monday is day 1, with Sunday being day 7), and a period of weekly.
 *
 * Days in a monthly schedule can be specified as either specific
 * dates (the 3rd) or the 2nd Wednesday in the month (which would be
 * specified as 2.3, since all indices are one-based).
 *
 * The set of days is unused in daily show schedules.
 */
class ScheduleDb : public Jsdb {
    int32_t addScheduleEntry( const char *ownerIdp,
                              const char *startTimep,
                              const char *endTimep,
                              const char *daysp[],
                              const char *periodp);

    /* note that the days array is an array of pointers to strings, not
     * a pointer to an array of strings, and the strings, for this parameter,
     * are all allocated by the called function.
     */
    int32_t lookupScheduleEntry ( const char *ownerIdp,
                                  int32_t ix,
                                  std::string *startTimep,
                                  std::string *endTimep,
                                  int32_t *daysCountp,
                                  std::string *daysp[],
                                  std::string *periodp);

    int32_t deleteScheduleEntry( const char *ownerIdp, int32_t ix);
};

/* one of these keeping track of the media available for a user */
class MediaDb : public Jsdb {
    int32_t addMediaEntry( const char *ownerIdp,
                           uint32_t startUnixTime,
                           uint32_t endUnixTime,
                           const char *fileNamep);
};

/* one of these to keep track of available radio stations, whether they announce songs, and
 * whether they've been verified.
 */
class StationDb : public Jsdb {
    
};

#endif /* __STRDB_H_ENV__ */
