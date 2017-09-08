// **********************************************************************
//
// Copyright (c) 2003-2017 ZeroC, Inc. All rights reserved.
//
// This copy of Ice is licensed to you under the terms described in the
// ICE_LICENSE file included in this distribution.
//
// **********************************************************************

package IceInternal;

final class OpaqueEndpointI extends EndpointI
{
    public OpaqueEndpointI(java.util.ArrayList<String> args)
    {
        _type = -1;
        _rawEncoding = Ice.Util.Encoding_1_0;
        _rawBytes = new byte[0];

        initWithOptions(args);

        if(_type < 0)
        {
            throw new Ice.EndpointParseException("no -t option in endpoint " + toString());
        }
        if(_rawBytes.length == 0)
        {
            throw new Ice.EndpointParseException("no -v option in endpoint " + toString());
        }

        calcHashValue();
    }

    public OpaqueEndpointI(short type, BasicStream s)
    {
        _type = type;
        _rawEncoding = s.getReadEncoding();
        int sz = s.getReadEncapsSize();
        _rawBytes = s.readBlob(sz);

        calcHashValue();
    }

    //
    // Marshal the endpoint
    //
    @Override
    public void streamWrite(BasicStream s)
    {
        s.startWriteEncaps(_rawEncoding, Ice.FormatType.DefaultFormat);
        s.writeBlob(_rawBytes);
        s.endWriteEncaps();
    }

    //
    // Return the endpoint information.
    //
    @Override
    public Ice.EndpointInfo getInfo()
    {
        return new Ice.OpaqueEndpointInfo(-1, false, _rawEncoding, _rawBytes)
            {
                @Override
                public short type()
                {
                    return _type;
                }

                @Override
                public boolean datagram()
                {
                    return false;
                }

                @Override
                public boolean secure()
                {
                    return false;
                }
        };
    }

    //
    // Return the endpoint type
    //
    @Override
    public short type()
    {
        return _type;
    }

    //
    // Return the protocol name
    //
    @Override
    public String protocol()
    {
        return "opaque";
    }

    //
    // Return the timeout for the endpoint in milliseconds. 0 means
    // non-blocking, -1 means no timeout.
    //
    @Override
    public int timeout()
    {
        return -1;
    }

    //
    // Return a new endpoint with a different timeout value, provided
    // that timeouts are supported by the endpoint. Otherwise the same
    // endpoint is returned.
    //
    @Override
    public EndpointI timeout(int t)
    {
        return this;
    }

    @Override
    public String connectionId()
    {
        return "";
    }

    //
    // Return a new endpoint with a different connection id.
    //
    @Override
    public EndpointI connectionId(String connectionId)
    {
        return this;
    }

    //
    // Return true if the endpoints support bzip2 compress, or false
    // otherwise.
    //
    @Override
    public boolean compress()
    {
        return false;
    }

    //
    // Return a new endpoint with a different compression value,
    // provided that compression is supported by the
    // endpoint. Otherwise the same endpoint is returned.
    //
    @Override
    public EndpointI compress(boolean compress)
    {
        return this;
    }

    //
    // Return true if the endpoint is datagram-based.
    //
    @Override
    public boolean datagram()
    {
        return false;
    }

    //
    // Return true if the endpoint is secure.
    //
    @Override
    public boolean secure()
    {
        return false;
    }

    //
    // Return a server side transceiver for this endpoint, or null if a
    // transceiver can only be created by an acceptor.d.
    //
    @Override
    public Transceiver transceiver()
    {
        return null;
    }

    //
    // Return connectors for this endpoint, or empty list if no connector
    // is available.
    //
    @Override
    public void connectors_async(Ice.EndpointSelectionType selType, EndpointI_connectors callback)
    {
        callback.connectors(new java.util.ArrayList<Connector>());
    }

    //
    // Return an acceptor for this endpoint, or null if no acceptors
    // is available.
    //
    @Override
    public Acceptor acceptor(String adapterName)
    {
        return null;
    }

    //
    // Expand endpoint out in to separate endpoints for each local
    // host if listening on INADDR_ANY on server side or if no host
    // was specified on client side.
    //
    @Override
    public java.util.List<EndpointI> expand()
    {
        java.util.List<EndpointI> endps = new java.util.ArrayList<EndpointI>();
        endps.add(this);
        return endps;
    }

    //
    // Check whether the endpoint is equivalent to another one.
    //
    @Override
    public boolean equivalent(EndpointI endpoint)
    {
        return false;
    }

    @Override
    public int hashCode()
    {
        return _hashCode;
    }

    @Override
    public String options()
    {
        String s = "";
        if(_type > -1)
        {
            s += " -t " + _type;
        }
        s += " -e " + Ice.Util.encodingVersionToString(_rawEncoding);
        if(_rawBytes.length > 0)
        {
            s += " -v " + IceUtilInternal.Base64.encode(_rawBytes);
        }
        return s;
    }

    //
    // Compare endpoints for sorting purposes
    //
    @Override
    public int compareTo(EndpointI obj) // From java.lang.Comparable
    {
        if(!(obj instanceof OpaqueEndpointI))
        {
            return type() < obj.type() ? -1 : 1;
        }

        OpaqueEndpointI p = (OpaqueEndpointI)obj;
        if(this == p)
        {
            return 0;
        }

        if(_type < p._type)
        {
            return -1;
        }
        else if(p._type < _type)
        {
            return 1;
        }

        if(_rawEncoding.major < p._rawEncoding.major)
        {
            return -1;
        }
        else if(p._rawEncoding.major < _rawEncoding.major)
        {
            return 1;
        }

        if(_rawEncoding.minor < p._rawEncoding.minor)
        {
            return -1;
        }
        else if(p._rawEncoding.minor < _rawEncoding.minor)
        {
            return 1;
        }

        if(_rawBytes.length < p._rawBytes.length)
        {
            return -1;
        }
        else if(p._rawBytes.length < _rawBytes.length)
        {
            return 1;
        }
        for(int i = 0; i < _rawBytes.length; i++)
        {
            if(_rawBytes[i] < p._rawBytes[i])
            {
                return -1;
            }
            else if(p._rawBytes[i] < _rawBytes[i])
            {
                return 1;
            }
        }

        return 0;
    }

    @Override
    protected boolean checkOption(String option, String argument, String endpoint)
    {
        switch(option.charAt(1))
        {
        case 't':
        {
            if(_type > -1)
            {
                throw new Ice.EndpointParseException("multiple -t options in endpoint " + endpoint);
            }
            if(argument == null)
            {
                throw new Ice.EndpointParseException("no argument provided for -t option in endpoint " + endpoint);
            }

            int t;
            try
            {
                t = Integer.parseInt(argument);
            }
            catch(NumberFormatException ex)
            {
                throw new Ice.EndpointParseException("invalid type value `" + argument + "' in endpoint " + endpoint);
            }

            if(t < 0 || t > 65535)
            {
                throw new Ice.EndpointParseException("type value `" + argument + "' out of range in endpoint " +
                                                     endpoint);
            }

            _type = (short)t;
            return true;
        }

        case 'v':
        {
            if(_rawBytes.length > 0)
            {
                throw new Ice.EndpointParseException("multiple -v options in endpoint " + endpoint);
            }
            if(argument == null)
            {
                throw new Ice.EndpointParseException("no argument provided for -v option in endpoint " + endpoint);
            }

            for(int j = 0; j < argument.length(); ++j)
            {
                if(!IceUtilInternal.Base64.isBase64(argument.charAt(j)))
                {
                    throw new Ice.EndpointParseException("invalid base64 character `" + argument.charAt(j) +
                                                         "' (ordinal " + ((int)argument.charAt(j)) +
                                                         ") in endpoint " + endpoint);
                }
            }
            _rawBytes = IceUtilInternal.Base64.decode(argument);
            return true;
        }

        case 'e':
        {
            if(argument == null)
            {
                throw new Ice.EndpointParseException("no argument provided for -e option in endpoint " + endpoint);
            }

            try
            {
                _rawEncoding = Ice.Util.stringToEncodingVersion(argument);
            }
            catch(Ice.VersionParseException e)
            {
                throw new Ice.EndpointParseException("invalid encoding version `" + argument +
                                                     "' in endpoint " + endpoint + ":\n" + e.str);
            }
            return true;
        }

        default:
        {
            return false;
        }
        }
    }

    private void calcHashValue()
    {
        int h = 5381;
        h = IceInternal.HashUtil.hashAdd(h, _type);
        h = IceInternal.HashUtil.hashAdd(h, _rawEncoding);
        h = IceInternal.HashUtil.hashAdd(h, _rawBytes);
        _hashCode = h;
    }

    private short _type;
    private Ice.EncodingVersion _rawEncoding;
    private byte[] _rawBytes;
    private int _hashCode;
}
