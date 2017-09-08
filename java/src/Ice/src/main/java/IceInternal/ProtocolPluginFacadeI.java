// **********************************************************************
//
// Copyright (c) 2003-2017 ZeroC, Inc. All rights reserved.
//
// This copy of Ice is licensed to you under the terms described in the
// ICE_LICENSE file included in this distribution.
//
// **********************************************************************

package IceInternal;

public class ProtocolPluginFacadeI implements ProtocolPluginFacade
{
    public ProtocolPluginFacadeI(Ice.Communicator communicator)
    {
        _communicator = communicator;
        _instance = Util.getInstance(communicator);
    }

    //
    // Get the Communicator instance with which this facade is
    // associated.
    //
    @Override
    public Ice.Communicator getCommunicator()
    {
        return _communicator;
    }

    //
    // Register an EndpointFactory.
    //
    @Override
    public void addEndpointFactory(EndpointFactory factory)
    {
        _instance.endpointFactoryManager().add(factory);
    }

    //
    // Register an EndpointFactory.
    //
    @Override
    public EndpointFactory getEndpointFactory(short type)
    {
        return _instance.endpointFactoryManager().get(type);
    }

    //
    // Look up a Java class by name.
    //
    @Override
    public Class<?> findClass(String className)
    {
        return _instance.findClass(className);
    }

    private Instance _instance;
    private Ice.Communicator _communicator;
}
