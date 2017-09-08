// **********************************************************************
//
// Copyright (c) 2003-2017 ZeroC, Inc. All rights reserved.
//
// This copy of Ice is licensed to you under the terms described in the
// ICE_LICENSE file included in this distribution.
//
// **********************************************************************

#include <Ice/Ice.h>
#include <BlobjectI.h>

using namespace std;

class Callback : public IceUtil::Shared
{
public:

    Callback(const Ice::AMD_Object_ice_invokePtr& cb, bool twoway) :
        _cb(cb), _twoway(twoway)
    {
    }

    void response(bool ok, const vector<Ice::Byte>& encaps)
    {
        _cb->ice_response(ok, encaps);
    }

    void exception(const Ice::Exception& ex)
    {
        _cb->ice_exception(ex);
    }

    void sent(bool)
    {
        if(!_twoway)
        {
            _cb->ice_response(true, vector<Ice::Byte>());
        }
    }

private:

    Ice::AMD_Object_ice_invokePtr _cb;
    bool _twoway;
};
typedef IceUtil::Handle<Callback> CallbackPtr;

BlobjectI::BlobjectI() :
    _startBatch(false)
{
}

void
BlobjectI::startBatch()
{
    assert(!_batchProxy);
    _startBatch = true;
}

void
BlobjectI::flushBatch()
{
    assert(_batchProxy);
    _batchProxy->ice_flushBatchRequests();
    _batchProxy = 0;
}

void
BlobjectI::ice_invoke_async(const Ice::AMD_Object_ice_invokePtr& amdCb, const vector<Ice::Byte>& inEncaps,
                            const Ice::Current& current)
{
    const bool twoway = current.requestId > 0;
    Ice::ObjectPrx obj = current.con->createProxy(current.id);
    if(!twoway)
    {
        if(_startBatch)
        {
            _startBatch = false;
            _batchProxy = obj->ice_batchOneway();
        }
        if(_batchProxy)
        {
            obj = _batchProxy;
        }

        if(!current.facet.empty())
        {
            obj = obj->ice_facet(current.facet);
        }

        if(_batchProxy)
        {
            vector<Ice::Byte> out;
            obj->ice_invoke(current.operation, current.mode, inEncaps, out, current.ctx);
            amdCb->ice_response(true, vector<Ice::Byte>());
        }
        else
        {
            CallbackPtr cb = new Callback(amdCb, false);
            Ice::Callback_Object_ice_invokePtr del =
                Ice::newCallback_Object_ice_invoke(cb, &Callback::response, &Callback::exception, &Callback::sent);
            obj->ice_oneway()->begin_ice_invoke(current.operation, current.mode, inEncaps, current.ctx, del);
        }
    }
    else
    {
        if(!current.facet.empty())
        {
            obj = obj->ice_facet(current.facet);
        }

        CallbackPtr cb = new Callback(amdCb, true);
        Ice::Callback_Object_ice_invokePtr del =
            Ice::newCallback_Object_ice_invoke(cb, &Callback::response, &Callback::exception, &Callback::sent);
        obj->begin_ice_invoke(current.operation, current.mode, inEncaps, current.ctx, del);
    }
}
