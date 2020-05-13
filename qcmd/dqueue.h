#ifndef _DQ_H_ENV__
#define  _DQ_H_ENV__ 1
template <class TT> class dqueue {
    TT *_headp;
    TT *_tailp;
 public:
    unsigned long _queueCount;      /* Stats: queue's current length */    
    unsigned long _queueMaxCount;   /* Stats: queue's max seen length */    
    
    dqueue() : _headp(NULL), _tailp(NULL), _queueCount(0), _queueMaxCount(0) {}

    void init() {
	_headp = NULL;
	_tailp = NULL;
        _queueCount = _queueMaxCount = 0;
    }

    int empty() {
	return _headp == NULL;
    }

    void append( TT *ep) {
	ep->_dqNextp = NULL;
	ep->_dqPrevp = _tailp;
	if (_tailp) {
	    _tailp->_dqNextp = ep;
	}
	else {
	    _headp = ep;
	}
	_tailp = ep;
        _queueCount++;
        if (_queueCount > _queueMaxCount) {
            _queueMaxCount = _queueCount;
        }
    }

    void prepend( TT *ep) {
	ep->_dqNextp = _headp;
	ep->_dqPrevp = NULL;
	if (_headp == NULL) {
	    _tailp = ep;
	}
	else {
	    _headp->_dqPrevp = ep;
	}
	_headp = ep;
        _queueCount++;
        if (_queueCount > _queueMaxCount) {
            _queueMaxCount = _queueCount;
        }
    }

    void remove( TT *ep) {
	TT *nextp = (TT *) ep->_dqNextp;
	TT *prevp = (TT *) ep->_dqPrevp;

	if ( nextp) {
	    nextp->_dqPrevp = prevp;
	}
	else {
	    /* we're last */
	    _tailp = prevp;
	}

	if ( prevp) {
	    prevp->_dqNextp = nextp;
	}
	else {
	    /* we're first */
	    _headp = nextp;
	}
        _queueCount--;
    }

    unsigned long count() {
        return _queueCount;
    }

    TT *head() { return _headp; }

    TT *tail() { return _tailp; }

    TT *pop() {
	TT *ep;
	ep = _headp;
	if (!ep) return ep;

	_headp = (TT *) ep->_dqNextp;
	if (_headp == NULL) {
	    _tailp = NULL;
	}
	else {
	    _headp->_dqPrevp = NULL;
	}
        _queueCount--;
	return ep;
    }
};
#endif
