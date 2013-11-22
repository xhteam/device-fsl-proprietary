/*
* Copyright (C) 2011 The Android Open Source Project
* Copyright (C) 2011-2013 Questers Technology, Inc.
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*      http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*/
#include <errno.h>
#include <pthread.h>
#include <termios.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <math.h>
#include <time.h>
#include <sys/time.h>
#define  LOG_TAG  "gps"
#include <cutils/log.h>
#include <cutils/sockets.h>
#include <cutils/array.h>
#include <cutils/properties.h>
#include <hardware/gps.h>

#define  GPS_DEBUG  0

#if GPS_DEBUG
#  define  D(...)   ALOGD(__VA_ARGS__)
#  define  V(...)   ALOGV(__VA_ARGS__)
#else
#  define  D(...)   
#  define  V(...)   
#endif

#define PROP_GPS_PORT "ro.gps.port"
#define PROP_GPS_RATE "ro.gps.rate"
#define PROP_GPS_POWER "ro.gps.power"
#define DEFAULT_GPS_PORT "/dev/ttyS1"
#define DEFAULT_GPS_RATE "9600"
#define DEFAULT_GPS_POWER  "/sys/class/gpio/gpio142/value"



/*****************************************************************/
/*****************************************************************/
/*****                                                       *****/
/*****       N M E A   T O K E N I Z E R                     *****/
/*****                                                       *****/
/*****************************************************************/
/*****************************************************************/

/* this is the state of our connection */

typedef struct {
    const char*  p;
    const char*  end;
} Token;

#define  MAX_NMEA_TOKENS  32

typedef struct {
    int     count;
    Token   tokens[ MAX_NMEA_TOKENS ];
} NmeaTokenizer;

static int
nmea_tokenizer_init( NmeaTokenizer*  t, const char*  p, const char*  end )
{
    int    count = 0;
    char*  q;

    // the initial '$' is optional
    if (p < end && p[0] == '$')
        p += 1;

    // remove trailing newline
    if (end > p && end[-1] == '\n') {
        end -= 1;
        if (end > p && end[-1] == '\r')
            end -= 1;
    }

    // get rid of checksum at the end of the sentecne
    if (end >= p+3 && end[-3] == '*') {
        end -= 3;
    }

    while (p < end) {
        const char*  q = p;

        q = memchr(p, ',', end-p);
        if (q == NULL)
            q = end;

         if (count < MAX_NMEA_TOKENS) {
             t->tokens[count].p   = p;
             t->tokens[count].end = q;
             count += 1;
         }
        if (q < end)
            q += 1;

        p = q;
    }

    t->count = count;
    return count;
}

static Token
nmea_tokenizer_get( NmeaTokenizer*  t, int  index )
{
    Token  tok;
    static const char*  dummy = "";

    if (index < 0 || index >= t->count) {
        tok.p = tok.end = dummy;
    } else
        tok = t->tokens[index];

    return tok;
}


static int
str2int( const char*  p, const char*  end )
{
    int   result = 0;
    int   len    = end - p;

    for ( ; len > 0; len--, p++ )
    {
        int  c;

        if (p >= end)
            goto Fail;

        c = *p - '0';
        if ((unsigned)c >= 10)
            goto Fail;

        result = result*10 + c;
    }
    return  result;

Fail:
    return -1;
}

static double
str2float( const char*  p, const char*  end )
{
    int   result = 0;
    int   len    = end - p;
    char  temp[16];

    if (len >= (int)sizeof(temp))
        return 0.;

    memcpy( temp, p, len );
    temp[len] = 0;
    return strtod( temp, NULL );
}

/*****************************************************************/
/*****************************************************************/
/*****                                                       *****/
/*****       N M E A   P A R S E R                           *****/
/*****                                                       *****/
/*****************************************************************/
/*****************************************************************/

#define  NMEA_MAX_SIZE  255

typedef struct {
    int     pos;
    int     overflow;
    int     utc_year;
    int     utc_mon;
    int     utc_day;
    int     utc_diff;
    GpsLocation fix;
    GpsSvStatus sv_status;
    int     sv_status_changed;
    char    in[ NMEA_MAX_SIZE+1 ];
} NmeaReader;

enum update_event_type {
	STATUS,
	SV_STATUS,
	LOCATION
};

typedef struct {
	int type;
	void* object;
} GpsUpdateEvent;

typedef struct {
	int                     init;
	int                     fd;
	GpsCallbacks            callbacks;
	int                     control[2];
	pthread_t               thread;
	pthread_mutex_t         update_mutex;
	Array*                  update_events;
} GpsState;
static GpsState  _gps_state[1];

/** @desc Convert struct tm to time_t (time zone neutral).
 *
 * The one missing function in libc: It works basically like mktime, with the main difference that
 * it does no time zone-related processing but interprets the members of the struct tm as UTC.
 * Unlike mktime, it will not modify any fields of the tm structure; if you need this behavior, call
 * mktime before this function.
 *
 * @param t Pointer to a struct tm containing date and time. Only the tm_year, tm_mon, tm_mday,
 * tm_hour, tm_min and tm_sec members will be evaluated, all others will be ignored.
 *
 * @return The epoch time (seconds since 1970-01-01 00:00:00 UTC) which corresponds to t.
 *
 * @author Originally written by Philippe De Muyter <phdm@macqel.be> for Lynx.
 * http://lynx.isc.org/current/lynx2-8-8/src/mktime.c
 */

static time_t
mkgmtime(struct tm *t)
{
    short month, year;
    time_t result;
    static int m_to_d[12] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};

    month = t->tm_mon;
    year = t->tm_year + month / 12 + 1900;
    month %= 12;
    if (month < 0) {
	    year -= 1;
	    month += 12;
    }
    result = (year - 1970) * 365 + m_to_d[month];
    if (month <= 1)
	    year -= 1;
    result += (year - 1968) / 4;
    result -= (year - 1900) / 100;
    result += (year - 1600) / 400;
    result += t->tm_mday;
    result -= 1;
    result *= 24;
    result += t->tm_hour;
    result *= 60;
    result += t->tm_min;
    result *= 60;
    result += t->tm_sec;
    return (result);
}

static void
nmea_reader_update_utc_diff( NmeaReader*  r )
{
    time_t         now = time(NULL);
    struct tm      tm_local;
    struct tm      tm_utc;
    long           time_local, time_utc;

    gmtime_r( &now, &tm_utc );
    localtime_r( &now, &tm_local );

    time_local = mktime(&tm_local);
    time_utc = mktime(&tm_utc);

    r->utc_diff = time_local - time_utc;
}


static void
nmea_reader_init( NmeaReader*  r )
{
    memset( r, 0, sizeof(*r) );

    r->pos      = 0;
    r->overflow = 0;
    r->utc_year = -1;
    r->utc_mon  = -1;
    r->utc_day  = -1;

    nmea_reader_update_utc_diff( r );
}


static int
nmea_reader_update_time( NmeaReader*  r, Token  tok )
{
    int        hour, minute, seconds, milliseconds;
    struct tm  tm;
    time_t     fix_time;

    if (tok.p + 6 > tok.end)
        return -1;

    if (r->utc_year < 0) {
        // no date, can't return valid timestamp (never ever make up a date, this could wreak havoc)
        return -1;
    }
    else
    {
        tm.tm_year = r->utc_year - 1900;
        tm.tm_mon  = r->utc_mon - 1;
        tm.tm_mday = r->utc_day;
    }

    hour    = str2int(tok.p,   tok.p+2);
    minute  = str2int(tok.p+2, tok.p+4);
    seconds = str2int(tok.p+4, tok.p+6);

    // parse also milliseconds (if present) for better precision
    milliseconds = 0;
    if (tok.end - (tok.p+7) == 2) {
        milliseconds = str2int(tok.p+7, tok.end) * 10;
    }
    else if (tok.end - (tok.p+7) == 1) {
        milliseconds = str2int(tok.p+7, tok.end) * 100;
    }
    else if (tok.end - (tok.p+7) >= 3) {
        milliseconds = str2int(tok.p+7, tok.p+10);
    }

    // the following is only guaranteed to work if we have previously set a correct date, so be sure
    // to always do that before

    tm.tm_hour = hour;
    tm.tm_min  = minute;
    tm.tm_sec  = seconds;

    fix_time = mkgmtime( &tm );

    r->fix.timestamp = (long long)fix_time * 1000 + milliseconds;
    return 0;

    return 0;
}

static int
nmea_reader_update_cdate( NmeaReader*  r, Token  tok_d, Token tok_m, Token tok_y )
{

    if ( (tok_d.p + 2 > tok_d.end) ||
         (tok_m.p + 2 > tok_m.end) ||
         (tok_y.p + 4 > tok_y.end) )
        return -1;

    r->utc_day = str2int(tok_d.p,   tok_d.p+2);
    r->utc_mon = str2int(tok_m.p, tok_m.p+2);
    r->utc_year = str2int(tok_y.p, tok_y.p+4);

    return 0;
}

static int
nmea_reader_update_date( NmeaReader*  r, Token  date, Token  time )
{
    Token  tok = date;
    int    day, mon, year;

    if (tok.p + 6 != tok.end) {
        D("date not properly formatted: '%.*s'", tok.end-tok.p, tok.p);
        return -1;
    }
    day  = str2int(tok.p, tok.p+2);
    mon  = str2int(tok.p+2, tok.p+4);
    year = str2int(tok.p+4, tok.p+6) + 2000;

    if ((day|mon|year) < 0) {
        D("date not properly formatted: '%.*s'", tok.end-tok.p, tok.p);
        return -1;
    }

    r->utc_year  = year;
    r->utc_mon   = mon;
    r->utc_day   = day;

    return nmea_reader_update_time( r, time );
}


static double
convert_from_hhmm( Token  tok )
{
    double  val     = str2float(tok.p, tok.end);
    int     degrees = (int)(floor(val) / 100);
    double  minutes = val - degrees*100.;
    double  dcoord  = degrees + minutes / 60.0;
    return dcoord;
}


static int
nmea_reader_update_latlong( NmeaReader*  r,
                            Token        latitude,
                            char         latitudeHemi,
                            Token        longitude,
                            char         longitudeHemi )
{
    double   lat, lon;
    Token    tok;

    tok = latitude;
    if (tok.p + 6 > tok.end) {
        D("latitude is too short: '%.*s'", tok.end-tok.p, tok.p);
        return -1;
    }
    lat = convert_from_hhmm(tok);
    if (latitudeHemi == 'S')
        lat = -lat;

    tok = longitude;
    if (tok.p + 6 > tok.end) {
        D("longitude is too short: '%.*s'", tok.end-tok.p, tok.p);
        return -1;
    }
    lon = convert_from_hhmm(tok);
    if (longitudeHemi == 'W')
        lon = -lon;

    r->fix.flags    |= GPS_LOCATION_HAS_LAT_LONG;
    r->fix.latitude  = lat;
    r->fix.longitude = lon;
    return 0;
}


static int
nmea_reader_update_altitude( NmeaReader*  r,
                             Token        altitude,
                             Token        units )
{
    double  alt;
    Token   tok = altitude;

    if (tok.p >= tok.end)
        return -1;

    r->fix.flags   |= GPS_LOCATION_HAS_ALTITUDE;
    r->fix.altitude = str2float(tok.p, tok.end);
    return 0;
}

static int
nmea_reader_update_accuracy( NmeaReader*  r,
                             Token        accuracy )
{
    double  acc;
    Token   tok = accuracy;

    if (tok.p >= tok.end)
        return -1;

    r->fix.accuracy = str2float(tok.p, tok.end);

    if (r->fix.accuracy == 99.99){
      return 0;
    }

    r->fix.flags   |= GPS_LOCATION_HAS_ACCURACY;
    return 0;
}

static int
nmea_reader_update_bearing( NmeaReader*  r,
                            Token        bearing )
{
    double  alt;
    Token   tok = bearing;

    if (tok.p >= tok.end)
        return -1;

    r->fix.flags   |= GPS_LOCATION_HAS_BEARING;
    r->fix.bearing  = str2float(tok.p, tok.end);
    return 0;
}


static int
nmea_reader_update_speed( NmeaReader*  r,
                          Token        speed )
{
    double  alt;
    Token   tok = speed;

    if (tok.p >= tok.end)
        return -1;

    r->fix.flags   |= GPS_LOCATION_HAS_SPEED;
    // convert knots into m/sec (1 knot equals 1.852 km/h, 1 km/h equals 3.6 m/s)
    // since 1.852 / 3.6 is an odd value (periodic), we're calculating the quotient on the fly
    // to obtain maximum precision (we don't want 1.9999 instead of 2)
    r->fix.speed    = str2float(tok.p, tok.end) * 1.852 / 3.6;
    return 0;
}


static void
nmea_reader_parse( NmeaReader*  r )
{
   /* we received a complete sentence, now parse it to generate
    * a new GPS fix...
    */
    NmeaTokenizer  tzer[1];
    Token          tok;
    int i;

    ALOGD("'%.*s'", r->pos, r->in);
    if (r->pos < 9) {
        D("Too short. discarded.");
        return;
    }
    {
		struct timeval tv;
		gettimeofday(&tv, NULL);
		_gps_state->callbacks.nmea_cb(tv.tv_sec*1000+tv.tv_usec/1000, r->in, r->pos);
    }

    nmea_tokenizer_init(tzer, r->in, r->in + r->pos);
#if GPS_DEBUG
    {
        int  n;
        D("Found %d tokens", tzer->count);
        for (n = 0; n < tzer->count; n++) {
            Token  tok = nmea_tokenizer_get(tzer,n);
            D("size of %2d: '%d', ptr=%x", n, tok.end-tok.p, (unsigned int)tok.p);
            D("%2d: '%.*s'", n, tok.end-tok.p, tok.p);
        }
    }
#endif

    tok = nmea_tokenizer_get(tzer, 0);
    if (tok.p + 5 > tok.end) {
        D("sentence id '%.*s' too short, ignored.", tok.end-tok.p, tok.p);
        return;
    }

    // ignore first two characters.
    tok.p += 2;
    if ( !memcmp(tok.p, "GGA", 3) ) {
        // GPS fix
        Token  tok_fixstaus      = nmea_tokenizer_get(tzer,6);

        if ((tok_fixstaus.p[0] > '0') && (r->utc_year >= 0)) {
          // ignore this until we have a valid timestamp

          Token  tok_time          = nmea_tokenizer_get(tzer,1);
          Token  tok_latitude      = nmea_tokenizer_get(tzer,2);
          Token  tok_latitudeHemi  = nmea_tokenizer_get(tzer,3);
          Token  tok_longitude     = nmea_tokenizer_get(tzer,4);
          Token  tok_longitudeHemi = nmea_tokenizer_get(tzer,5);
          Token  tok_altitude      = nmea_tokenizer_get(tzer,9);
          Token  tok_altitudeUnits = nmea_tokenizer_get(tzer,10);

          // don't use this as we have no fractional seconds and no date; there are better ways to
          // get a good timestamp from GPS
          //nmea_reader_update_time(r, tok_time);
          nmea_reader_update_latlong(r, tok_latitude,
                                        tok_latitudeHemi.p[0],
                                        tok_longitude,
                                        tok_longitudeHemi.p[0]);
          nmea_reader_update_altitude(r, tok_altitude, tok_altitudeUnits);
        }

    } else if ( !memcmp(tok.p, "GLL", 3) ) {

        Token  tok_fixstaus      = nmea_tokenizer_get(tzer,6);

        if ((tok_fixstaus.p[0] == 'A') && (r->utc_year >= 0)) {
          // ignore this until we have a valid timestamp

          Token  tok_latitude      = nmea_tokenizer_get(tzer,1);
          Token  tok_latitudeHemi  = nmea_tokenizer_get(tzer,2);
          Token  tok_longitude     = nmea_tokenizer_get(tzer,3);
          Token  tok_longitudeHemi = nmea_tokenizer_get(tzer,4);
          Token  tok_time          = nmea_tokenizer_get(tzer,5);

          // don't use this as we have no fractional seconds and no date; there are better ways to
          // get a good timestamp from GPS
          //nmea_reader_update_time(r, tok_time);
          nmea_reader_update_latlong(r, tok_latitude,
                                        tok_latitudeHemi.p[0],
                                        tok_longitude,
                                        tok_longitudeHemi.p[0]);
        }
 
    }else if ( !memcmp(tok.p, "GSA", 3) ) {
        // do something ?
        Token  tok_fixStatus   = nmea_tokenizer_get(tzer, 2);
        int i;
 
        if (tok_fixStatus.p[0] != '\0' && tok_fixStatus.p[0] != '1') {
 
          Token  tok_accuracy      = nmea_tokenizer_get(tzer, 15);
 
          nmea_reader_update_accuracy(r, tok_accuracy);
 
          r->sv_status.used_in_fix_mask = 0ul;
 
          for (i = 3; i <= 14; ++i){
 
            Token  tok_prn  = nmea_tokenizer_get(tzer, i);
            int prn = str2int(tok_prn.p, tok_prn.end);
 
            if (prn > 0){
              r->sv_status.used_in_fix_mask |= (1ul << (32 - prn));
              r->sv_status_changed = 1;
            //  D("%s: fix mask is %d", __FUNCTION__, r->sv_status.used_in_fix_mask);
            }
 
          }
 
        }
    } else if ( !memcmp(tok.p, "GSV", 3) ) {
 
        Token  tok_noSatellites  = nmea_tokenizer_get(tzer, 3);
        int    noSatellites = str2int(tok_noSatellites.p, tok_noSatellites.end);
       
        if (noSatellites > 0) {
 
          Token  tok_noSentences   = nmea_tokenizer_get(tzer, 1);
          Token  tok_sentence      = nmea_tokenizer_get(tzer, 2);
 
          int sentence = str2int(tok_sentence.p, tok_sentence.end);
          int totalSentences = str2int(tok_noSentences.p, tok_noSentences.end);
          int curr;
          int i;
          
          if (sentence == 1) {
              r->sv_status_changed = 0;
              r->sv_status.num_svs = 0;
          }
 
          curr = r->sv_status.num_svs;
 
          i = 0;
 
          while (i < 4 && r->sv_status.num_svs < noSatellites){
 
                 Token  tok_prn = nmea_tokenizer_get(tzer, i * 4 + 4);
                 Token  tok_elevation = nmea_tokenizer_get(tzer, i * 4 + 5);
                 Token  tok_azimuth = nmea_tokenizer_get(tzer, i * 4 + 6);
                 Token  tok_snr = nmea_tokenizer_get(tzer, i * 4 + 7);
 
                 r->sv_status.sv_list[curr].prn = str2int(tok_prn.p, tok_prn.end);
                 r->sv_status.sv_list[curr].elevation = str2float(tok_elevation.p, tok_elevation.end);
                 r->sv_status.sv_list[curr].azimuth = str2float(tok_azimuth.p, tok_azimuth.end);
                 r->sv_status.sv_list[curr].snr = str2float(tok_snr.p, tok_snr.end);
 
                 r->sv_status.num_svs += 1;
 
                 curr += 1;
 
                 i += 1;
          }
 
          if (sentence == totalSentences) {
              r->sv_status_changed = 1;
          }
 
 
        }
 
    } else if ( !memcmp(tok.p, "RMC", 3) ) {

        Token  tok_fixStatus     = nmea_tokenizer_get(tzer,2);

        if (tok_fixStatus.p[0] == 'A')
        {
          Token  tok_time          = nmea_tokenizer_get(tzer,1);
          Token  tok_latitude      = nmea_tokenizer_get(tzer,3);
          Token  tok_latitudeHemi  = nmea_tokenizer_get(tzer,4);
          Token  tok_longitude     = nmea_tokenizer_get(tzer,5);
          Token  tok_longitudeHemi = nmea_tokenizer_get(tzer,6);
          Token  tok_speed         = nmea_tokenizer_get(tzer,7);
          Token  tok_bearing       = nmea_tokenizer_get(tzer,8);
          Token  tok_date          = nmea_tokenizer_get(tzer,9);

            nmea_reader_update_date( r, tok_date, tok_time );


            nmea_reader_update_latlong( r, tok_latitude,
                                           tok_latitudeHemi.p[0],
                                           tok_longitude,
                                           tok_longitudeHemi.p[0] );


            nmea_reader_update_bearing( r, tok_bearing );
            nmea_reader_update_speed  ( r, tok_speed );
        }
    } else if ( !memcmp(tok.p, "VTG", 3) ) {
 
        Token  tok_fixStatus     = nmea_tokenizer_get(tzer,9);
 
        if (tok_fixStatus.p[0] != '\0' && tok_fixStatus.p[0] != 'N')
        {
            Token  tok_bearing       = nmea_tokenizer_get(tzer,1);
            Token  tok_speed         = nmea_tokenizer_get(tzer,5);
 
            nmea_reader_update_bearing( r, tok_bearing );
            nmea_reader_update_speed  ( r, tok_speed );
        }
 
    } else if ( !memcmp(tok.p, "ZDA", 3) ) {
 
        Token  tok_time;
        Token  tok_year  = nmea_tokenizer_get(tzer,4);
        tok_time  = nmea_tokenizer_get(tzer,1);

        if ((tok_year.p[0] != '\0') && (tok_time.p[0] != '\0')) {

          // make sure to always set date and time together, lest bad things happen
          Token  tok_day   = nmea_tokenizer_get(tzer,2);
          Token  tok_mon   = nmea_tokenizer_get(tzer,3);

          nmea_reader_update_cdate( r, tok_day, tok_mon, tok_year );
          nmea_reader_update_time(r, tok_time);
 
        }
 
 
    }else {
        tok.p -= 2;
        D("unknown sentence '%.*s", tok.end-tok.p, tok.p);
    }
    if (r->fix.flags != 0) {
#if GPS_DEBUG
        char   temp[1024];
        char*  p   = temp;
        char*  end = p + sizeof(temp);
        struct tm   utc;


        p += snprintf( p, end-p, "sending fix" );
        if (r->fix.flags & GPS_LOCATION_HAS_LAT_LONG) {
            p += snprintf(p, end-p, " lat=%g lon=%g", r->fix.latitude, r->fix.longitude);
        }
        if (r->fix.flags & GPS_LOCATION_HAS_ALTITUDE) {
            p += snprintf(p, end-p, " altitude=%g", r->fix.altitude);
        }
        if (r->fix.flags & GPS_LOCATION_HAS_SPEED) {
            p += snprintf(p, end-p, " speed=%g", r->fix.speed);
        }
        if (r->fix.flags & GPS_LOCATION_HAS_BEARING) {
            p += snprintf(p, end-p, " bearing=%g", r->fix.bearing);
        }
        if (r->fix.flags & GPS_LOCATION_HAS_ACCURACY) {
            p += snprintf(p,end-p, " accuracy=%g", r->fix.accuracy);
        }
        gmtime_r( (time_t*) &r->fix.timestamp, &utc );
        p += snprintf(p, end-p, " time=%s", asctime( &utc ) );
        D("%s",temp);
#endif
        if (_gps_state->callbacks.location_cb) 
        {
            _gps_state->callbacks.location_cb( &r->fix );
            r->fix.flags = 0;
        }
    }
    if (r->sv_status_changed != 0) 
    {
        if (_gps_state->callbacks.sv_status_cb) 
        {
            _gps_state->callbacks.sv_status_cb( &r->sv_status );
             r->sv_status_changed = 0;
        }
        else {
                D("no callback, keeping data until needed !");
        }
    }
    


}


static void
nmea_reader_addc( NmeaReader*  r, int  c )
{
    if (r->overflow) {
        r->overflow = (c != '\n');
        return;
    }

    if (r->pos >= (int) sizeof(r->in)-1 ) {
        r->overflow = 1;
        r->pos      = 0;
        return;
    }

    r->in[r->pos] = (char)c;
    r->pos       += 1;

    if (c == '\n') {
        nmea_reader_parse( r );
        r->pos = 0;
    }
}


/*****************************************************************/
/*****************************************************************/
/*****                                                       *****/
/*****       C O N N E C T I O N   S T A T E                 *****/
/*****                                                       *****/
/*****************************************************************/
/*****************************************************************/

/* commands sent to the gps thread */
enum state_thread_cmd {
	CMD_QUIT  = 0,
	CMD_START = 1,
	CMD_STOP  = 2,
	CMD_SEND  = 3
};

static void gps_state_thread_ctl(GpsState* state, enum state_thread_cmd val) {
	char cmd = (char)val;
	int ret;

	V("%s: command %d", __func__, val);

	do { ret=write( state->control[0], &cmd, 1 ); }
	while (ret < 0 && errno == EINTR);

	if (ret != 1)
		ALOGE("%s: could not send command %d: ret=%d: %s", __func__, val, ret,
			strerror(errno));
}

static int epoll_register( int  epoll_fd, int  fd ) {
	struct epoll_event  ev;
	int                 ret, flags;

	/* important: make the fd non-blocking */
	flags = fcntl(fd, F_GETFL);
	fcntl(fd, F_SETFL, flags | O_NONBLOCK);

	ev.events  = EPOLLIN;
	ev.data.fd = fd;
	do {
		ret = epoll_ctl( epoll_fd, EPOLL_CTL_ADD, fd, &ev );
	} while (ret < 0 && errno == EINTR);
	return ret;
}

static int epoll_deregister( int  epoll_fd, int  fd ) {
	int  ret;
	do {
		ret = epoll_ctl( epoll_fd, EPOLL_CTL_DEL, fd, NULL );
	} while (ret < 0 && errno == EINTR);
	return ret;
}

void enqueue_update_event(void* object, enum update_event_type type) {
	GpsState* state = _gps_state;

	if (!state->init) {
		/* There's some sparse events coming in from the rpc system after
		 * the state thread has been stopped. We just ignore these.
		 */
		V("%s: gps state not initialized. Won't enqueue event type %d!",
			__func__, type);
		free(object);
		return;
	}

	GpsUpdateEvent* event = malloc(sizeof(GpsUpdateEvent));
	event->type = type;
	event->object = object;

	pthread_mutex_lock(&state->update_mutex);
	arrayAdd(state->update_events, event);
	pthread_mutex_unlock(&state->update_mutex);

	gps_state_thread_ctl(state, CMD_SEND);
}

void update_gps_status(GpsStatusValue val) {
	V("%s", __func__);

	GpsStatus* status = malloc(sizeof(GpsStatus));
	status->size = sizeof(GpsStatus);
	status->status = val;

	enqueue_update_event(status, STATUS);
}

void update_gps_svstatus(GpsSvStatus *val) {
	V("%s", __func__);

	GpsSvStatus* sv_status = malloc(sizeof(GpsSvStatus));
	memcpy(sv_status, val, sizeof(GpsSvStatus));
	sv_status->size = sizeof(GpsSvStatus);

	enqueue_update_event(sv_status, SV_STATUS);
}

void update_gps_location(GpsLocation *fix) {
	V("%s", __func__);

	GpsLocation* location = malloc(sizeof(GpsLocation));
	memcpy(location, fix, sizeof(GpsLocation));
	location->size = sizeof(GpsLocation);

	enqueue_update_event(location, LOCATION);
}

static void send_update_events(GpsState* state) {
	int event_size;

	pthread_mutex_lock(&state->update_mutex);
	event_size = arraySize(state->update_events);

	if (!event_size) {
		pthread_mutex_unlock(&state->update_mutex);
		return;
	}

	do {
		GpsUpdateEvent* event = arrayRemove(state->update_events, 0);
		switch (event->type) {
			case STATUS: {
				if (state->callbacks.status_cb) {
					D("%s: sending STATUS event (status=%d)", __func__,
						((GpsStatus*)event->object)->status);
					state->callbacks.status_cb(event->object);
				}
				break;
			}
			case SV_STATUS: {
				if (state->callbacks.sv_status_cb) {
					D("%s: sending SV_STATUS event (num_svs=%d)", __func__,
						((GpsSvStatus*)event->object)->num_svs);
					state->callbacks.sv_status_cb(event->object);
				}
				break;
			}
			case LOCATION: {
				if (state->callbacks.location_cb) {
					D("%s: sending LOCATION event (flags=0x%x)", __func__,
						((GpsLocation*)event->object)->flags);
					state->callbacks.location_cb(event->object);
				}
				break;
			}
			default: {
				D("%s: unknown event type %d", __func__, event->type);
			}
		}
		free(event->object);
		free(event);
		--event_size;
	} while (event_size > 0);

	pthread_mutex_unlock(&state->update_mutex);
}

static int write_attr_file(const char* path,int val)
{
	int fd,nwr;
	char value[20]; 			
	fd = open(path, O_RDWR);
	if(fd<0)
	{
		ALOGE("failed to write attr file %s\n",path);
		return -1;
	}
	
	nwr = sprintf(value, "%d\n", val);
	write(fd, value, nwr);
	close(fd);		

	return 0;

}

static void gps_state_power(int on){	
	char value[PROPERTY_VALUE_MAX];
	property_get(PROP_GPS_POWER, value, DEFAULT_GPS_POWER);
    ALOGD("gps state -> power %s ",(on>0)?"on":"off");
    if(on){
        write_attr_file(value,1);
        //wait chip to finish reset
        usleep(500*1000);
    }else{
        write_attr_file(value,0);
    }
}
/* this is the main thread, it waits for commands from gps_state_thread_ctl() and,
 * when started, messages from the NMEA SMD. these are simple NMEA sentences
 * that must be parsed to be converted into GPS fixes sent to the framework
 */
uint32_t scan_interval = 0;
static void gps_state_thread( void*  arg ) {
	GpsState*   state = (GpsState*) arg;
	NmeaReader  reader[1];
	int         epoll_fd   = epoll_create(2);
	int         started    = 0;
	int         gps_fd     = state->fd;
	int         control_fd = state->control[1];

	// Engine is enabled when the thread is started.
	GpsStatus gps_status;
	memset(&gps_status, 0, sizeof(GpsStatus));
	gps_status.size = sizeof(GpsStatus);
	gps_status.status = GPS_STATUS_ENGINE_ON;
	if(state->callbacks.status_cb)
		state->callbacks.status_cb(&gps_status);

	nmea_reader_init( reader );

	// register control file descriptors for polling
	epoll_register( epoll_fd, control_fd );
	if (gps_fd > -1) {
		epoll_register( epoll_fd, gps_fd );
	}

	D("%s: start tid=%d", __func__, gettid());

	// now loop
	for (;;) {
		struct epoll_event   events[2];
		int                  ne, nevents;

		nevents = epoll_wait( epoll_fd, events, gps_fd>-1 ? 2 : 1,
			started ? (int)scan_interval : -1);
		if (nevents < 0) {
			if (errno != EINTR)
				ALOGE("epoll_wait() unexpected error: %s", strerror(errno));
			continue;
		}
		for (ne = 0; ne < nevents; ne++) {
			if ((events[ne].events & (EPOLLERR|EPOLLHUP)) != 0) {
				ALOGE("EPOLLERR or EPOLLHUP after epoll_wait() !?");
				goto Exit;
			}
			if ((events[ne].events & EPOLLIN) != 0) {
				int  fd = events[ne].data.fd;

				if (fd == control_fd) {
					int   ret;
					int   i;
					char  buf[255];
					int   update_handled = 0;
					memset(buf, 0xff, 255);
					V("%s: control fd event", __func__);
					do {
						ret = read(fd, buf, 255);
					} while (ret < 0 && errno == EINTR);

					for (i = 0; i < ret && buf[i] != 0xff; i++) {
						char cmd = buf[i];
						V("%s: handling cmd=%d\n", __func__, cmd);
						if (cmd == CMD_QUIT) {
							V("%s: quit", __func__);
							gps_status.status = GPS_STATUS_ENGINE_OFF;
							if(state->callbacks.status_cb)
								state->callbacks.status_cb(&gps_status);
							goto Exit;
						} else if (cmd == CMD_START) {
							if (!started) {
								V("%s: start", __func__);
								started = 1;
								gps_state_power(started);
								nmea_reader_init( reader );
								gps_status.status = GPS_STATUS_SESSION_BEGIN;
							    if(state->callbacks.status_cb)
								state->callbacks.status_cb(&gps_status);
							    }
						} else if (cmd == CMD_STOP) {
							if (started) {
								V("%s: stop", __func__);
								started = 0;
								gps_state_power(started);
								gps_status.status = GPS_STATUS_SESSION_END;
							    if(state->callbacks.status_cb)
								state->callbacks.status_cb(&gps_status);
							    
							}
						} else if (cmd == CMD_SEND) {
							/* Don't try sending update events when we already
							 * have in this run.
							 */
							if (!update_handled) {
								send_update_events(state);
								update_handled = 1;
							}
						}
					}
				} else if (fd == gps_fd) {
					char  buff[32];
					D("gps fd event");
					for (;;) {
						int  nn, ret;

						ret = read( fd, buff, sizeof(buff) );
						if (ret < 0) {
							if (errno == EINTR)
								continue;
							if (errno != EWOULDBLOCK)
							ALOGE("error while reading from gps daemon socket: %s:", strerror(errno));
							break;
						}
						D("received %d bytes: %.*s", ret, ret, buff);
						for (nn = 0; nn < ret; nn++)
							nmea_reader_addc( reader, buff[nn] );
					}
					D("gps fd event end");
				} else {
					ALOGE("epoll_wait() returned unkown fd %d ?", fd);
				}
			}
		}
	}
Exit:
	D("%s: exit tid=%d", __func__, gettid());
	return;
}

static void gps_state_deinit(GpsState*);

static void gps_state_init(GpsState* state) {
	char value[PROPERTY_VALUE_MAX];	
	char uart_dev_name[32];	
	int baudrate=9600;
	state->init       = 1;
	state->control[0] = -1;
	state->control[1] = -1;
	state->fd = -1; // open("/dev/smd27", O_RDONLY );
	state->update_events = arrayCreate();


	gps_state_power(0);
	
	pthread_mutex_init(&state->update_mutex, NULL);
	
	//get gps port
	property_get(PROP_GPS_PORT, value, DEFAULT_GPS_PORT);
	sprintf(uart_dev_name, "%s", value);
	state->fd = open(uart_dev_name, O_RDONLY);    
    if (state->fd < 0) {
        D("gps open port[%s] fail\n",uart_dev_name);
        state->init = 0;
        return;
    }
    
	property_get(PROP_GPS_RATE, value, DEFAULT_GPS_RATE);
	baudrate = atoi("value");
	
    
    // disable echo on serial lines
    if (isatty( state->fd ) ) {
        struct termios  ios;
        int BaudRate = B9600;
        if (baudrate == 38400)
          BaudRate = B38400;
        else if (baudrate == 57600)
          BaudRate = B57600;
        else if (baudrate == 115200)
          BaudRate = B115200;
        else if (baudrate == 230400)
          BaudRate = B230400;
        else if (baudrate == 460800)
          BaudRate = B460800;
        else if (baudrate == 921600)
          BaudRate = B921600;  
        
        tcgetattr( state->fd, &ios );
        ios.c_lflag = 0;  /* disable ECHO, ICANON, etc... */
        ios.c_oflag &= (~ONLCR); /* Stop \n -> \r\n translation on output */
        ios.c_iflag &= (~(ICRNL | INLCR)); /* Stop \r -> \n & \n -> \r translation on input */
        ios.c_iflag |= (IGNCR | IXOFF);  /* Ignore \r & XON/XOFF on input */
        cfsetispeed(&ios, BaudRate);
        cfsetospeed(&ios, BaudRate);
        tcsetattr( state->fd, TCSANOW, &ios );
    }

	if ( socketpair( AF_LOCAL, SOCK_STREAM, 0, state->control ) < 0 ) {
		ALOGE("%s: could not create thread control socket pair: %s", __func__,
			strerror(errno));
		goto Fail;
	}

	state->thread = state->callbacks.create_thread_cb("gps_state",
		gps_state_thread, state);
	if (!state->thread) {
		state->thread = 0;
		ALOGE("%s could not create gps state thread: %s", __func__,
			strerror(errno));
		goto Fail;
	}

	ALOGD("%s: done", __func__);
	return;

Fail:
	gps_state_deinit( state );
}

static void gps_state_deinit(GpsState*  state) {
	GpsUpdateEvent* event;

	ALOGD("%s", __func__);

	// tell the thread to quit, and wait for it
	gps_state_thread_ctl(state, CMD_QUIT);
	pthread_join(state->thread, NULL);

	// close the control socket pair
	close(state->control[0]);
	state->control[0] = -1;
	close(state->control[1]);
	state->control[1] = -1;

	// close connection to the QEMU GPS daemon
	close(state->fd);
	state->fd = -1;

	state->init = 0;

	// cleanup
	pthread_mutex_lock(&state->update_mutex);
	// free any pending events
	while (arraySize(state->update_events)) {
		event = arrayRemove(state->update_events, 0);
		free(event->object);
		free(event);
	}
	arrayFree(state->update_events);
	state->update_events = NULL;
	pthread_mutex_unlock(&state->update_mutex);
	pthread_mutex_destroy(&state->update_mutex);
}

/*****************************************************************/
/*****************************************************************/
/*****                                                       *****/
/*****       I N T E R F A C E                               *****/
/*****                                                       *****/
/*****************************************************************/
/*****************************************************************/

static int gps_init(GpsCallbacks* callbacks) {
    GpsState*  s = _gps_state;

	ALOGD("%s", __func__);

	s->callbacks = *callbacks;


    return 0;
}

static void gps_cleanup() {
    GpsState*  s = _gps_state;

	ALOGD("%s", __func__);

}

static int gps_start() {
    GpsState*  s = _gps_state;

	ALOGD("%s", __func__);

	if (!s->init)
		gps_state_init(s);

    if (s->fd < 0)
        return -1;


	gps_state_thread_ctl(s, CMD_START);
	return 0;
}

static int gps_stop() {
    GpsState*  s = _gps_state;

	if (!s->init) {
		D("%s: called with uninitialized state !!", __FUNCTION__);
		return -1;
	}
	
	ALOGD("%s", __func__);
	gps_state_thread_ctl(s, CMD_STOP);
	gps_state_deinit(s);
	return 0;
}

static int 
gps_inject_time(GpsUtcTime time, int64_t timeReference, int uncertainty) {
	return 0;
}

static int 
gps_inject_location(double latitude, double longitude, float accuracy) {
	return 0;
}

static void 
gps_delete_aiding_data(GpsAidingData flags) {
}

static int gps_set_position_mode(GpsPositionMode mode,
	GpsPositionRecurrence recurrence, uint32_t min_interval,
	uint32_t preferred_accuracy, uint32_t preferred_time) {

	scan_interval = min_interval;
	if (scan_interval == 0) {
		//We don't handle single shot requests atm...
		//So one every 4seconds will it be.
		scan_interval=4000;
	}
	if (scan_interval > 6000) {
		//Ok, A9 will timeout with so high value.
		//Set it to 6. This used to be 8 seconds, which didn't work out either.
		scan_interval=6000;
	}

	ALOGD("%s: scan_interval=%u (requested=%u)", __func__, scan_interval, min_interval);

	return 0;
}

static const void* 
gps_get_extension(const char* name) {
	return NULL;
}

static const GpsInterface  NmeaUartGpsInterface = {
    sizeof(GpsInterface),
    gps_init,
    gps_start,
    gps_stop,
    gps_cleanup,
    gps_inject_time,
    gps_inject_location,
    gps_delete_aiding_data,
    gps_set_position_mode,
    gps_get_extension,
};

static const GpsInterface* gps_get_hardware_interface(struct gps_device_t* dev) {
    return &NmeaUartGpsInterface;
}



static int open_gps(const struct hw_module_t* module, char const* name,
        struct hw_device_t** device)
{
    struct gps_device_t *dev = malloc(sizeof(struct gps_device_t));
    memset(dev, 0, sizeof(*dev));

    dev->common.tag = HARDWARE_DEVICE_TAG;
    dev->common.version = 0;
    dev->common.module = (struct hw_module_t*)module;
    dev->get_gps_interface = gps_get_hardware_interface;

    *device = (struct hw_device_t*)dev;
    return 0;
}

static struct hw_module_methods_t gps_module_methods = {
    .open = open_gps
};

struct hw_module_t HAL_MODULE_INFO_SYM = {
    .tag = HARDWARE_MODULE_TAG,
    .version_major = 1,
    .version_minor = 0,
    .id = GPS_HARDWARE_MODULE_ID,
    .name = "NMEA GPS Module",
    .author = "http://www.quester.com.cn",
    .methods = &gps_module_methods,
};

