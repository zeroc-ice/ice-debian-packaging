// **********************************************************************
//
// Copyright (c) 2003-2017 ZeroC, Inc. All rights reserved.
//
// This copy of Ice is licensed to you under the terms described in the
// ICE_LICENSE file included in this distribution.
//
// **********************************************************************

#include <Proxy.h>
#include <Connection.h>
#include <Endpoint.h>
#include <Util.h>

using namespace std;
using namespace IcePHP;

ZEND_EXTERN_MODULE_GLOBALS(ice)

//
// Here's a brief description of how proxies are handled by this extension.
//
// A single PHP class, ObjectPrx, is registered. This is an "internal" class,
// i.e., implemented by this extension, and it is used to represent all proxies
// regardless of interface type.
//
// Like in C++, a proxy is only capable of invoking the Ice::ObjectPrx operations
// until it is narrowed with a checked or unchecked cast. Unlike C++, no PHP classes
// are created for proxies, because all marshaling activity is driven by the type
// definitions, not by statically-generated code.
//
// In order to perform a checked or unchecked cast, the generated code invokes
// ice_checkedCast or ice_uncheckedCast on the proxy to be narrowed, supplying a scoped
// name for the desired type. Internally, the proxy validates the scoped name and returns
// a new proxy containing the class or interface definition. This proxy is considered
// to be narrowed to that interface and therefore supports user-defined operations.
//
// Naturally, there are many predefined proxy methods (e.g., ice_getIdentity, etc.), but
// the proxy also needs to support user-defined operations (if it has type information).
// We use a Zend API hook that allows us to intercept the invocation of unknown methods
// on the proxy object.
//

//
// Class entries represent the PHP class implementations we have registered.
//
namespace IcePHP
{
zend_class_entry* proxyClassEntry = 0;
}

//
// Ice::ObjectPrx support.
//
static zend_object_handlers _handlers;

extern "C"
{
static zend_object* handleAlloc(zend_class_entry*);
static void handleFreeStorage(zend_object*);
static zend_object* handleClone(zval*);
static union _zend_function* handleGetMethod(zend_object**, zend_string*, const zval*);
static int handleCompare(zval*, zval*);
}

static ClassInfoPtr lookupClass(const string&);

namespace IcePHP
{

//
// Encapsulates proxy and type information.
//
class Proxy : public IceUtil::Shared
{
public:

    Proxy(const Ice::ObjectPrx&, const ClassInfoPtr&, const CommunicatorInfoPtr&);
    ~Proxy();

    bool clone(zval*, const Ice::ObjectPrx&);
    bool cloneUntyped(zval*, const Ice::ObjectPrx&);
    static bool create(zval*, const Ice::ObjectPrx&, const ClassInfoPtr&, const CommunicatorInfoPtr&);

    Ice::ObjectPrx proxy;
    ClassInfoPtr info;
    CommunicatorInfoPtr communicator;
    zval* connection;
    zval* cachedConnection;
};
typedef IceUtil::Handle<Proxy> ProxyPtr;

} // End of namespace IcePHP

ZEND_METHOD(Ice_ObjectPrx, __construct)
{
    runtimeError("proxies cannot be instantiated, use stringToProxy()");
}

ZEND_METHOD(Ice_ObjectPrx, __toString)
{
    if(ZEND_NUM_ARGS() > 0)
    {
        WRONG_PARAM_COUNT;
    }

    ProxyPtr _this = Wrapper<ProxyPtr>::value(getThis());
    assert(_this);

    try
    {
        string str = _this->proxy->ice_toString();
        RETURN_STRINGL(STRCAST(str.c_str()), static_cast<int>(str.length()));
    }
    catch(const IceUtil::Exception& ex)
    {
        throwException(ex);
        RETURN_NULL();
    }
}

ZEND_METHOD(Ice_ObjectPrx, ice_getCommunicator)
{
    if(ZEND_NUM_ARGS() > 0)
    {
        WRONG_PARAM_COUNT;
    }

    ProxyPtr _this = Wrapper<ProxyPtr>::value(getThis());
    assert(_this);

    _this->communicator->getZval(return_value);
}

ZEND_METHOD(Ice_ObjectPrx, ice_toString)
{
    ZEND_MN(Ice_ObjectPrx___toString)(INTERNAL_FUNCTION_PARAM_PASSTHRU);
}

ZEND_METHOD(Ice_ObjectPrx, ice_getIdentity)
{
    if(ZEND_NUM_ARGS() != 0)
    {
        WRONG_PARAM_COUNT;
    }

    ProxyPtr _this = Wrapper<ProxyPtr>::value(getThis());
    assert(_this);
    assert(_this->proxy);

    createIdentity(return_value, _this->proxy->ice_getIdentity());
}

ZEND_METHOD(Ice_ObjectPrx, ice_identity)
{
    ProxyPtr _this = Wrapper<ProxyPtr>::value(getThis());
    assert(_this);

    zend_class_entry* cls = idToClass("::Ice::Identity");
    assert(cls);

    zval *zid;

    if(zend_parse_parameters(ZEND_NUM_ARGS(), const_cast<char*>("O"), &zid, cls) == FAILURE)
    {
        RETURN_NULL();
    }

    Ice::Identity id;
    if(extractIdentity(zid, id))
    {
        try
        {
            if(!_this->cloneUntyped(return_value, _this->proxy->ice_identity(id)))
            {
                RETURN_NULL();
            }
        }
        catch(const IceUtil::Exception& ex)
        {
            throwException(ex);
            RETURN_NULL();
        }
    }
}

ZEND_METHOD(Ice_ObjectPrx, ice_getContext)
{
    if(ZEND_NUM_ARGS() != 0)
    {
        WRONG_PARAM_COUNT;
    }

    ProxyPtr _this = Wrapper<ProxyPtr>::value(getThis());
    assert(_this);

    if(!createStringMap(return_value, _this->proxy->ice_getContext()))
    {
        RETURN_NULL();
    }
}

ZEND_METHOD(Ice_ObjectPrx, ice_context)
{
    zval* arr = 0;

    if(zend_parse_parameters(ZEND_NUM_ARGS(), const_cast<char*>("a"), &arr) == FAILURE)
    {
        RETURN_NULL();
    }

    //
    // Populate the context.
    //
    Ice::Context ctx;
    if(arr && !extractStringMap(arr, ctx))
    {
        RETURN_NULL();
    }

    ProxyPtr _this = Wrapper<ProxyPtr>::value(getThis());
    assert(_this);

    try
    {
        if(!_this->clone(return_value, _this->proxy->ice_context(ctx)))
        {
            RETURN_NULL();
        }
    }
    catch(const IceUtil::Exception& ex)
    {
        throwException(ex);
        RETURN_NULL();
    }
}

ZEND_METHOD(Ice_ObjectPrx, ice_getFacet)
{
    if(ZEND_NUM_ARGS() != 0)
    {
        WRONG_PARAM_COUNT;
    }

    ProxyPtr _this = Wrapper<ProxyPtr>::value(getThis());
    assert(_this);

    try
    {
        string facet = _this->proxy->ice_getFacet();
        ZVAL_STRINGL(return_value, STRCAST(facet.c_str()), static_cast<int>(facet.length()));
    }
    catch(const IceUtil::Exception& ex)
    {
        throwException(ex);
        RETURN_NULL();
    }
}

ZEND_METHOD(Ice_ObjectPrx, ice_facet)
{
    char* name;
    size_t len;

    if(zend_parse_parameters(ZEND_NUM_ARGS(), const_cast<char*>("s"), &name, &len) == FAILURE)
    {
        RETURN_NULL();
    }

    ProxyPtr _this = Wrapper<ProxyPtr>::value(getThis());
    assert(_this);

    try
    {
        if(!_this->cloneUntyped(return_value, _this->proxy->ice_facet(name)))
        {
            RETURN_NULL();
        }
    }
    catch(const IceUtil::Exception& ex)
    {
        throwException(ex);
        RETURN_NULL();
    }
}

ZEND_METHOD(Ice_ObjectPrx, ice_getAdapterId)
{
    if(ZEND_NUM_ARGS() != 0)
    {
        WRONG_PARAM_COUNT;
    }

    ProxyPtr _this = Wrapper<ProxyPtr>::value(getThis());
    assert(_this);

    try
    {
        string id = _this->proxy->ice_getAdapterId();
        ZVAL_STRINGL(return_value, STRCAST(id.c_str()), static_cast<int>(id.length()));
    }
    catch(const IceUtil::Exception& ex)
    {
        throwException(ex);
        RETURN_NULL();
    }
}

ZEND_METHOD(Ice_ObjectPrx, ice_adapterId)
{
    ProxyPtr _this = Wrapper<ProxyPtr>::value(getThis());
    assert(_this);

    char* id;
    size_t len;

    if(zend_parse_parameters(ZEND_NUM_ARGS(), const_cast<char*>("s"), &id, &len) == FAILURE)
    {
        RETURN_NULL();
    }

    try
    {
        if(!_this->clone(return_value, _this->proxy->ice_adapterId(id)))
        {
            RETURN_NULL();
        }
    }
    catch(const IceUtil::Exception& ex)
    {
        throwException(ex);
        RETURN_NULL();
    }
}

ZEND_METHOD(Ice_ObjectPrx, ice_getEndpoints)
{
    if(ZEND_NUM_ARGS() != 0)
    {
        WRONG_PARAM_COUNT;
    }

    ProxyPtr _this = Wrapper<ProxyPtr>::value(getThis());
    assert(_this);

    try
    {
        Ice::EndpointSeq endpoints = _this->proxy->ice_getEndpoints();

        array_init(return_value);
        uint idx = 0;
        for(Ice::EndpointSeq::const_iterator p = endpoints.begin(); p != endpoints.end(); ++p, ++idx)
        {
            zval elem;
            if(!createEndpoint(&elem, *p))
            {
                zval_ptr_dtor(&elem);
                RETURN_NULL();
            }
            add_index_zval(return_value, idx, &elem);
        }
    }
    catch(const IceUtil::Exception& ex)
    {
        throwException(ex);
        RETURN_NULL();
    }
}

ZEND_METHOD(Ice_ObjectPrx, ice_endpoints)
{
    ProxyPtr _this = Wrapper<ProxyPtr>::value(getThis());
    assert(_this);

    zval* zv;

    if(zend_parse_parameters(ZEND_NUM_ARGS(), const_cast<char*>("a"), &zv) == FAILURE)
    {
        RETURN_NULL();
    }

    Ice::EndpointSeq seq;

    HashTable* arr = Z_ARRVAL_P(zv);
    zval* val;
    ZEND_HASH_FOREACH_VAL(arr, val)
    {
        if(Z_TYPE_P(val) != IS_OBJECT)
        {
            runtimeError("expected an element of type Ice::Endpoint");
            RETURN_NULL();
        }

        Ice::EndpointPtr endpoint;
        if(!fetchEndpoint(val, endpoint))
        {
            RETURN_NULL();
        }

        seq.push_back(endpoint);
    }
    ZEND_HASH_FOREACH_END();

    try
    {
        if(!_this->clone(return_value, _this->proxy->ice_endpoints(seq)))
        {
            RETURN_NULL();
        }
    }
    catch(const IceUtil::Exception& ex)
    {
        throwException(ex);
        RETURN_NULL();
    }
}

ZEND_METHOD(Ice_ObjectPrx, ice_getLocatorCacheTimeout)
{
    if(ZEND_NUM_ARGS() != 0)
    {
        WRONG_PARAM_COUNT;
    }

    ProxyPtr _this = Wrapper<ProxyPtr>::value(getThis());
    assert(_this);

    try
    {
        Ice::Int timeout = _this->proxy->ice_getLocatorCacheTimeout();
        ZVAL_LONG(return_value, static_cast<long>(timeout));
    }
    catch(const IceUtil::Exception& ex)
    {
        throwException(ex);
        RETURN_NULL();
    }
}

ZEND_METHOD(Ice_ObjectPrx, ice_getConnectionId)
{
    if(ZEND_NUM_ARGS() != 0)
    {
        WRONG_PARAM_COUNT;
    }

    ProxyPtr _this = Wrapper<ProxyPtr>::value(getThis());
    assert(_this);

    try
    {
        string connectionId = _this->proxy->ice_getConnectionId();
        ZVAL_STRINGL(return_value, STRCAST(connectionId.c_str()), static_cast<int>(connectionId.length()));
    }
    catch(const IceUtil::Exception& ex)
    {
        throwException(ex);
        RETURN_NULL();
    }
}

ZEND_METHOD(Ice_ObjectPrx, ice_locatorCacheTimeout)
{
    ProxyPtr _this = Wrapper<ProxyPtr>::value(getThis());
    assert(_this);

    long l;
    if(zend_parse_parameters(ZEND_NUM_ARGS(), const_cast<char*>("l"), &l) != SUCCESS)
    {
        RETURN_NULL();
    }

    try
    {
        if(!_this->clone(return_value, _this->proxy->ice_locatorCacheTimeout(l)))
        {
            RETURN_NULL();
        }
    }
    catch(const IceUtil::Exception& ex)
    {
        throwException(ex);
        RETURN_NULL();
    }
}

ZEND_METHOD(Ice_ObjectPrx, ice_isConnectionCached)
{
    if(ZEND_NUM_ARGS() != 0)
    {
        WRONG_PARAM_COUNT;
    }

    ProxyPtr _this = Wrapper<ProxyPtr>::value(getThis());
    assert(_this);

    try
    {
        bool b = _this->proxy->ice_isConnectionCached();
        ZVAL_BOOL(return_value, b ? 1 : 0);
    }
    catch(const IceUtil::Exception& ex)
    {
        throwException(ex);
        RETURN_NULL();
    }
}

ZEND_METHOD(Ice_ObjectPrx, ice_connectionCached)
{
    ProxyPtr _this = Wrapper<ProxyPtr>::value(getThis());
    assert(_this);

    zend_bool b;
    if(zend_parse_parameters(ZEND_NUM_ARGS(), const_cast<char*>("b"), &b) != SUCCESS)
    {
        RETURN_NULL();
    }

    try
    {
        if(!_this->clone(return_value, _this->proxy->ice_connectionCached(b ? true : false)))
        {
            RETURN_NULL();
        }
    }
    catch(const IceUtil::Exception& ex)
    {
        throwException(ex);
        RETURN_NULL();
    }
}

ZEND_METHOD(Ice_ObjectPrx, ice_getEndpointSelection)
{
    if(ZEND_NUM_ARGS() != 0)
    {
        WRONG_PARAM_COUNT;
    }

    ProxyPtr _this = Wrapper<ProxyPtr>::value(getThis());
    assert(_this);

    try
    {
        Ice::EndpointSelectionType type = _this->proxy->ice_getEndpointSelection();
        ZVAL_LONG(return_value, type == Ice::Random ? 0 : 1);
    }
    catch(const IceUtil::Exception& ex)
    {
        throwException(ex);
        RETURN_NULL();
    }
}

ZEND_METHOD(Ice_ObjectPrx, ice_endpointSelection)
{
    ProxyPtr _this = Wrapper<ProxyPtr>::value(getThis());
    assert(_this);

    long l;
    if(zend_parse_parameters(ZEND_NUM_ARGS(), const_cast<char*>("l"), &l) != SUCCESS)
    {
        RETURN_NULL();
    }

    if(l < 0 || l > 1)
    {
        runtimeError("expecting Random or Ordered");
        RETURN_NULL();
    }

    try
    {
        Ice::EndpointSelectionType type = l == 0 ? Ice::Random : Ice::Ordered;
        if(!_this->clone(return_value, _this->proxy->ice_endpointSelection(type)))
        {
            RETURN_NULL();
        }
    }
    catch(const IceUtil::Exception& ex)
    {
        throwException(ex);
        RETURN_NULL();
    }
}

ZEND_METHOD(Ice_ObjectPrx, ice_isSecure)
{
    if(ZEND_NUM_ARGS() != 0)
    {
        WRONG_PARAM_COUNT;
    }

    ProxyPtr _this = Wrapper<ProxyPtr>::value(getThis());
    assert(_this);

    try
    {
        bool b = _this->proxy->ice_isSecure();
        RETURN_BOOL(b ? 1 : 0);
    }
    catch(const IceUtil::Exception& ex)
    {
        throwException(ex);
        RETURN_FALSE;
    }
}

ZEND_METHOD(Ice_ObjectPrx, ice_secure)
{
    ProxyPtr _this = Wrapper<ProxyPtr>::value(getThis());
    assert(_this);

    zend_bool b;
    if(zend_parse_parameters(ZEND_NUM_ARGS(), const_cast<char*>("b"), &b) != SUCCESS)
    {
        RETURN_NULL();
    }

    try
    {
        if(!_this->clone(return_value, _this->proxy->ice_secure(b ? true : false)))
        {
            RETURN_NULL();
        }
    }
    catch(const IceUtil::Exception& ex)
    {
        throwException(ex);
        RETURN_NULL();
    }
}

ZEND_METHOD(Ice_ObjectPrx, ice_getEncodingVersion)
{
    if(ZEND_NUM_ARGS() != 0)
    {
        WRONG_PARAM_COUNT;
    }

    ProxyPtr _this = Wrapper<ProxyPtr>::value(getThis());
    assert(_this);

    try
    {
        if(!createEncodingVersion(return_value, _this->proxy->ice_getEncodingVersion()))
        {
            RETURN_NULL();
        }
    }
    catch(const IceUtil::Exception& ex)
    {
        throwException(ex);
        RETURN_NULL();
    }
}

ZEND_METHOD(Ice_ObjectPrx, ice_encodingVersion)
{
    ProxyPtr _this = Wrapper<ProxyPtr>::value(getThis());
    assert(_this);

    zend_class_entry* cls = idToClass("::Ice::EncodingVersion");
    assert(cls);

    zval *zv;
    if(zend_parse_parameters(ZEND_NUM_ARGS(), const_cast<char*>("O"), &zv, cls) == FAILURE)
    {
        RETURN_NULL();
    }

    Ice::EncodingVersion v;
    if(extractEncodingVersion(zv, v))
    {
        try
        {
            if(!_this->clone(return_value, _this->proxy->ice_encodingVersion(v)))
            {
                RETURN_NULL();
            }
        }
        catch(const IceUtil::Exception& ex)
        {
            throwException(ex);
            RETURN_NULL();
        }
    }
}

ZEND_METHOD(Ice_ObjectPrx, ice_isPreferSecure)
{
    if(ZEND_NUM_ARGS() != 0)
    {
        WRONG_PARAM_COUNT;
    }

    ProxyPtr _this = Wrapper<ProxyPtr>::value(getThis());
    assert(_this);

    try
    {
        bool b = _this->proxy->ice_isPreferSecure();
        RETURN_BOOL(b ? 1 : 0);
    }
    catch(const IceUtil::Exception& ex)
    {
        throwException(ex);
        RETURN_FALSE;
    }
}

ZEND_METHOD(Ice_ObjectPrx, ice_preferSecure)
{
    ProxyPtr _this = Wrapper<ProxyPtr>::value(getThis());
    assert(_this);

    zend_bool b;
    if(zend_parse_parameters(ZEND_NUM_ARGS(), const_cast<char*>("b"), &b) != SUCCESS)
    {
        RETURN_NULL();
    }

    try
    {
        if(!_this->clone(return_value, _this->proxy->ice_preferSecure(b ? true : false)))
        {
            RETURN_NULL();
        }
    }
    catch(const IceUtil::Exception& ex)
    {
        throwException(ex);
        RETURN_NULL();
    }
}

ZEND_METHOD(Ice_ObjectPrx, ice_getRouter)
{
    if(ZEND_NUM_ARGS() != 0)
    {
        WRONG_PARAM_COUNT;
    }

    ProxyPtr _this = Wrapper<ProxyPtr>::value(getThis());
    assert(_this);

    try
    {
        Ice::RouterPrx router = _this->proxy->ice_getRouter();
        if(router)
        {
            ClassInfoPtr info = lookupClass("::Ice::Router");
            if(!info)
            {
                RETURN_NULL();
            }

            assert(info);

            if(!createProxy(return_value, router, info, _this->communicator))
            {
                RETURN_NULL();
            }
        }
        else
        {
            RETURN_NULL();
        }
    }
    catch(const IceUtil::Exception& ex)
    {
        throwException(ex);
        RETURN_FALSE;
    }
}

ZEND_METHOD(Ice_ObjectPrx, ice_router)
{
    ProxyPtr _this = Wrapper<ProxyPtr>::value(getThis());
    assert(_this);

    zval* zprx;
    if(zend_parse_parameters(ZEND_NUM_ARGS(), const_cast<char*>("O!"), &zprx, proxyClassEntry) !=
        SUCCESS)
    {
        RETURN_NULL();
    }

    Ice::ObjectPrx proxy;
    ClassInfoPtr def;
    if(zprx && !fetchProxy(zprx, proxy, def))
    {
        RETURN_NULL();
    }

    Ice::RouterPrx router;
    if(proxy)
    {
        if(!def || !def->isA("::Ice::Router"))
        {
            runtimeError("ice_router requires a proxy narrowed to Ice::Router");
            RETURN_NULL();
        }
        router = Ice::RouterPrx::uncheckedCast(proxy);
    }

    try
    {
        if(!_this->clone(return_value, _this->proxy->ice_router(router)))
        {
            RETURN_NULL();
        }
    }
    catch(const IceUtil::Exception& ex)
    {
        throwException(ex);
        RETURN_NULL();
    }
}

ZEND_METHOD(Ice_ObjectPrx, ice_getLocator)
{
    if(ZEND_NUM_ARGS() != 0)
    {
        WRONG_PARAM_COUNT;
    }

    ProxyPtr _this = Wrapper<ProxyPtr>::value(getThis());
    assert(_this);

    try
    {
        Ice::LocatorPrx locator = _this->proxy->ice_getLocator();
        if(locator)
        {
            ClassInfoPtr info = lookupClass("::Ice::Locator");
            if(!info)
            {
                RETURN_NULL();
            }

            if(!createProxy(return_value, locator, info, _this->communicator))
            {
                RETURN_NULL();
            }
        }
        else
        {
            RETURN_NULL();
        }
    }
    catch(const IceUtil::Exception& ex)
    {
        throwException(ex);
        RETURN_FALSE;
    }
}

ZEND_METHOD(Ice_ObjectPrx, ice_locator)
{
    ProxyPtr _this = Wrapper<ProxyPtr>::value(getThis());
    assert(_this);

    zval* zprx;
    if(zend_parse_parameters(ZEND_NUM_ARGS(), const_cast<char*>("O!"), &zprx, proxyClassEntry) !=
        SUCCESS)
    {
        RETURN_NULL();
    }

    Ice::ObjectPrx proxy;
    ClassInfoPtr def;
    if(zprx && !fetchProxy(zprx, proxy, def))
    {
        RETURN_NULL();
    }

    Ice::LocatorPrx locator;
    if(proxy)
    {
        if(!def || !def->isA("::Ice::Locator"))
        {
            runtimeError("ice_locator requires a proxy narrowed to Ice::Locator");
            RETURN_NULL();
        }
        locator = Ice::LocatorPrx::uncheckedCast(proxy);
    }

    try
    {
        if(!_this->clone(return_value, _this->proxy->ice_locator(locator)))
        {
            RETURN_NULL();
        }
    }
    catch(const IceUtil::Exception& ex)
    {
        throwException(ex);
        RETURN_NULL();
    }
}

ZEND_METHOD(Ice_ObjectPrx, ice_twoway)
{
    if(ZEND_NUM_ARGS() != 0)
    {
        WRONG_PARAM_COUNT;
    }

    ProxyPtr _this = Wrapper<ProxyPtr>::value(getThis());
    assert(_this);

    try
    {
        if(!_this->clone(return_value, _this->proxy->ice_twoway()))
        {
            RETURN_NULL();
        }
    }
    catch(const IceUtil::Exception& ex)
    {
        throwException(ex);
        RETURN_NULL();
    }
}

ZEND_METHOD(Ice_ObjectPrx, ice_isTwoway)
{
    if(ZEND_NUM_ARGS() != 0)
    {
        WRONG_PARAM_COUNT;
    }

    ProxyPtr _this = Wrapper<ProxyPtr>::value(getThis());
    assert(_this);

    try
    {
        bool b = _this->proxy->ice_isTwoway();
        RETURN_BOOL(b ? 1 : 0);
    }
    catch(const IceUtil::Exception& ex)
    {
        throwException(ex);
        RETURN_FALSE;
    }
}

ZEND_METHOD(Ice_ObjectPrx, ice_oneway)
{
    if(ZEND_NUM_ARGS() != 0)
    {
        WRONG_PARAM_COUNT;
    }

    ProxyPtr _this = Wrapper<ProxyPtr>::value(getThis());
    assert(_this);

    try
    {
        if(!_this->clone(return_value, _this->proxy->ice_oneway()))
        {
            RETURN_NULL();
        }
    }
    catch(const IceUtil::Exception& ex)
    {
        throwException(ex);
        RETURN_NULL();
    }
}

ZEND_METHOD(Ice_ObjectPrx, ice_isOneway)
{
    if(ZEND_NUM_ARGS() != 0)
    {
        WRONG_PARAM_COUNT;
    }

    ProxyPtr _this = Wrapper<ProxyPtr>::value(getThis());
    assert(_this);

    try
    {
        bool b = _this->proxy->ice_isOneway();
        RETURN_BOOL(b ? 1 : 0);
    }
    catch(const IceUtil::Exception& ex)
    {
        throwException(ex);
        RETURN_FALSE;
    }
}

ZEND_METHOD(Ice_ObjectPrx, ice_batchOneway)
{
    if(ZEND_NUM_ARGS() != 0)
    {
        WRONG_PARAM_COUNT;
    }

    ProxyPtr _this = Wrapper<ProxyPtr>::value(getThis());
    assert(_this);

    try
    {
        if(!_this->clone(return_value, _this->proxy->ice_batchOneway()))
        {
            RETURN_NULL();
        }
    }
    catch(const IceUtil::Exception& ex)
    {
        throwException(ex);
        RETURN_NULL();
    }
}

ZEND_METHOD(Ice_ObjectPrx, ice_isBatchOneway)
{
    if(ZEND_NUM_ARGS() != 0)
    {
        WRONG_PARAM_COUNT;
    }

    ProxyPtr _this = Wrapper<ProxyPtr>::value(getThis());
    assert(_this);

    try
    {
        bool b = _this->proxy->ice_isBatchOneway();
        RETURN_BOOL(b ? 1 : 0);
    }
    catch(const IceUtil::Exception& ex)
    {
        throwException(ex);
        RETURN_FALSE;
    }
}

ZEND_METHOD(Ice_ObjectPrx, ice_datagram)
{
    if(ZEND_NUM_ARGS() != 0)
    {
        WRONG_PARAM_COUNT;
    }

    ProxyPtr _this = Wrapper<ProxyPtr>::value(getThis());
    assert(_this);

    try
    {
        if(!_this->clone(return_value, _this->proxy->ice_datagram()))
        {
            RETURN_NULL();
        }
    }
    catch(const IceUtil::Exception& ex)
    {
        throwException(ex);
        RETURN_NULL();
    }
}

ZEND_METHOD(Ice_ObjectPrx, ice_isDatagram)
{
    if(ZEND_NUM_ARGS() != 0)
    {
        WRONG_PARAM_COUNT;
    }

    ProxyPtr _this = Wrapper<ProxyPtr>::value(getThis());
    assert(_this);

    try
    {
        bool b = _this->proxy->ice_isDatagram();
        RETURN_BOOL(b ? 1 : 0);
    }
    catch(const IceUtil::Exception& ex)
    {
        throwException(ex);
        RETURN_FALSE;
    }
}

ZEND_METHOD(Ice_ObjectPrx, ice_batchDatagram)
{
    if(ZEND_NUM_ARGS() != 0)
    {
        WRONG_PARAM_COUNT;
    }

    ProxyPtr _this = Wrapper<ProxyPtr>::value(getThis());
    assert(_this);

    try
    {
        if(!_this->clone(return_value, _this->proxy->ice_batchDatagram()))
        {
            RETURN_NULL();
        }
    }
    catch(const IceUtil::Exception& ex)
    {
        throwException(ex);
        RETURN_NULL();
    }
}

ZEND_METHOD(Ice_ObjectPrx, ice_isBatchDatagram)
{
    if(ZEND_NUM_ARGS() != 0)
    {
        WRONG_PARAM_COUNT;
    }

    ProxyPtr _this = Wrapper<ProxyPtr>::value(getThis());
    assert(_this);

    try
    {
        bool b = _this->proxy->ice_isBatchDatagram();
        RETURN_BOOL(b ? 1 : 0);
    }
    catch(const IceUtil::Exception& ex)
    {
        throwException(ex);
        RETURN_FALSE;
    }
}

ZEND_METHOD(Ice_ObjectPrx, ice_compress)
{
    ProxyPtr _this = Wrapper<ProxyPtr>::value(getThis());
    assert(_this);

    zend_bool b;
    if(zend_parse_parameters(ZEND_NUM_ARGS(), const_cast<char*>("b"), &b) != SUCCESS)
    {
        RETURN_NULL();
    }

    try
    {
        if(!_this->clone(return_value, _this->proxy->ice_compress(b ? true : false)))
        {
            RETURN_NULL();
        }
    }
    catch(const IceUtil::Exception& ex)
    {
        throwException(ex);
        RETURN_NULL();
    }
}

ZEND_METHOD(Ice_ObjectPrx, ice_timeout)
{
    ProxyPtr _this = Wrapper<ProxyPtr>::value(getThis());
    assert(_this);

    try
    {
        long l;
        if(zend_parse_parameters(ZEND_NUM_ARGS(), const_cast<char*>("l"), &l) != SUCCESS)
        {
            RETURN_NULL();
        }
        // TODO: range check?
        if(!_this->clone(return_value, _this->proxy->ice_timeout(l)))
        {
            RETURN_NULL();
        }
    }
    catch(const IceUtil::Exception& ex)
    {
        throwException(ex);
        RETURN_NULL();
    }
}

ZEND_METHOD(Ice_ObjectPrx, ice_connectionId)
{
    ProxyPtr _this = Wrapper<ProxyPtr>::value(getThis());
    assert(_this);

    try
    {
        char* id;
        size_t idLen;
        if(zend_parse_parameters(ZEND_NUM_ARGS(), const_cast<char*>("s"), &id, &idLen) != SUCCESS)
        {
            RETURN_NULL();
        }
        if(!_this->clone(return_value, _this->proxy->ice_connectionId(id)))
        {
            RETURN_NULL();
        }
    }
    catch(const IceUtil::Exception& ex)
    {
        throwException(ex);
        RETURN_NULL();
    }
}

ZEND_METHOD(Ice_ObjectPrx, ice_getConnection)
{
    if(ZEND_NUM_ARGS() != 0)
    {
        WRONG_PARAM_COUNT;
    }

    ProxyPtr _this = Wrapper<ProxyPtr>::value(getThis());
    assert(_this);

    try
    {
        Ice::ConnectionPtr con = _this->proxy->ice_getConnection();
        if(!createConnection(return_value, con))
        {
            RETURN_NULL();
        }
    }
    catch(const IceUtil::Exception& ex)
    {
        throwException(ex);
        RETURN_NULL();
    }
}

ZEND_METHOD(Ice_ObjectPrx, ice_getCachedConnection)
{
    if(ZEND_NUM_ARGS() != 0)
    {
        WRONG_PARAM_COUNT;
    }

    ProxyPtr _this = Wrapper<ProxyPtr>::value(getThis());
    assert(_this);

    try
    {
        Ice::ConnectionPtr con = _this->proxy->ice_getCachedConnection();
        if(!con || !createConnection(return_value, con))
        {
            RETURN_NULL();
        }
    }
    catch(const IceUtil::Exception& ex)
    {
        throwException(ex);
        RETURN_NULL();
    }
}

ZEND_METHOD(Ice_ObjectPrx, ice_flushBatchRequests)
{
    if(ZEND_NUM_ARGS() != 0)
    {
        WRONG_PARAM_COUNT;
    }

    ProxyPtr _this = Wrapper<ProxyPtr>::value(getThis());
    assert(_this);

    try
    {
        _this->proxy->ice_flushBatchRequests();
    }
    catch(const IceUtil::Exception& ex)
    {
        throwException(ex);
        RETURN_NULL();
    }
}

static ClassInfoPtr
lookupClass(const string& id)
{
    ClassInfoPtr info = getClassInfoById(id);
    if(!info)
    {
        if(!id.empty() && id[id.size() - 1] == '*')
        {
            info = getClassInfoById(id.substr(0, id.size() - 1));
        }
    }

    if(info && !info->defined)
    {
        runtimeError("%s is declared but not defined", id.c_str());
    }
    else if(!info)
    {
        runtimeError("no definition found for class or interface %s", id.c_str());
    }

    return info;
}

static void
do_cast(INTERNAL_FUNCTION_PARAMETERS, bool check)
{
    //
    // First argument is required and should be a scoped name. The second and third arguments
    // are optional and represent a facet name, a context, or a facet name followed by a context.
    //
    if(ZEND_NUM_ARGS() < 1 || ZEND_NUM_ARGS() > 3)
    {
        WRONG_PARAM_COUNT;
    }

    char* id;
    size_t idLen;
    char* facet = 0;
    size_t facetLen;
    zval* arr = 0;

    if(zend_parse_parameters_ex(ZEND_PARSE_PARAMS_QUIET, ZEND_NUM_ARGS(), const_cast<char*>("s|s!a!"), &id,
                                &idLen, &facet, &facetLen, &arr) == FAILURE)
    {
        facet = 0;
        if(zend_parse_parameters_ex(ZEND_PARSE_PARAMS_QUIET, ZEND_NUM_ARGS(), const_cast<char*>("s|a!"), &id,
                                    &idLen, &arr) == FAILURE)
        {
            php_error(E_ERROR, "%s() requires a type id followed by an optional facet and/or context",
                      get_active_function_name());
            return;
        }
    }

    ProxyPtr _this = Wrapper<ProxyPtr>::value(getThis());
    assert(_this);

    //
    // Populate the context.
    //
    Ice::Context ctx;
    if(arr && !extractStringMap(arr, ctx))
    {
        RETURN_NULL();
    }

    try
    {
        ClassInfoPtr info = lookupClass(id);
        if(!info)
        {
            RETURN_NULL();
        }

        Ice::ObjectPrx prx = _this->proxy;
        if(facet)
        {
            prx = prx->ice_facet(facet);
        }

        if(arr)
        {
            prx = prx->ice_context(ctx);
        }

        if(check)
        {
            //
            // Verify that the object supports the requested type.
            //
            if(!prx->ice_isA(info->id))
            {
                RETURN_NULL();
            }
        }

        if(!createProxy(return_value, prx, info, _this->communicator))
        {
            RETURN_NULL();
        }
    }
    catch(const Ice::FacetNotExistException&)
    {
        // Ignore.
    }
    catch(const IceUtil::Exception& ex)
    {
        throwException(ex);
        RETVAL_FALSE;
    }
}

ZEND_METHOD(Ice_ObjectPrx, ice_uncheckedCast)
{
    do_cast(INTERNAL_FUNCTION_PARAM_PASSTHRU, false);
}

ZEND_METHOD(Ice_ObjectPrx, ice_checkedCast)
{
    do_cast(INTERNAL_FUNCTION_PARAM_PASSTHRU, true);
}

IcePHP::Proxy::Proxy(const Ice::ObjectPrx& p, const ClassInfoPtr& i, const CommunicatorInfoPtr& comm) :
    proxy(p), info(i), communicator(comm), connection(0), cachedConnection(0)
{
    //
    // We want to ensure that the PHP object corresponding to the communicator is
    // not destroyed until after this proxy is destroyed.
    //
    communicator->addRef();
}

IcePHP::Proxy::~Proxy()
{
    communicator->decRef();
    if(connection)
    {
        zval_ptr_dtor(connection);
    }
    if(cachedConnection)
    {
        zval_ptr_dtor(cachedConnection);
    }
}

bool
IcePHP::Proxy::clone(zval* zv, const Ice::ObjectPrx& p)
{
    return create(zv, p, info, communicator);
}

bool
IcePHP::Proxy::cloneUntyped(zval* zv, const Ice::ObjectPrx& p)
{
    return create(zv, p, 0, communicator);
}

bool
IcePHP::Proxy::create(zval* zv, const Ice::ObjectPrx& p, const ClassInfoPtr& info, const CommunicatorInfoPtr& comm
                     )
{
    ClassInfoPtr cls = info;
    if(!cls)
    {
        cls = getClassInfoById("::Ice::Object");
        assert(cls);
    }

    if(object_init_ex(zv, proxyClassEntry) != SUCCESS)
    {
        runtimeError("unable to initialize proxy");
        return false;
    }

    Wrapper<ProxyPtr>* obj = Wrapper<ProxyPtr>::extract(zv);
    ProxyPtr proxy = new Proxy(p, cls, comm);
    assert(!obj->ptr);
    obj->ptr = new ProxyPtr(proxy);
    return true;
}

#ifdef _WIN32
extern "C"
#endif
static zend_object*
handleAlloc(zend_class_entry* ce)
{
    Wrapper<ProxyPtr>* obj = Wrapper<ProxyPtr>::create(ce);
    assert(obj);

    obj->zobj.handlers = &_handlers;

    return &obj->zobj;
}

#ifdef _WIN32
extern "C"
#endif
static void
handleFreeStorage(zend_object* object)
{
    Wrapper<ProxyPtr>* obj = Wrapper<ProxyPtr>::fetch(object);
    delete obj->ptr;
    zend_object_std_dtor(object);
}

#ifdef _WIN32
extern "C"
#endif
static zend_object*
handleClone(zval* zv)
{
    //
    // Create a new object that shares a C++ proxy instance with this object.
    //

    ProxyPtr obj = Wrapper<ProxyPtr>::value(zv);
    assert(obj);

    zval clone;
    if(!obj->clone(&clone, obj->proxy))
    {
        return 0;
    }

    return Z_OBJ(clone);
}

#ifdef _WIN32
extern "C"
#endif
static union _zend_function*
handleGetMethod(zend_object **object, zend_string *name, const zval *key )
{
    zend_function* result;
    //
    // First delegate to the standard implementation of get_method. This will find
    // any of our predefined proxy methods. If it returns 0, then we return a
    // function that will check the class definition.
    //
    result = zend_get_std_object_handlers()->get_method(object, name, key);
    if(!result)
    {
        Wrapper<ProxyPtr>* obj = Wrapper<ProxyPtr>::fetch(*object);
        assert(obj->ptr);
        ProxyPtr _this = *obj->ptr;

        ClassInfoPtr info = _this->info;
        assert(info);

        OperationPtr op = info->getOperation(name->val);
        if(!op)
        {
            //
            // Returning 0 causes PHP to report an "undefined method" error.
            //
            return 0;
        }

        result = op->function();
    }

    return result;
}

#ifdef _WIN32
extern "C"
#endif
static int
handleCompare(zval* zobj1, zval* zobj2)
{
    //
    // PHP guarantees that the objects have the same class.
    //

    Wrapper<ProxyPtr>* obj1 = Wrapper<ProxyPtr>::extract(zobj1);
    assert(obj1->ptr);
    ProxyPtr _this1 = *obj1->ptr;
    Ice::ObjectPrx prx1 = _this1->proxy;

    Wrapper<ProxyPtr>* obj2 = Wrapper<ProxyPtr>::extract(zobj2);
    assert(obj2->ptr);
    ProxyPtr _this2 = *obj2->ptr;
    Ice::ObjectPrx prx2 = _this2->proxy;

    if(prx1 == prx2)
    {
        return 0;
    }
    else if(prx1 < prx2)
    {
        return -1;
    }
    else
    {
        return 1;
    }
}

//
// Necessary to suppress warnings from zend_function_entry in php-5.2.
//
#if defined(__GNUC__)
#  pragma GCC diagnostic ignored "-Wwrite-strings"
#endif

//
// Predefined methods for ObjectPrx.
//
static zend_function_entry _proxyMethods[] =
{
    ZEND_ME(Ice_ObjectPrx, __construct, NULL, ZEND_ACC_PRIVATE|ZEND_ACC_CTOR)
    ZEND_ME(Ice_ObjectPrx, __toString, NULL, ZEND_ACC_PUBLIC)
    ZEND_ME(Ice_ObjectPrx, ice_getCommunicator, NULL, ZEND_ACC_PUBLIC)
    ZEND_ME(Ice_ObjectPrx, ice_toString, NULL, ZEND_ACC_PUBLIC)
    ZEND_ME(Ice_ObjectPrx, ice_getIdentity, NULL, ZEND_ACC_PUBLIC)
    ZEND_ME(Ice_ObjectPrx, ice_identity, NULL, ZEND_ACC_PUBLIC)
    ZEND_ME(Ice_ObjectPrx, ice_getContext, NULL, ZEND_ACC_PUBLIC)
    ZEND_ME(Ice_ObjectPrx, ice_context, NULL, ZEND_ACC_PUBLIC)
    ZEND_ME(Ice_ObjectPrx, ice_getFacet, NULL, ZEND_ACC_PUBLIC)
    ZEND_ME(Ice_ObjectPrx, ice_facet, NULL, ZEND_ACC_PUBLIC)
    ZEND_ME(Ice_ObjectPrx, ice_getAdapterId, NULL, ZEND_ACC_PUBLIC)
    ZEND_ME(Ice_ObjectPrx, ice_adapterId, NULL, ZEND_ACC_PUBLIC)
    ZEND_ME(Ice_ObjectPrx, ice_getEndpoints, NULL, ZEND_ACC_PUBLIC)
    ZEND_ME(Ice_ObjectPrx, ice_endpoints, NULL, ZEND_ACC_PUBLIC)
    ZEND_ME(Ice_ObjectPrx, ice_getLocatorCacheTimeout, NULL, ZEND_ACC_PUBLIC)
    ZEND_ME(Ice_ObjectPrx, ice_getConnectionId, NULL, ZEND_ACC_PUBLIC)
    ZEND_ME(Ice_ObjectPrx, ice_locatorCacheTimeout, NULL, ZEND_ACC_PUBLIC)
    ZEND_ME(Ice_ObjectPrx, ice_isConnectionCached, NULL, ZEND_ACC_PUBLIC)
    ZEND_ME(Ice_ObjectPrx, ice_connectionCached, NULL, ZEND_ACC_PUBLIC)
    ZEND_ME(Ice_ObjectPrx, ice_getEndpointSelection, NULL, ZEND_ACC_PUBLIC)
    ZEND_ME(Ice_ObjectPrx, ice_endpointSelection, NULL, ZEND_ACC_PUBLIC)
    ZEND_ME(Ice_ObjectPrx, ice_isSecure, NULL, ZEND_ACC_PUBLIC)
    ZEND_ME(Ice_ObjectPrx, ice_secure, NULL, ZEND_ACC_PUBLIC)
    ZEND_ME(Ice_ObjectPrx, ice_getEncodingVersion, NULL, ZEND_ACC_PUBLIC)
    ZEND_ME(Ice_ObjectPrx, ice_encodingVersion, NULL, ZEND_ACC_PUBLIC)
    ZEND_ME(Ice_ObjectPrx, ice_isPreferSecure, NULL, ZEND_ACC_PUBLIC)
    ZEND_ME(Ice_ObjectPrx, ice_preferSecure, NULL, ZEND_ACC_PUBLIC)
    ZEND_ME(Ice_ObjectPrx, ice_getRouter, NULL, ZEND_ACC_PUBLIC)
    ZEND_ME(Ice_ObjectPrx, ice_router, NULL, ZEND_ACC_PUBLIC)
    ZEND_ME(Ice_ObjectPrx, ice_getLocator, NULL, ZEND_ACC_PUBLIC)
    ZEND_ME(Ice_ObjectPrx, ice_locator, NULL, ZEND_ACC_PUBLIC)
    ZEND_ME(Ice_ObjectPrx, ice_twoway, NULL, ZEND_ACC_PUBLIC)
    ZEND_ME(Ice_ObjectPrx, ice_isTwoway, NULL, ZEND_ACC_PUBLIC)
    ZEND_ME(Ice_ObjectPrx, ice_oneway, NULL, ZEND_ACC_PUBLIC)
    ZEND_ME(Ice_ObjectPrx, ice_isOneway, NULL, ZEND_ACC_PUBLIC)
    ZEND_ME(Ice_ObjectPrx, ice_batchOneway, NULL, ZEND_ACC_PUBLIC)
    ZEND_ME(Ice_ObjectPrx, ice_isBatchOneway, NULL, ZEND_ACC_PUBLIC)
    ZEND_ME(Ice_ObjectPrx, ice_datagram, NULL, ZEND_ACC_PUBLIC)
    ZEND_ME(Ice_ObjectPrx, ice_isDatagram, NULL, ZEND_ACC_PUBLIC)
    ZEND_ME(Ice_ObjectPrx, ice_batchDatagram, NULL, ZEND_ACC_PUBLIC)
    ZEND_ME(Ice_ObjectPrx, ice_isBatchDatagram, NULL, ZEND_ACC_PUBLIC)
    ZEND_ME(Ice_ObjectPrx, ice_compress, NULL, ZEND_ACC_PUBLIC)
    ZEND_ME(Ice_ObjectPrx, ice_timeout, NULL, ZEND_ACC_PUBLIC)
    ZEND_ME(Ice_ObjectPrx, ice_connectionId, NULL, ZEND_ACC_PUBLIC)
    ZEND_ME(Ice_ObjectPrx, ice_getConnection, NULL, ZEND_ACC_PUBLIC)
    ZEND_ME(Ice_ObjectPrx, ice_getCachedConnection, NULL, ZEND_ACC_PUBLIC)
    ZEND_ME(Ice_ObjectPrx, ice_flushBatchRequests, NULL, ZEND_ACC_PUBLIC)
    ZEND_ME(Ice_ObjectPrx, ice_uncheckedCast, NULL, ZEND_ACC_PUBLIC)
    ZEND_ME(Ice_ObjectPrx, ice_checkedCast, NULL, ZEND_ACC_PUBLIC)
    {0, 0, 0}
};

//
// enable warning again
//
#if defined(__GNUC__)
#  pragma GCC diagnostic error "-Wwrite-strings"
#endif

bool
IcePHP::proxyInit(void)
{
    //
    // Register the ObjectPrx class.
    //
    zend_class_entry ce;
#ifdef ICEPHP_USE_NAMESPACES
    INIT_NS_CLASS_ENTRY(ce, "Ice", "ObjectPrx", _proxyMethods);
#else
    INIT_CLASS_ENTRY(ce, "Ice_ObjectPrx", _proxyMethods);
#endif
    ce.create_object = handleAlloc;
    proxyClassEntry = zend_register_internal_class(&ce);
    //proxyClassEntry->ce_flags |= ZEND_ACC_EXPLICIT_ABSTRACT_CLASS;
    memcpy(&_handlers, zend_get_std_object_handlers(), sizeof(zend_object_handlers));
    _handlers.clone_obj = handleClone;
    _handlers.get_method = handleGetMethod;
    _handlers.compare_objects = handleCompare;
    _handlers.free_obj = handleFreeStorage;
    _handlers.offset =  XtOffsetOf(Wrapper<ProxyPtr>, zobj);

    return true;
}

bool
IcePHP::createProxy(zval* zv, const Ice::ObjectPrx& p, const CommunicatorInfoPtr& comm)
{
    return Proxy::create(zv, p, 0, comm);
}

bool
IcePHP::createProxy(zval* zv, const Ice::ObjectPrx& p, const ClassInfoPtr& info, const CommunicatorInfoPtr& comm
                   )
{
    return Proxy::create(zv, p, info, comm);
}

bool
IcePHP::fetchProxy(zval* zv, Ice::ObjectPrx& prx, ClassInfoPtr& cls)
{
    CommunicatorInfoPtr comm;
    return fetchProxy(zv, prx, cls, comm);
}

bool
IcePHP::fetchProxy(zval* zv, Ice::ObjectPrx& prx, ClassInfoPtr& cls, CommunicatorInfoPtr& comm)
{
    if(!ZVAL_IS_NULL(zv))
    {
        if(Z_TYPE_P(zv) != IS_OBJECT || Z_OBJCE_P(zv) != proxyClassEntry)
        {
            invalidArgument("value is not a proxy");
            return false;
        }
        Wrapper<ProxyPtr>* obj = Wrapper<ProxyPtr>::extract(zv);
        if(!obj)
        {
            runtimeError("unable to retrieve proxy object from object store");
            return false;
        }
        assert(obj->ptr);
        prx = (*obj->ptr)->proxy;
        cls = (*obj->ptr)->info;
        comm = (*obj->ptr)->communicator;
    }
    return true;
}
