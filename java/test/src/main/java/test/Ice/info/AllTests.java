// **********************************************************************
//
// Copyright (c) 2003-2017 ZeroC, Inc. All rights reserved.
//
// This copy of Ice is licensed to you under the terms described in the
// ICE_LICENSE file included in this distribution.
//
// **********************************************************************

package test.Ice.info;

import java.io.PrintWriter;

import test.Ice.info.Test.TestIntfPrx;
import test.Ice.info.Test.TestIntfPrxHelper;

public class AllTests
{
    private static void
    test(boolean b)
    {
        if(!b)
        {
            throw new RuntimeException();
        }
    }

    public static void
    allTests(Ice.Communicator communicator, PrintWriter out)
    {
        out.print("testing proxy endpoint information... ");
        out.flush();
        {
            Ice.ObjectPrx p1 = communicator.stringToProxy(
                                "test -t:default -h tcphost -p 10000 -t 1200 -z --sourceAddress 10.10.10.10:" +
                                "udp -h udphost -p 10001 --interface eth0 --ttl 5 --sourceAddress 10.10.10.10:" +
                                "opaque -e 1.8 -t 100 -v ABCD");

            Ice.Endpoint[] endps = p1.ice_getEndpoints();


            Ice.IPEndpointInfo ipEndpoint = (Ice.IPEndpointInfo)endps[0].getInfo();
            test(ipEndpoint.host.equals("tcphost"));
            test(ipEndpoint.port == 10000);
            test(ipEndpoint.timeout == 1200);
            test(ipEndpoint.sourceAddress.equals("10.10.10.10"));
            test(ipEndpoint.compress);
            test(!ipEndpoint.datagram());
            test(ipEndpoint.type() == Ice.TCPEndpointType.value && !ipEndpoint.secure() ||
                 ipEndpoint.type() == IceSSL.EndpointType.value && ipEndpoint.secure() ||
                 ipEndpoint.type() == Ice.WSEndpointType.value && !ipEndpoint.secure() ||
                 ipEndpoint.type() == Ice.WSSEndpointType.value && ipEndpoint.secure());
            test(ipEndpoint.type() == Ice.TCPEndpointType.value && ipEndpoint instanceof Ice.TCPEndpointInfo ||
                 ipEndpoint.type() == IceSSL.EndpointType.value && ipEndpoint instanceof IceSSL.EndpointInfo ||
                 ipEndpoint.type() == Ice.WSEndpointType.value && ipEndpoint instanceof Ice.WSEndpointInfo ||
                 ipEndpoint.type() == Ice.WSSEndpointType.value && ipEndpoint instanceof IceSSL.WSSEndpointInfo);

            Ice.UDPEndpointInfo udpEndpoint = (Ice.UDPEndpointInfo)endps[1].getInfo();
            test(udpEndpoint.host.equals("udphost"));
            test(udpEndpoint.port == 10001);
            test(udpEndpoint.mcastInterface.equals("eth0"));
            test(udpEndpoint.mcastTtl == 5);
            test(udpEndpoint.sourceAddress.equals("10.10.10.10"));
            test(udpEndpoint.timeout == -1);
            test(!udpEndpoint.compress);
            test(!udpEndpoint.secure());
            test(udpEndpoint.datagram());
            test(udpEndpoint.type() == Ice.UDPEndpointType.value);

            Ice.OpaqueEndpointInfo opaqueEndpoint = (Ice.OpaqueEndpointInfo)endps[2].getInfo();
            test(opaqueEndpoint.rawEncoding.equals(new Ice.EncodingVersion((byte)1, (byte)8)));
        }
        out.println("ok");

        String defaultHost = communicator.getProperties().getProperty("Ice.Default.Host");
        out.print("test object adapter endpoint information... ");
        out.flush();
        {
            communicator.getProperties().setProperty("TestAdapter.Endpoints", "default -t 15000:udp");
            Ice.ObjectAdapter adapter = communicator.createObjectAdapter("TestAdapter");

            Ice.Endpoint[] endpoints = adapter.getEndpoints();
            test(endpoints.length == 2);
            Ice.Endpoint[] publishedEndpoints = adapter.getPublishedEndpoints();
            test(java.util.Arrays.equals(endpoints, publishedEndpoints));

            Ice.IPEndpointInfo ipEndpoint = (Ice.IPEndpointInfo)endpoints[0].getInfo();
            test(ipEndpoint.type() == Ice.TCPEndpointType.value || ipEndpoint.type() == IceSSL.EndpointType.value ||
                 ipEndpoint.type() == Ice.WSEndpointType.value || ipEndpoint.type() == Ice.WSSEndpointType.value);
            test(ipEndpoint.host.equals(defaultHost));
            test(ipEndpoint.port > 0);
            test(ipEndpoint.timeout == 15000);

            Ice.UDPEndpointInfo udpEndpoint = (Ice.UDPEndpointInfo)endpoints[1].getInfo();
            test(udpEndpoint.host.equals(defaultHost));
            test(udpEndpoint.datagram());
            test(udpEndpoint.port > 0);

            adapter.destroy();

            communicator.getProperties().setProperty("TestAdapter.Endpoints", "default -h * -p 12020");
            communicator.getProperties().setProperty("TestAdapter.PublishedEndpoints", "default -h 127.0.0.1 -p 12020");
            adapter = communicator.createObjectAdapter("TestAdapter");

            endpoints = adapter.getEndpoints();
            test(endpoints.length >= 1);
            publishedEndpoints = adapter.getPublishedEndpoints();
            test(publishedEndpoints.length == 1);

            for(Ice.Endpoint endpoint : endpoints)
            {
                ipEndpoint = (Ice.IPEndpointInfo)endpoint.getInfo();
                test(ipEndpoint.port == 12020);
            }

            ipEndpoint = (Ice.IPEndpointInfo)publishedEndpoints[0].getInfo();
            test(ipEndpoint.host.equals("127.0.0.1"));
            test(ipEndpoint.port == 12020);

            adapter.destroy();
        }
        out.println("ok");

        Ice.ObjectPrx base = communicator.stringToProxy("test:default -p 12010:udp -p 12010");
        TestIntfPrx testIntf = TestIntfPrxHelper.checkedCast(base);

        out.print("test connection endpoint information... ");
        out.flush();
        {
            Ice.EndpointInfo info = base.ice_getConnection().getEndpoint().getInfo();
            Ice.IPEndpointInfo ipinfo = (Ice.IPEndpointInfo)info;
            test(ipinfo.port == 12010);
            test(!ipinfo.compress);
            test(ipinfo.host.equals(defaultHost));

            java.util.Map<String, String> ctx = testIntf.getEndpointInfoAsContext();
            test(ctx.get("host").equals(ipinfo.host));
            test(ctx.get("compress").equals("false"));
            int port = Integer.parseInt(ctx.get("port"));
            test(port > 0);

            info = base.ice_datagram().ice_getConnection().getEndpoint().getInfo();
            Ice.UDPEndpointInfo udp = (Ice.UDPEndpointInfo)info;
            test(udp.port == 12010);
            test(udp.host.equals(defaultHost));
        }
        out.println("ok");

        out.print("testing connection information... ");
        out.flush();
        {
            Ice.Connection connection = base.ice_getConnection();
            connection.setBufferSize(1024, 2048);

            Ice.IPConnectionInfo info = (Ice.IPConnectionInfo)connection.getInfo();
            test(!info.incoming);
            test(info.adapterName.length() == 0);
            test(info.localPort > 0);
            test(info.remotePort == 12010);
            if(defaultHost.equals("127.0.0.1"))
            {
                test(info.remoteAddress.equals(defaultHost));
                test(info.localAddress.equals(defaultHost));
            }
            test(info.rcvSize >= 1024);
            test(info.sndSize >= 2048);

            java.util.Map<String, String> ctx = testIntf.getConnectionInfoAsContext();
            test(ctx.get("incoming").equals("true"));
            test(ctx.get("adapterName").equals("TestAdapter"));
            test(ctx.get("remoteAddress").equals(info.localAddress));
            test(ctx.get("localAddress").equals(info.remoteAddress));
            test(ctx.get("remotePort").equals(Integer.toString(info.localPort)));
            test(ctx.get("localPort").equals(Integer.toString(info.remotePort)));

            if(base.ice_getConnection().type().equals("ws") || base.ice_getConnection().type().equals("wss"))
            {
                java.util.Map<String, String> headers;
                if(info instanceof Ice.WSConnectionInfo)
                {
                    headers = ((Ice.WSConnectionInfo)info).headers;
                }
                else
                {
                    headers = ((IceSSL.WSSConnectionInfo)info).headers;
                }
                test(headers.get("Upgrade").equals("websocket"));
                test(headers.get("Connection").equals("Upgrade"));
                test(headers.get("Sec-WebSocket-Protocol").equals("ice.zeroc.com"));
                test(headers.get("Sec-WebSocket-Accept") != null);

                test(ctx.get("ws.Upgrade").equals("websocket"));
                test(ctx.get("ws.Connection").equals("Upgrade"));
                test(ctx.get("ws.Sec-WebSocket-Protocol").equals("ice.zeroc.com"));
                test(ctx.get("ws.Sec-WebSocket-Version").equals("13"));
                test(ctx.get("ws.Sec-WebSocket-Key") != null);
            }

            connection = base.ice_datagram().ice_getConnection();
            connection.setBufferSize(2048, 1024);

            info = (Ice.IPConnectionInfo)connection.getInfo();
            test(!info.incoming);
            test(info.adapterName.length() == 0);
            test(info.localPort > 0);
            test(info.remotePort == 12010);
            if(defaultHost.equals("127.0.0.1"))
            {
                test(info.remoteAddress.equals(defaultHost));
                test(info.localAddress.equals(defaultHost));
            }
            test(info.rcvSize >= 2048);
            test(info.sndSize >= 1024);
        }
        out.println("ok");

        testIntf.shutdown();

        communicator.shutdown();
        communicator.waitForShutdown();
    }
}
