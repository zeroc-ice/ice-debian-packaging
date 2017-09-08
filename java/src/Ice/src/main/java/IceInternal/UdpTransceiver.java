// **********************************************************************
//
// Copyright (c) 2003-2017 ZeroC, Inc. All rights reserved.
//
// This copy of Ice is licensed to you under the terms described in the
// ICE_LICENSE file included in this distribution.
//
// **********************************************************************

package IceInternal;

final class UdpTransceiver implements Transceiver
{
    @Override
    public java.nio.channels.SelectableChannel fd()
    {
        assert(_fd != null);
        return _fd;
    }

    @Override
    public int initialize(Buffer readBuffer, Buffer writeBuffer, Ice.Holder<Boolean> moreData)
    {
        //
        // Nothing to do.
        //
        return SocketOperation.None;
    }

    @Override
    public int closing(boolean initiator, Ice.LocalException ex)
    {
        //
        // Nothing to do.
        //
        return SocketOperation.None;
    }

    @Override
    public void close()
    {
        assert(_fd != null);

        try
        {
            _fd.close();
        }
        catch(java.io.IOException ex)
        {
        }
        _fd = null;
    }

    @Override
    public EndpointI bind()
    {
        if(_addr.getAddress().isMulticastAddress())
        {
            Network.setReuseAddress(_fd, true);
            _mcastAddr = _addr;
            if(System.getProperty("os.name").startsWith("Windows") ||
               System.getProperty("java.vm.name").startsWith("OpenJDK"))
            {
                //
                // Windows does not allow binding to the mcast address itself
                // so we bind to INADDR_ANY (0.0.0.0) instead. As a result,
                // bi-directional connection won't work because the source
                // address won't be the multicast address and the client will
                // therefore reject the datagram.
                //
                int protocol =
                    _mcastAddr.getAddress().getAddress().length == 4 ? Network.EnableIPv4 : Network.EnableIPv6;
                _addr = Network.getAddressForServer("", _port, protocol, _instance.preferIPv6());
            }
            _addr = Network.doBind(_fd, _addr);
            configureMulticast(_mcastAddr, _mcastInterface, -1);

            if(_port == 0)
            {
                _mcastAddr = new java.net.InetSocketAddress(_mcastAddr.getAddress(), _addr.getPort());
            }
        }
        else
        {
            if(!System.getProperty("os.name").startsWith("Windows"))
            {
                //
                // Enable SO_REUSEADDR on Unix platforms to allow
                // re-using the socket even if it's in the TIME_WAIT
                // state. On Windows, this doesn't appear to be
                // necessary and enabling SO_REUSEADDR would actually
                // not be a good thing since it allows a second
                // process to bind to an address even it's already
                // bound by another process.
                //
                // TODO: using SO_EXCLUSIVEADDRUSE on Windows would
                // probably be better but it's only supported by recent
                // Windows versions (XP SP2, Windows Server 2003).
                //
                Network.setReuseAddress(_fd, true);
            }
            _addr = Network.doBind(_fd, _addr);
        }

        _bound = true;
        _endpoint = _endpoint.endpoint(this);
        return _endpoint;
    }

    @Override
    public int write(Buffer buf)
    {
        if(!buf.b.hasRemaining())
        {
            return SocketOperation.None;
        }

        assert(buf.b.position() == 0);
        assert(_fd != null && _state >= StateConnected);

        // The caller is supposed to check the send size before by calling checkSendSize
        assert(java.lang.Math.min(_maxPacketSize, _sndSize - _udpOverhead) >= buf.size());

        int ret = 0;
        while(true)
        {
            try
            {
                if(_state == StateConnected)
                {
                    ret = _fd.write(buf.b);
                }
                else
                {
                    if(_peerAddr == null)
                    {
                        throw new Ice.SocketException(); // No peer has sent a datagram yet.
                    }
                    ret = _fd.send(buf.b, _peerAddr);
                }
                break;
            }
            catch(java.nio.channels.AsynchronousCloseException ex)
            {
                throw new Ice.ConnectionLostException(ex);
            }
            catch(java.net.PortUnreachableException ex)
            {
                throw new Ice.ConnectionLostException(ex);
            }
            catch(java.io.InterruptedIOException ex)
            {
                continue;
            }
            catch(java.io.IOException ex)
            {
                throw new Ice.SocketException(ex);
            }
        }

        if(ret == 0)
        {
            return SocketOperation.Write;
        }

        assert(ret == buf.b.limit());
        buf.b.position(buf.b.limit());
        return SocketOperation.None;
    }

    @Override
    public int read(Buffer buf, Ice.Holder<Boolean> moreData)
    {
        if(!buf.b.hasRemaining())
        {
            return SocketOperation.None;
        }

        assert(buf.b.position() == 0);

        final int packetSize = java.lang.Math.min(_maxPacketSize, _rcvSize - _udpOverhead);
        buf.resize(packetSize, true);
        buf.b.position(0);

        int ret = 0;
        while(true)
        {
            try
            {
                java.net.SocketAddress peerAddr = _fd.receive(buf.b);
                if(peerAddr == null || buf.b.position() == 0)
                {
                    return SocketOperation.Read;
                }

                _peerAddr = (java.net.InetSocketAddress)peerAddr;
                ret = buf.b.position();
                break;
            }
            catch(java.nio.channels.AsynchronousCloseException ex)
            {
                throw new Ice.ConnectionLostException(ex);
            }
            catch(java.net.PortUnreachableException ex)
            {
                throw new Ice.ConnectionLostException(ex);
            }
            catch(java.io.InterruptedIOException ex)
            {
                continue;
            }
            catch(java.io.IOException ex)
            {
                throw new Ice.ConnectionLostException(ex);
            }
        }

        if(_state == StateNeedConnect)
        {
            //
            // If we must connect, we connect to the first peer that sends us a packet.
            //
            Network.doConnect(_fd, _peerAddr, null);
            _state = StateConnected;

            if(_instance.traceLevel() >= 1)
            {
                String s = "connected " + _instance.protocol() + " socket\n" + toString();
                _instance.logger().trace(_instance.traceCategory(), s);
            }
        }

        buf.resize(ret, true);
        buf.b.position(ret);

        return SocketOperation.None;
    }

    @Override
    public String protocol()
    {
        return _instance.protocol();
    }

    @Override
    public String toString()
    {
        if(_fd == null)
        {
            return "<closed>";
        }

        String s;
        if(_incoming && !_bound)
        {
            s = "local address = " + Network.addrToString(_addr);
        }
        else if(_state == StateNotConnected)
        {
            java.net.DatagramSocket socket = _fd.socket();
            s = "local address = " + Network.addrToString((java.net.InetSocketAddress)socket.getLocalSocketAddress());
            if(_peerAddr != null)
            {
                s += "\nremote address = " + Network.addrToString(_peerAddr);
            }
        }
        else
        {
            s = Network.fdToString(_fd);
        }

        if(_mcastAddr != null)
        {
            s += "\nmulticast address = " + Network.addrToString(_mcastAddr);
        }
        return s;
    }

    @Override
    public String toDetailedString()
    {
        StringBuilder s = new StringBuilder(toString());
        java.util.List<String> intfs =
            Network.getHostsForEndpointExpand(_addr.getAddress().getHostAddress(), _instance.protocolSupport(), true);
        if(!intfs.isEmpty())
        {
            s.append("\nlocal interfaces = ");
            s.append(IceUtilInternal.StringUtil.joinString(intfs, ", "));
        }
        return s.toString();
    }

    @Override
    public Ice.ConnectionInfo getInfo()
    {
        Ice.UDPConnectionInfo info = new Ice.UDPConnectionInfo();
        if(_fd != null)
        {
            java.net.DatagramSocket socket = _fd.socket();
            info.localAddress = socket.getLocalAddress().getHostAddress();
            info.localPort = socket.getLocalPort();
            if(_state == StateNotConnected)
            {
                if(_peerAddr != null)
                {
                    info.remoteAddress = _peerAddr.getAddress().getHostAddress();
                    info.remotePort = _peerAddr.getPort();
                }
            }
            else
            {
                if(socket.getInetAddress() != null)
                {
                    info.remoteAddress = socket.getInetAddress().getHostAddress();
                    info.remotePort = socket.getPort();
                }
            }
            if(!socket.isClosed())
            {
                info.rcvSize = Network.getRecvBufferSize(_fd);
                info.sndSize = Network.getSendBufferSize(_fd);
            }
        }
        if(_mcastAddr != null)
        {
            info.mcastAddress = _mcastAddr.getAddress().getHostAddress();
            info.mcastPort = _mcastAddr.getPort();
        }
        return info;
    }

    @Override
    public void checkSendSize(Buffer buf)
    {
        //
        // The maximum packetSize is either the maximum allowable UDP packet size, or
        // the UDP send buffer size (which ever is smaller).
        //
        final int packetSize = java.lang.Math.min(_maxPacketSize, _sndSize - _udpOverhead);
        if(packetSize < buf.size())
        {
            throw new Ice.DatagramLimitException();
        }
    }

    @Override
    public void setBufferSize(int rcvSize, int sndSize)
    {
        setBufSize(rcvSize, sndSize);
    }

    public final int effectivePort()
    {
        return _addr.getPort();
    }

    //
    // Only for use by UdpEndpoint
    //
    UdpTransceiver(ProtocolInstance instance, java.net.InetSocketAddress addr, java.net.InetSocketAddress sourceAddr,
                   String mcastInterface, int mcastTtl)
    {
        _instance = instance;
        _state = StateNeedConnect;
        _addr = addr;

        try
        {
            _fd = Network.createUdpSocket(_addr);
            setBufSize(-1, -1);
            Network.setBlock(_fd, false);
            //
            // NOTE: setting the multicast interface before performing the
            // connect is important for some OS such as macOS.
            //
            if(_addr.getAddress().isMulticastAddress())
            {
                configureMulticast(null, mcastInterface, mcastTtl);
            }
            Network.doConnect(_fd, _addr, sourceAddr);
            _state = StateConnected; // We're connected now
        }
        catch(Ice.LocalException ex)
        {
            _fd = null;
            throw ex;
        }
    }

    //
    // Only for use by UdpEndpoint
    //
    UdpTransceiver(UdpEndpointI endpoint, ProtocolInstance instance, String host, int port, String mcastInterface,
                   boolean connect)
    {
        _endpoint = endpoint;
        _instance = instance;
        _state = connect ? StateNeedConnect : StateNotConnected;
        _mcastInterface = mcastInterface;
        _incoming = true;
        _port = port;

        try
        {
            _addr = Network.getAddressForServer(host, port, instance.protocolSupport(), instance.preferIPv6());
            _fd = Network.createUdpSocket(_addr);
            setBufSize(-1, -1);
            Network.setBlock(_fd, false);
        }
        catch(Ice.LocalException ex)
        {
            _fd = null;
            throw ex;
        }
    }

    private synchronized void setBufSize(int rcvSize, int sndSize)
    {
        assert(_fd != null);

        for(int i = 0; i < 2; ++i)
        {
            boolean isSnd;
            String direction;
            String prop;
            int dfltSize;
            int sizeRequested;
            if(i == 0)
            {
                isSnd = false;
                direction = "receive";
                prop = "Ice.UDP.RcvSize";
                dfltSize = Network.getRecvBufferSize(_fd);
                sizeRequested = rcvSize;
                _rcvSize = dfltSize;
            }
            else
            {
                isSnd = true;
                direction = "send";
                prop = "Ice.UDP.SndSize";
                dfltSize = Network.getSendBufferSize(_fd);
                sizeRequested = sndSize;
                _sndSize = dfltSize;
            }

            //
            // Get property for buffer size if size not passed in.
            //
            if(sizeRequested == -1)
            {
                sizeRequested = _instance.properties().getPropertyAsIntWithDefault(prop, dfltSize);
            }
            //
            // Check for sanity.
            //
            if(sizeRequested < (_udpOverhead + IceInternal.Protocol.headerSize))
            {
                _instance.logger().warning("Invalid " + prop + " value of " + sizeRequested + " adjusted to " +
                    dfltSize);
                sizeRequested = dfltSize;
            }

            if(sizeRequested != dfltSize)
            {
                //
                // Try to set the buffer size. The kernel will silently adjust
                // the size to an acceptable value. Then read the size back to
                // get the size that was actually set.
                //
                int sizeSet;
                if(i == 0)
                {
                    Network.setRecvBufferSize(_fd, sizeRequested);
                    _rcvSize = Network.getRecvBufferSize(_fd);
                    sizeSet = _rcvSize;
                }
                else
                {
                    Network.setSendBufferSize(_fd, sizeRequested);
                    _sndSize = Network.getSendBufferSize(_fd);
                    sizeSet = _sndSize;
                }

                //
                // Warn if the size that was set is less than the requested size
                // and we have not already warned
                //
                if(sizeSet < sizeRequested)
                {
                    BufSizeWarnInfo winfo = _instance.getBufSizeWarn(Ice.UDPEndpointType.value);
                    if((isSnd && (!winfo.sndWarn || winfo.sndSize != sizeRequested)) ||
                       (!isSnd && (!winfo.rcvWarn || winfo.rcvSize != sizeRequested)))
                    {
                        _instance.logger().warning("UDP " + direction + " buffer size: requested size of "
                                                   + sizeRequested + " adjusted to " + sizeSet);

                        if(isSnd)
                        {
                            _instance.setSndBufSizeWarn(Ice.UDPEndpointType.value, sizeRequested);
                        }
                        else
                        {
                            _instance.setRcvBufSizeWarn(Ice.UDPEndpointType.value, sizeRequested);
                        }
                    }
                }
            }
        }
    }

    private void configureMulticast(java.net.InetSocketAddress group, String interfaceAddr, int ttl)
    {
        try
        {
            java.net.NetworkInterface intf = null;

            if(interfaceAddr.length() != 0)
            {
                intf = java.net.NetworkInterface.getByName(interfaceAddr);
                if(intf == null)
                {
                    try
                    {
                        intf = java.net.NetworkInterface.getByInetAddress(
                            java.net.InetAddress.getByName(interfaceAddr));
                    }
                    catch(Exception ex)
                    {
                    }
                }
            }

            if(group != null)
            {
                //
                // Join multicast group.
                //
                if(intf != null)
                {
                    _fd.join(group.getAddress(), intf);
                }
                else
                {
                    boolean join = false;
                    //
                    // If the user doesn't specify an interface, we join to the multicast group with every
                    // interface that supports multicast and has a configured address with the same protocol
                    // as the group address protocol.
                    //
                    int protocol = group.getAddress().getAddress().length == 4 ? Network.EnableIPv4 :
                                                                                 Network.EnableIPv6;

                    java.util.List<java.net.NetworkInterface> interfaces =
                                java.util.Collections.list(java.net.NetworkInterface.getNetworkInterfaces());
                    for(java.net.NetworkInterface iface : interfaces)
                    {
                        boolean hasProtocolAddress = false;
                        java.util.List<java.net.InetAddress> addresses =
                            java.util.Collections.list(iface.getInetAddresses());
                        for(java.net.InetAddress address : addresses)
                        {
                            if(address.getAddress().length == 4 && protocol == Network.EnableIPv4 ||
                               address.getAddress().length != 4 && protocol == Network.EnableIPv6)
                            {
                                hasProtocolAddress = true;
                                break;
                            }
                        }

                        if(hasProtocolAddress)
                        {
                            _fd.join(group.getAddress(), iface);
                            join = true;
                        }
                    }

                    if(!join)
                    {
                        throw new Ice.SocketException(new IllegalArgumentException(
                                            "There are no interfaces that are configured for the group protocol.\n" +
                                            "Cannot join the multicast group."));
                    }
                }
            }
            else if(intf != null)
            {
                //
                // Otherwise, set the multicast interface if specified.
                //
                _fd.setOption(java.net.StandardSocketOptions.IP_MULTICAST_IF, intf);
            }

            if(ttl != -1)
            {
                _fd.setOption(java.net.StandardSocketOptions.IP_MULTICAST_TTL, ttl);
            }
        }
        catch(Exception ex)
        {
            throw new Ice.SocketException(ex);
        }
    }

    @Override
    protected synchronized void finalize()
        throws Throwable
    {
        try
        {
            IceUtilInternal.Assert.FinalizerAssert(_fd == null);
        }
        catch(java.lang.Exception ex)
        {
        }
        finally
        {
            super.finalize();
        }
    }

    private UdpEndpointI _endpoint = null;
    private ProtocolInstance _instance;

    private int _state;
    private int _rcvSize;
    private int _sndSize;
    private java.nio.channels.DatagramChannel _fd;
    private java.net.InetSocketAddress _addr;
    private java.net.InetSocketAddress _mcastAddr = null;
    private String _mcastInterface;
    private java.net.InetSocketAddress _peerAddr = null;

    private boolean _incoming = false;
    private int _port = 0;
    private boolean _bound = false;

    //
    // The maximum IP datagram size is 65535. Subtract 20 bytes for the IP header and 8 bytes for the UDP header
    // to get the maximum payload.
    //
    private final static int _udpOverhead = 20 + 8;
    private final static int _maxPacketSize = 65535 - _udpOverhead;

    private static final int StateNeedConnect = 0;
    private static final int StateConnected = 1;
    private static final int StateNotConnected = 2;
}
