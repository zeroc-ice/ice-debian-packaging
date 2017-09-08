// **********************************************************************
//
// Copyright (c) 2003-2017 ZeroC, Inc. All rights reserved.
//
// This copy of Ice is licensed to you under the terms described in the
// ICE_LICENSE file included in this distribution.
//
// **********************************************************************

package IceDiscovery;

public class PluginI implements Ice.Plugin
{
    public
    PluginI(Ice.Communicator communicator)
    {
        _communicator = communicator;
    }

    @Override
    public void
    initialize()
    {
        Ice.Properties properties = _communicator.getProperties();

        boolean ipv4 = properties.getPropertyAsIntWithDefault("Ice.IPv4", 1) > 0;
        boolean preferIPv6 = properties.getPropertyAsInt("Ice.PreferIPv6Address") > 0;
        String address;
        if(ipv4 && !preferIPv6)
        {
            address = properties.getPropertyWithDefault("IceDiscovery.Address", "239.255.0.1");
        }
        else
        {
            address = properties.getPropertyWithDefault("IceDiscovery.Address", "ff15::1");
        }
        int port = properties.getPropertyAsIntWithDefault("IceDiscovery.Port", 4061);
        String intf = properties.getProperty("IceDiscovery.Interface");

        if(properties.getProperty("IceDiscovery.Multicast.Endpoints").isEmpty())
        {
            StringBuilder s = new StringBuilder();
            s.append("udp -h \"").append(address).append("\" -p ").append(port);
            if(!intf.isEmpty())
            {
                s.append(" --interface \"").append(intf).append("\"");
            }
            properties.setProperty("IceDiscovery.Multicast.Endpoints", s.toString());
        }
        if(properties.getProperty("IceDiscovery.Reply.Endpoints").isEmpty())
        {
            StringBuilder s = new StringBuilder();
            s.append("udp");
            if(!intf.isEmpty())
            {
                s.append(" -h \"").append(intf).append("\"");
            }
            properties.setProperty("IceDiscovery.Reply.Endpoints", s.toString());
        }
        if(properties.getProperty("IceDiscovery.Locator.Endpoints").isEmpty())
        {
            properties.setProperty("IceDiscovery.Locator.AdapterId", java.util.UUID.randomUUID().toString());
        }

        _multicastAdapter = _communicator.createObjectAdapter("IceDiscovery.Multicast");
        _replyAdapter = _communicator.createObjectAdapter("IceDiscovery.Reply");
        _locatorAdapter = _communicator.createObjectAdapter("IceDiscovery.Locator");

        //
        // Setup locatory registry.
        //
        LocatorRegistryI locatorRegistry = new LocatorRegistryI(_communicator);
        Ice.LocatorRegistryPrx locatorRegistryPrx = Ice.LocatorRegistryPrxHelper.uncheckedCast(
            _locatorAdapter.addWithUUID(locatorRegistry));

        String lookupEndpoints = properties.getProperty("IceDiscovery.Lookup");
        if(lookupEndpoints.isEmpty())
        {
            StringBuilder s = new StringBuilder();
            s.append("udp -h \"").append(address).append("\" -p ").append(port);
            if(!intf.isEmpty())
            {
                s.append(" --interface \"").append(intf).append("\"");
            }
            lookupEndpoints = s.toString();
        }

        Ice.ObjectPrx lookupPrx = _communicator.stringToProxy("IceDiscovery/Lookup -d:" + lookupEndpoints);
        // No collocation optimization for the multicast proxy!
        lookupPrx = lookupPrx.ice_collocationOptimized(false).ice_router(null);
        try
        {
            lookupPrx.ice_getConnection();
        }
        catch(Ice.LocalException ex)
        {
            StringBuilder b = new StringBuilder();
            b.append("IceDiscovery is unable to establish a multicast connection:\n");
            b.append("proxy = ");
            b.append(lookupPrx.toString());
            b.append('\n');
            b.append(ex.toString());
            throw new Ice.PluginInitializationException(b.toString());
        }

        //
        // Add lookup and lookup reply Ice objects
        //
        LookupI lookup = new LookupI(locatorRegistry, LookupPrxHelper.uncheckedCast(lookupPrx), properties);
        _multicastAdapter.add(lookup, _communicator.stringToIdentity("IceDiscovery/Lookup"));

        _replyAdapter.addDefaultServant(new LookupReplyI(lookup), "");
        final Ice.Identity id = new Ice.Identity("dummy", "");
        lookup.setLookupReply(LookupReplyPrxHelper.uncheckedCast(_replyAdapter.createProxy(id).ice_datagram()));

        //
        // Setup locator on the communicator.
        //
        Ice.ObjectPrx locator = _locatorAdapter.addWithUUID(new LocatorI(lookup, locatorRegistryPrx));
        _defaultLocator = _communicator.getDefaultLocator();
        _locator = Ice.LocatorPrxHelper.uncheckedCast(locator);
        _communicator.setDefaultLocator(_locator);

        _multicastAdapter.activate();
        _replyAdapter.activate();
        _locatorAdapter.activate();
    }

    @Override
    public void
    destroy()
    {
        if(_multicastAdapter != null)
        {
            _multicastAdapter.destroy();
        }
        if(_replyAdapter != null)
        {
            _replyAdapter.destroy();
        }
        if(_locatorAdapter != null)
        {
            _locatorAdapter.destroy();
        }
        if(_communicator.getDefaultLocator().equals(_locator))
        {
            // Restore original default locator proxy, if the user didn't change it in the meantime
            _communicator.setDefaultLocator(_defaultLocator);
        }
    }

    private Ice.Communicator _communicator;
    private Ice.ObjectAdapter _multicastAdapter;
    private Ice.ObjectAdapter _replyAdapter;
    private Ice.ObjectAdapter _locatorAdapter;
    private Ice.LocatorPrx _locator;
    private Ice.LocatorPrx _defaultLocator;
}
