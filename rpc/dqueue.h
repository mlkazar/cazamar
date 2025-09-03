#ifndef _DQ_H_ENV__
#define  _DQ_H_ENV__ 1

/*

Copyright 2016-2025 Michael Kazar

Permission is hereby granted, free of charge, to any person obtaining
a copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice shall be
included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

 */

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

    void insertAfter( TT *prevEp, TT *ep) {
        TT *nextp;

        if (!prevEp) {
            /* this branch handles the null head case of course */
            prepend(ep);
            return;
        }

        nextp = prevEp->_dqNextp;
        ep->_dqNextp = nextp;
        ep->_dqPrevp = prevEp;
        prevEp->_dqNextp = ep;
        if (nextp) {
            /* there's an element after us */
            nextp->_dqPrevp = ep;
        }
        else {
            /* we're the new tail */
            _tailp = ep;
        }

        _queueCount++;
        if (_queueCount > _queueMaxCount)
            _queueMaxCount = _queueCount;
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

    void concat(dqueue<TT> *srcp) {
        /* empty source, we're done */
        if (srcp->_queueCount == 0)
            return;

        if (!_tailp) {
            _headp = srcp->_headp;
            _tailp = srcp->_tailp;
            _queueCount = srcp->_queueCount;
        }
        else {
            _tailp->_dqNextp = srcp->_headp;
            srcp->_headp->_dqPrevp = _tailp;
            _queueCount += srcp->_queueCount;
        }

        srcp->_headp = NULL;
        srcp->_tailp = NULL;
        srcp->_queueCount = 0;
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
