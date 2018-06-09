#ifndef __RADIXTREE_H_ENV__

#define __RADIXTREE_H_ENV__ 1

#include <string>
#include "osp.h"
#include "dqueue.h"

/* a RadixTree tree */
class RadixTree {
    /* define Ptr types */
    static const int32_t _typeNull = 0;
    static const int32_t _typeList = 1;
    static const int32_t _typeNode = 2;

    /* two levels of nodes 256 wide, followed by a list of what's left */
    static const uint32_t _levelCount = 2;

    /* one of these in each item */
    class Node;

public:
    class Item;

private:
    class Ptr {
    public:
        int32_t _type;
        union {
            Node *_nodep;
            dqueue<Item> *_listp;
        } _u;

        Ptr() {
            _type = _typeNull;
            _u._nodep = NULL;
        }
    };

    /* one of these for each level; we leave them as self-describing in case we want
     * to change the code to expand a level when its list gets too large.
     */
    class Node {
    public:
        static const uint32_t _logWidth = 8;
        static const uint32_t _width = (1<<_logWidth);

        uint32_t _itemCount;
        Ptr _ptr[_width];

        Node() {
            _itemCount = 0;
        }
    };

    /* you put one of these in your object for each key in a radix tree */
public:
    class Item {
    public:
        std::string *_keyp;
        void *_backp;

        Item *_dqNextp;
        Item *_dqPrevp;

        uint8_t _inList;

        Item() {
            _backp = NULL;
            _inList = 0;
            _keyp = NULL;
            _dqNextp = NULL;
            _dqPrevp = NULL;
        }

        static uint32_t getStrByte(std::string *keyp, uint32_t ix) {
            uint8_t *tp;
            uint32_t len;

            tp = (uint8_t *) keyp->c_str();
            len = (uint32_t) keyp->length();
            if (ix < len)
                return tp[ix];
            else
                return 0;
        }

        uint32_t getKeyByte(uint32_t ix) {
            return Item::getStrByte(_keyp, ix);
        }
    };

    /* the radix tree root; we have _levelCount levels of Node followed by
     * a list of sorted items.
     */
    Ptr _root;

public:
    RadixTree() {
        return;
    }

    int32_t find(std::string *keyp, dqueue<Item> **listpp, Item **itempp);

    int32_t insert(std::string *keyp, Item *recordItemp, void *objp);

    int32_t lookup(std::string *keyp, void **objp);

    /* if objp is non-null, we check that it matches, and fail if it doesn't */
    int32_t remove(std::string *keyp, void *objp = NULL);

    typedef int32_t (Callback)(void *callbackContextp, void *recordContextp);

    int32_t apply(Callback *callbackp, void *callbackContextp, int onlyUnique=0);
};

#endif /* __RADIXTREE_H_ENV__ */
