#include "radixtree.h"

int32_t
RadixTree::apply(Callback *procp, void *callbackContextp, int onlyUnique)
{
    uint32_t i;
    uint32_t j;
    Ptr *ptr0p;
    Ptr *ptr1p;
    Node *nodep;
    dqueue<Item> *listp;
    Item *itemp;
    int32_t code;
    std::string *lastKeyp;

    /* no root object */
    if (_root._type == _typeNull)
        return 0;

    for(i=0;i<Node::_width;i++) {
        nodep = _root._u._nodep;
        ptr0p = &nodep->_ptr[i];
        if (ptr0p->_type == _typeNode) {
            nodep = ptr0p->_u._nodep;
            for(j=0;j<Node::_width;j++) {
                ptr1p = &nodep->_ptr[j];
                if (ptr1p->_type == _typeList) {
                    listp = ptr1p->_u._listp;
                    lastKeyp = NULL;
                    for(itemp = listp->head(); itemp; itemp=itemp->_dqNextp) {
                        if (!onlyUnique || (!lastKeyp || (*lastKeyp != *itemp->_keyp))) {
                            code = procp(callbackContextp, itemp->_backp);
                            if (code)
                                return code;
                        }
                        lastKeyp = itemp->_keyp;
                    }
                }
            }
        }
    }

    return 0;
}

/* find the list and item with a particular key */
int32_t
RadixTree::find(std::string *keyp, dqueue<Item> **listpp, Item **itempp)
{
    Ptr *ptrp;
    uint32_t i;
    uint32_t ix;
    dqueue<Item> *listp;
    Item *itemp;

    ptrp = &_root;
    for(i=0;i<_levelCount;i++) {
        if (ptrp->_type == _typeNull) {
            /* key doesn't exist; list doesn't even exist */
            return -1;
        }
        ix = Item::getStrByte(keyp, i);
        ptrp = ptrp->_u._nodep->_ptr+ix;
    }

    /* terminal list is null */
    if (ptrp->_type != _typeList)
        return -1;
    listp = ptrp->_u._listp;

    for( itemp = listp->head(); itemp; itemp=itemp->_dqNextp) {
        if (*itemp->_keyp == *keyp) {
            *itempp = itemp;
            *listpp = listp;
            return 0;
        }
    }

    /* name is not present in the list */
    return -1;
}

int32_t
RadixTree::lookup(std::string *keyp, void **objp)
{
    int32_t code;
    Item *itemp;
    dqueue<Item> *listp;

    code = find(keyp, &listp, &itemp);
    if (code != 0) {
        *objp = NULL;
        return code;
    }

    *objp = itemp;
    return 0;
}

int32_t
RadixTree::insert(std::string *keyp, Item *recordItemp, void *objp)
{
    uint32_t i;
    uint32_t ix;
    Item *itemp;
    Item *prevItemp;
    Ptr *ptrp;
    dqueue<Item> *listp;

    recordItemp->_keyp = keyp;
    recordItemp->_backp = objp;

    ptrp = &_root;
    for(i=0;i<_levelCount;i++) {
        if (ptrp->_type == _typeNull) {
            /* have to allocate this */
            ptrp->_type = _typeNode;
            ptrp->_u._nodep = new Node();
        }
        ix = recordItemp->getKeyByte(i);
        ptrp = ptrp->_u._nodep->_ptr+ix;
    }

    /* here, ptrp points to a pointer in the last digit Node that points to
     * a linked list of objects, or is null.
     */
    osp_assert(ptrp->_type != _typeNode);
    if (ptrp->_type == _typeNull) {
        ptrp->_type = _typeList;
        ptrp->_u._listp = new dqueue<Item>;
    }

    /* now insert the new item sorted into this list */
    listp = ptrp->_u._listp;
    for( prevItemp = NULL, itemp = listp->head();
         itemp;
         prevItemp = itemp, itemp = itemp->_dqNextp) {
        if (*itemp->_keyp > *recordItemp->_keyp) {
            break;
        }
    }

    /* insert before item that stopped our scan (i.e. after prevItemp) */
    listp->insertAfter(prevItemp, recordItemp);
    recordItemp->_inList = 1;

    return 0;
}

/* remove the entry with a key; if objp is specified, it must
 * match the key's value.
 */
int32_t
RadixTree::remove(std::string *keyp, void *objp)
{
    int32_t code;
    Item *itemp;
    Item *nitemp;
    dqueue<Item> *listp;

    code = find(keyp, &listp, &itemp);
    if (code != 0) {
        return code;
    }

    if (objp != NULL && objp == itemp) {
        /* we were asked to remove a single object, and find returned it, so no further
         * search is necessary.
         */
        listp->remove(itemp);
        return 0;
    }

    /* otherwise, we search the list for this object, or if objp is null, for all
     * objects, and remove them.
     */
    for( itemp = listp->head(); itemp; itemp=nitemp) {
        nitemp = itemp->_dqNextp;       /* before removing */
        if ( *itemp->_keyp == *keyp &&
             (objp == NULL || objp == itemp->_backp)) {
            listp->remove(itemp);
        }
    }

    return 0;
}
