// **********************************************************************
//
// Copyright (c) 2003-2017 ZeroC, Inc. All rights reserved.
//
// This copy of Ice is licensed to you under the terms described in the
// ICE_LICENSE file included in this distribution.
//
// **********************************************************************

package IceInternal;

final class WSConnector implements Connector
{
    @Override
    public Transceiver connect()
    {
        return new WSTransceiver(_instance, _delegate.connect(), _host, _port, _resource);
    }

    @Override
    public short type()
    {
        return _delegate.type();
    }

    @Override
    public String toString()
    {
        return _delegate.toString();
    }

    @Override
    public int hashCode()
    {
        return _delegate.hashCode();
    }

    WSConnector(ProtocolInstance instance, Connector del, String host, int port, String resource)
    {
        _instance = instance;
        _delegate = del;
        _host = host;
        _port = port;
        _resource = resource;
    }

    @Override
    public boolean equals(java.lang.Object obj)
    {
        if(!(obj instanceof WSConnector))
        {
            return false;
        }

        if(this == obj)
        {
            return true;
        }

        WSConnector p = (WSConnector)obj;
        if(!_delegate.equals(p._delegate))
        {
            return false;
        }

        if(!_resource.equals(p._resource))
        {
            return false;
        }

        return true;
    }

    private ProtocolInstance _instance;
    private Connector _delegate;
    private String _host;
    private int _port;
    private String _resource;
}
