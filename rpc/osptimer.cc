#include "osptimer.h"

/* declare statics */
pthread_once_t OspTimer::_once = PTHREAD_ONCE_INIT;
pthread_mutex_t OspTimer::_mutex;
dqueue<OspTimer> OspTimer::_allTimers;
CThreadHandle *OspTimer::_timerHandlep;
pthread_cond_t OspTimer::_cv;
int OspTimer::_sleeping = 0;
CThread OspTimer::_dummyCThread;

void
OspTimer::initSys()
{
    pthread_cond_init(&_cv, NULL);
    _timerHandlep = new CThreadHandle();
    _timerHandlep->init((CThread::StartMethod) &OspTimer::helper, &_dummyCThread, NULL);
}


void
OspTimer::helper(void *contextp)
{
    OspTimer *evp;
    struct timespec ts;
    struct timeval tv;
    
    pthread_mutex_lock(&_mutex);
    while(1) {
        if ((evp = _allTimers.head()) != NULL) {
            gettimeofday(&tv, NULL);

            ts.tv_sec = tv.tv_sec;
            ts.tv_nsec = tv.tv_usec * 1000;

            if (ts.tv_sec > evp->_expires.tv_sec ||
                (ts.tv_sec == evp->_expires.tv_sec &&
                 ts.tv_nsec >= evp->_expires.tv_nsec)) {
                /* we found an event we can run */
                evp = _allTimers.pop();
                osp_assert(evp->_inQueue);
                evp->_inQueue = 0;
                evp->holdNL();
                pthread_mutex_unlock(&_mutex);
                evp->_procp(evp, evp->_contextp);
                pthread_mutex_lock(&_mutex);
                if (!evp->_canceled) {
                    evp->_canceled = 0;
                    evp->releaseNL();
                }
                evp->releaseNL();
            }
            else {
                /* timer present, but hasn't expired yet */
                _sleeping = 1;
                pthread_cond_timedwait(&_cv, &_mutex, &evp->_expires);
            }
        }
        else {
            /* go to sleep; no timers are currently queued, so just sleep */
            _sleeping = 1;
            pthread_cond_wait(&_cv, &_mutex);
        }
    }
}

void
OspTimer::init(uint32_t timerMs, Callback *procp, void *contextp)
{
    OspTimer *prevp;
    OspTimer *tp;
    struct timeval tv;

    pthread_once(&OspTimer::_once, &OspTimer::initSys);

    /* queue our timer on the global list */
    _procp = procp;
    _contextp = contextp;
    _canceled = 0;

    _refCount = 1;

    /* compute ts */
    gettimeofday(&tv, NULL);
    _expires.tv_sec = tv.tv_sec;
    _expires.tv_nsec = tv.tv_usec * 1000;

    /* add in delay, but note that tv_nsec could be as much as 1.9B */
    _expires.tv_sec += timerMs / 1000;
    _expires.tv_nsec += (timerMs % 1000) * 1000000;
    
    /* handle overflow */
    if (_expires.tv_nsec >= 1000000000) {
        _expires.tv_sec++;
        _expires.tv_nsec -= 1000000000;
    }

    pthread_mutex_lock(&_mutex);
    /* sort into the list */
    for( prevp = NULL, tp = _allTimers.head(); 
         tp;
         prevp = tp, tp = tp->_dqNextp) {
        if ( tp->_expires.tv_sec > _expires.tv_sec ||
             (tp->_expires.tv_sec == _expires.tv_sec &&
              tp->_expires.tv_nsec > _expires.tv_nsec)) {
            break;
        }
    }

    /* at this point, tp points to the first guy expiring after us, and thus prevp is
     * the last guy expiring before or at the same time as us.
     */
    _allTimers.insertAfter(prevp, this);
    _inQueue = 1;

    if (_sleeping) {
        _sleeping = 0;
        pthread_cond_broadcast(&_cv);
    }

    pthread_mutex_unlock(&_mutex);
}

void 
OspTimer::releaseNL()
{
    osp_assert(_refCount > 0);
    if (--_refCount == 0) {
        if (_inQueue) {
            _inQueue = 0;
            _allTimers.remove(this);
        }
        delete this;
    }
}

void
OspTimer:: cancel()
{
    pthread_mutex_lock(&_mutex);
    osp_assert(!_canceled);
    _canceled = 1;
    if (_inQueue) {
        _inQueue = 0;
        _allTimers.remove(this);
    }
    releaseNL();
    pthread_mutex_unlock(&_mutex);
}

OspTimer::~OspTimer()
{
    return;
}
