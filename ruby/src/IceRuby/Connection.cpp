// **********************************************************************
//
// Copyright (c) 2003-2017 ZeroC, Inc. All rights reserved.
//
// This copy of Ice is licensed to you under the terms described in the
// ICE_LICENSE file included in this distribution.
//
// **********************************************************************

#include <Connection.h>
#include <Endpoint.h>
#include <Types.h>
#include <Util.h>
#include <Ice/Object.h>
#include <IceSSL/ConnectionInfo.h>

using namespace std;
using namespace IceRuby;

static VALUE _connectionClass;

static VALUE _connectionInfoClass;
static VALUE _ipConnectionInfoClass;
static VALUE _tcpConnectionInfoClass;
static VALUE _udpConnectionInfoClass;
static VALUE _wsConnectionInfoClass;
static VALUE _sslConnectionInfoClass;
static VALUE _wssConnectionInfoClass;

// **********************************************************************
// Connection
// **********************************************************************

extern "C"
void
IceRuby_Connection_free(Ice::ConnectionPtr* p)
{
    assert(p);
    delete p;
}

VALUE
IceRuby::createConnection(const Ice::ConnectionPtr& p)
{
    return Data_Wrap_Struct(_connectionClass, 0, IceRuby_Connection_free, new Ice::ConnectionPtr(p));
}

extern "C"
VALUE
IceRuby_Connection_close(VALUE self, VALUE b)
{
    ICE_RUBY_TRY
    {
        Ice::ConnectionPtr* p = reinterpret_cast<Ice::ConnectionPtr*>(DATA_PTR(self));
        assert(p);

        (*p)->close(RTEST(b));
    }
    ICE_RUBY_CATCH
    return Qnil;
}

extern "C"
VALUE
IceRuby_Connection_flushBatchRequests(VALUE self)
{
    ICE_RUBY_TRY
    {
        Ice::ConnectionPtr* p = reinterpret_cast<Ice::ConnectionPtr*>(DATA_PTR(self));
        assert(p);

        (*p)->flushBatchRequests();
    }
    ICE_RUBY_CATCH
    return Qnil;
}

extern "C"
VALUE
IceRuby_Connection_setACM(VALUE self, VALUE t, VALUE c, VALUE h)
{
    ICE_RUBY_TRY
    {
        Ice::ConnectionPtr* p = reinterpret_cast<Ice::ConnectionPtr*>(DATA_PTR(self));
        assert(p);

        IceUtil::Optional<Ice::Int> timeout;
        IceUtil::Optional<Ice::ACMClose> close;
        IceUtil::Optional<Ice::ACMHeartbeat> heartbeat;

        if(t != Unset)
        {
            timeout = static_cast<Ice::Int>(getInteger(t));
        }

        if(c != Unset)
        {
            volatile VALUE type = callRuby(rb_path2class, "Ice::ACMClose");
            if(callRuby(rb_obj_is_instance_of, c, type) != Qtrue)
            {
                throw RubyException(rb_eTypeError,
                    "value for 'close' argument must be Unset or an enumerator of Ice.ACMClose");
            }
            volatile VALUE closeValue = callRuby(rb_funcall, c, rb_intern("to_i"), 0);
            assert(TYPE(closeValue) == T_FIXNUM);
            close = static_cast<Ice::ACMClose>(FIX2LONG(closeValue));
        }

        if(h != Unset)
        {
            volatile VALUE type = callRuby(rb_path2class, "Ice::ACMHeartbeat");
            if(callRuby(rb_obj_is_instance_of, h, type) != Qtrue)
            {
                throw RubyException(rb_eTypeError,
                    "value for 'heartbeat' argument must be Unset or an enumerator of Ice.ACMHeartbeat");
            }
            volatile VALUE heartbeatValue = callRuby(rb_funcall, h, rb_intern("to_i"), 0);
            assert(TYPE(heartbeatValue) == T_FIXNUM);
            heartbeat = static_cast<Ice::ACMHeartbeat>(FIX2LONG(heartbeatValue));
        }

        (*p)->setACM(timeout, close, heartbeat);
    }
    ICE_RUBY_CATCH
    return Qnil;
}

extern "C"
VALUE
IceRuby_Connection_getACM(VALUE self)
{
    ICE_RUBY_TRY
    {
        Ice::ConnectionPtr* p = reinterpret_cast<Ice::ConnectionPtr*>(DATA_PTR(self));
        assert(p);

        Ice::ACM acm = (*p)->getACM();
        volatile VALUE type = callRuby(rb_path2class, "Ice::ACM");
        assert(type != Qnil);
        volatile VALUE r = callRuby(rb_class_new_instance, 0, static_cast<VALUE*>(0), type);
        assert(r != Qnil);

        callRuby(rb_ivar_set, r, rb_intern("@timeout"), LONG2FIX(acm.timeout));

        type = callRuby(rb_path2class, "Ice::ACMClose");
        assert(type != Qnil);
        volatile VALUE c = callRuby(rb_funcall, type, rb_intern("from_int"), 1, LONG2NUM(static_cast<int>(acm.close)));
        callRuby(rb_ivar_set, r, rb_intern("@close"), c);

        type = callRuby(rb_path2class, "Ice::ACMHeartbeat");
        assert(type != Qnil);
        volatile VALUE h =
            callRuby(rb_funcall, type, rb_intern("from_int"), 1, LONG2NUM(static_cast<int>(acm.heartbeat)));
        callRuby(rb_ivar_set, r, rb_intern("@heartbeat"), h);

        return r;
    }
    ICE_RUBY_CATCH
    return Qnil;
}

extern "C"
VALUE
IceRuby_Connection_type(VALUE self)
{
    ICE_RUBY_TRY
    {
        Ice::ConnectionPtr* p = reinterpret_cast<Ice::ConnectionPtr*>(DATA_PTR(self));
        assert(p);

        string s = (*p)->type();
        return createString(s);
    }
    ICE_RUBY_CATCH
    return Qnil;
}

extern "C"
VALUE
IceRuby_Connection_timeout(VALUE self)
{
    ICE_RUBY_TRY
    {
        Ice::ConnectionPtr* p = reinterpret_cast<Ice::ConnectionPtr*>(DATA_PTR(self));
        assert(p);

        Ice::Int timeout = (*p)->timeout();
        return INT2FIX(timeout);
    }
    ICE_RUBY_CATCH
    return Qnil;
}

extern "C"
VALUE
IceRuby_Connection_getInfo(VALUE self)
{
    ICE_RUBY_TRY
    {
        Ice::ConnectionPtr* p = reinterpret_cast<Ice::ConnectionPtr*>(DATA_PTR(self));
        assert(p);

        Ice::ConnectionInfoPtr info = (*p)->getInfo();
        return createConnectionInfo(info);
    }
    ICE_RUBY_CATCH
    return Qnil;
}

extern "C"
VALUE
IceRuby_Connection_getEndpoint(VALUE self)
{
    ICE_RUBY_TRY
    {
        Ice::ConnectionPtr* p = reinterpret_cast<Ice::ConnectionPtr*>(DATA_PTR(self));
        assert(p);

        Ice::EndpointPtr endpoint = (*p)->getEndpoint();
        return createEndpoint(endpoint);
    }
    ICE_RUBY_CATCH
    return Qnil;
}
extern "C"
VALUE
IceRuby_Connection_setBufferSize(VALUE self, VALUE r, VALUE s)
{
    ICE_RUBY_TRY
    {
        Ice::ConnectionPtr* p = reinterpret_cast<Ice::ConnectionPtr*>(DATA_PTR(self));
        assert(p);

        int rcvSize = static_cast<int>(getInteger(r));
        int sndSize = static_cast<int>(getInteger(s));

        (*p)->setBufferSize(rcvSize, sndSize);
    }
    ICE_RUBY_CATCH
    return Qnil;
}

extern "C"
VALUE
IceRuby_Connection_toString(VALUE self)
{
    ICE_RUBY_TRY
    {
        Ice::ConnectionPtr* p = reinterpret_cast<Ice::ConnectionPtr*>(DATA_PTR(self));
        assert(p);

        string s = (*p)->toString();
        return createString(s);
    }
    ICE_RUBY_CATCH
    return Qnil;
}

extern "C"
VALUE
IceRuby_Connection_equals(VALUE self, VALUE other)
{
    ICE_RUBY_TRY
    {
        if(NIL_P(other))
        {
            return Qfalse;
        }
        if(callRuby(rb_obj_is_kind_of, other, _connectionClass) != Qtrue)
        {
            throw RubyException(rb_eTypeError, "argument must be a connection");
        }
        Ice::ConnectionPtr* p1 = reinterpret_cast<Ice::ConnectionPtr*>(DATA_PTR(self));
        Ice::ConnectionPtr* p2 = reinterpret_cast<Ice::ConnectionPtr*>(DATA_PTR(other));
        return *p1 == *p2 ? Qtrue : Qfalse;
    }
    ICE_RUBY_CATCH
    return Qnil;
}

// **********************************************************************
// ConnectionInfo
// **********************************************************************

extern "C"
void
IceRuby_ConnectionInfo_free(Ice::ConnectionInfoPtr* p)
{
    assert(p);
    delete p;
}

VALUE
IceRuby::createConnectionInfo(const Ice::ConnectionInfoPtr& p)
{
    VALUE info;
    if(Ice::WSConnectionInfoPtr::dynamicCast(p))
    {
        info = Data_Wrap_Struct(_wsConnectionInfoClass, 0, IceRuby_ConnectionInfo_free, new Ice::ConnectionInfoPtr(p));

        Ice::WSConnectionInfoPtr ws = Ice::WSConnectionInfoPtr::dynamicCast(p);
        rb_ivar_set(info, rb_intern("@localAddress"), createString(ws->localAddress));
        rb_ivar_set(info, rb_intern("@localPort"), INT2FIX(ws->localPort));
        rb_ivar_set(info, rb_intern("@remoteAddress"), createString(ws->remoteAddress));
        rb_ivar_set(info, rb_intern("@remotePort"), INT2FIX(ws->remotePort));

        volatile VALUE result = callRuby(rb_hash_new);
        for(Ice::HeaderDict::const_iterator q = ws->headers.begin(); q != ws->headers.end(); ++q)
        {
            volatile VALUE key = createString(q->first);
            volatile VALUE value = createString(q->second);
            callRuby(rb_hash_aset, result, key, value);
        }
        rb_ivar_set(info, rb_intern("@headers"), result);
    }
    else if(Ice::TCPConnectionInfoPtr::dynamicCast(p))
    {
        info = Data_Wrap_Struct(_tcpConnectionInfoClass, 0, IceRuby_ConnectionInfo_free, new Ice::ConnectionInfoPtr(p));

        Ice::TCPConnectionInfoPtr tcp = Ice::TCPConnectionInfoPtr::dynamicCast(p);
        rb_ivar_set(info, rb_intern("@localAddress"), createString(tcp->localAddress));
        rb_ivar_set(info, rb_intern("@localPort"), INT2FIX(tcp->localPort));
        rb_ivar_set(info, rb_intern("@remoteAddress"), createString(tcp->remoteAddress));
        rb_ivar_set(info, rb_intern("@remotePort"), INT2FIX(tcp->remotePort));
    }
    else if(Ice::UDPConnectionInfoPtr::dynamicCast(p))
    {
        info = Data_Wrap_Struct(_udpConnectionInfoClass, 0, IceRuby_ConnectionInfo_free, new Ice::ConnectionInfoPtr(p));

        Ice::UDPConnectionInfoPtr udp = Ice::UDPConnectionInfoPtr::dynamicCast(p);
        rb_ivar_set(info, rb_intern("@localAddress"), createString(udp->localAddress));
        rb_ivar_set(info, rb_intern("@localPort"), INT2FIX(udp->localPort));
        rb_ivar_set(info, rb_intern("@remoteAddress"), createString(udp->remoteAddress));
        rb_ivar_set(info, rb_intern("@remotePort"), INT2FIX(udp->remotePort));
        rb_ivar_set(info, rb_intern("@mcastAddress"), createString(udp->mcastAddress));
        rb_ivar_set(info, rb_intern("@mcastPort"), INT2FIX(udp->mcastPort));
    }
    else if(IceSSL::WSSConnectionInfoPtr::dynamicCast(p))
    {
        info = Data_Wrap_Struct(_wssConnectionInfoClass, 0, IceRuby_ConnectionInfo_free, new Ice::ConnectionInfoPtr(p));

        IceSSL::WSSConnectionInfoPtr wss = IceSSL::WSSConnectionInfoPtr::dynamicCast(p);
        rb_ivar_set(info, rb_intern("@localAddress"), createString(wss->localAddress));
        rb_ivar_set(info, rb_intern("@localPort"), INT2FIX(wss->localPort));
        rb_ivar_set(info, rb_intern("@remoteAddress"), createString(wss->remoteAddress));
        rb_ivar_set(info, rb_intern("@remotePort"), INT2FIX(wss->remotePort));
        rb_ivar_set(info, rb_intern("@cipher"), createString(wss->cipher));
        rb_ivar_set(info, rb_intern("@certs"), stringSeqToArray(wss->certs));
        rb_ivar_set(info, rb_intern("@verified"), wss->verified ? Qtrue : Qfalse);

        volatile VALUE result = callRuby(rb_hash_new);
        for(Ice::HeaderDict::const_iterator q = wss->headers.begin(); q != wss->headers.end(); ++q)
        {
            volatile VALUE key = createString(q->first);
            volatile VALUE value = createString(q->second);
            callRuby(rb_hash_aset, result, key, value);
        }
        rb_ivar_set(info, rb_intern("@headers"), result);
    }
    else if(IceSSL::ConnectionInfoPtr::dynamicCast(p))
    {
        info = Data_Wrap_Struct(_sslConnectionInfoClass, 0, IceRuby_ConnectionInfo_free, new Ice::ConnectionInfoPtr(p));

        IceSSL::ConnectionInfoPtr ssl = IceSSL::ConnectionInfoPtr::dynamicCast(p);
        rb_ivar_set(info, rb_intern("@localAddress"), createString(ssl->localAddress));
        rb_ivar_set(info, rb_intern("@localPort"), INT2FIX(ssl->localPort));
        rb_ivar_set(info, rb_intern("@remoteAddress"), createString(ssl->remoteAddress));
        rb_ivar_set(info, rb_intern("@remotePort"), INT2FIX(ssl->remotePort));
        rb_ivar_set(info, rb_intern("@cipher"), createString(ssl->cipher));
        rb_ivar_set(info, rb_intern("@certs"), stringSeqToArray(ssl->certs));
        rb_ivar_set(info, rb_intern("@verified"), ssl->verified ? Qtrue : Qfalse);
    }
    else if(Ice::IPConnectionInfoPtr::dynamicCast(p))
    {
        info = Data_Wrap_Struct(_ipConnectionInfoClass, 0, IceRuby_ConnectionInfo_free, new Ice::ConnectionInfoPtr(p));

        Ice::IPConnectionInfoPtr ip = Ice::IPConnectionInfoPtr::dynamicCast(p);
        rb_ivar_set(info, rb_intern("@localAddress"), createString(ip->localAddress));
        rb_ivar_set(info, rb_intern("@localPort"), INT2FIX(ip->localPort));
        rb_ivar_set(info, rb_intern("@remoteAddress"), createString(ip->remoteAddress));
        rb_ivar_set(info, rb_intern("@remotePort"), INT2FIX(ip->remotePort));
    }
    else
    {
        info = Data_Wrap_Struct(_connectionInfoClass, 0, IceRuby_ConnectionInfo_free, new Ice::ConnectionInfoPtr(p));
    }
    rb_ivar_set(info, rb_intern("@incoming"), p->incoming ? Qtrue : Qfalse);
    rb_ivar_set(info, rb_intern("@adapterName"), createString(p->adapterName));
    rb_ivar_set(info, rb_intern("@rcvSize"), INT2FIX(p->rcvSize));
    rb_ivar_set(info, rb_intern("@sndSize"), INT2FIX(p->sndSize));
    return info;
}

void
IceRuby::initConnection(VALUE iceModule)
{
    //
    // Connection.
    //
    _connectionClass = rb_define_class_under(iceModule, "ConnectionI", rb_cObject);

    //
    // Instance methods.
    //
    rb_define_method(_connectionClass, "close", CAST_METHOD(IceRuby_Connection_close), 1);
    rb_define_method(_connectionClass, "flushBatchRequests", CAST_METHOD(IceRuby_Connection_flushBatchRequests), 0);
    rb_define_method(_connectionClass, "setACM", CAST_METHOD(IceRuby_Connection_setACM), 3);
    rb_define_method(_connectionClass, "getACM", CAST_METHOD(IceRuby_Connection_getACM), 0);
    rb_define_method(_connectionClass, "type", CAST_METHOD(IceRuby_Connection_type), 0);
    rb_define_method(_connectionClass, "timeout", CAST_METHOD(IceRuby_Connection_timeout), 0);
    rb_define_method(_connectionClass, "getInfo", CAST_METHOD(IceRuby_Connection_getInfo), 0);
    rb_define_method(_connectionClass, "getEndpoint", CAST_METHOD(IceRuby_Connection_getEndpoint), 0);
    rb_define_method(_connectionClass, "setBufferSize", CAST_METHOD(IceRuby_Connection_setBufferSize), 2);
    rb_define_method(_connectionClass, "toString", CAST_METHOD(IceRuby_Connection_toString), 0);
    rb_define_method(_connectionClass, "to_s", CAST_METHOD(IceRuby_Connection_toString), 0);
    rb_define_method(_connectionClass, "inspect", CAST_METHOD(IceRuby_Connection_toString), 0);
    rb_define_method(_connectionClass, "==", CAST_METHOD(IceRuby_Connection_equals), 1);
    rb_define_method(_connectionClass, "eql?", CAST_METHOD(IceRuby_Connection_equals), 1);

    //
    // ConnectionInfo.
    //
    _connectionInfoClass = rb_define_class_under(iceModule, "ConnectionInfo", rb_cObject);

    //
    // Instance members.
    //
    rb_define_attr(_connectionInfoClass, "incoming", 1, 0);
    rb_define_attr(_connectionInfoClass, "adapterName", 1, 0);
    rb_define_attr(_connectionInfoClass, "rcvSize", 1, 0);
    rb_define_attr(_connectionInfoClass, "sndSize", 1, 0);

    //
    // IPConnectionInfo
    //
    _ipConnectionInfoClass = rb_define_class_under(iceModule, "IPConnectionInfo", _connectionInfoClass);

    //
    // Instance members.
    //
    rb_define_attr(_ipConnectionInfoClass, "localAddress", 1, 0);
    rb_define_attr(_ipConnectionInfoClass, "localPort", 1, 0);
    rb_define_attr(_ipConnectionInfoClass, "remoteAddress", 1, 0);
    rb_define_attr(_ipConnectionInfoClass, "remotePort", 1, 0);

    //
    // TCPConnectionInfo
    //
    _tcpConnectionInfoClass = rb_define_class_under(iceModule, "TCPConnectionInfo", _ipConnectionInfoClass);

    //
    // UDPConnectionInfo
    //
    _udpConnectionInfoClass = rb_define_class_under(iceModule, "UDPConnectionInfo", _ipConnectionInfoClass);

    //
    // Instance members.
    //
    rb_define_attr(_udpConnectionInfoClass, "mcastAddress", 1, 0);
    rb_define_attr(_udpConnectionInfoClass, "mcastPort", 1, 0);

    //
    // WSConnectionInfo
    //
    _wsConnectionInfoClass = rb_define_class_under(iceModule, "WSConnectionInfo", _ipConnectionInfoClass);

    //
    // Instance members.
    //
    rb_define_attr(_wsConnectionInfoClass, "headers", 1, 0);

    //
    // SSLConnectionInfo
    //
    _sslConnectionInfoClass = rb_define_class_under(iceModule, "SSLConnectionInfo", _ipConnectionInfoClass);

    //
    // Instance members.
    //
    rb_define_attr(_wsConnectionInfoClass, "cipher", 1, 0);
    rb_define_attr(_wsConnectionInfoClass, "certs", 1, 0);
    rb_define_attr(_wsConnectionInfoClass, "verified", 1, 0);

    //
    // WSSConnectionInfo
    //
    _wssConnectionInfoClass = rb_define_class_under(iceModule, "WSSConnectionInfo", _sslConnectionInfoClass);

    //
    // Instance members.
    //
    rb_define_attr(_wssConnectionInfoClass, "headers", 1, 0);
}
