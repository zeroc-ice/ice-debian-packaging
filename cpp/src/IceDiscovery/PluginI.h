// **********************************************************************
//
// Copyright (c) 2003-2017 ZeroC, Inc. All rights reserved.
//
// This copy of Ice is licensed to you under the terms described in the
// ICE_LICENSE file included in this distribution.
//
// **********************************************************************

#ifndef PLUGIN_I_H
#define PLUGIN_I_H

#include <Ice/Plugin.h>
#include <IceDiscovery/LookupI.h>

namespace IceDiscovery
{

class PluginI : public Ice::Plugin
{
public:

    PluginI(const Ice::CommunicatorPtr&);

    virtual void initialize();
    virtual void destroy();

private:

    const Ice::CommunicatorPtr _communicator;
    Ice::ObjectAdapterPtr _multicastAdapter;
    Ice::ObjectAdapterPtr _replyAdapter;
    Ice::ObjectAdapterPtr _locatorAdapter;
    LookupIPtr _lookup;
    Ice::LocatorPrx _locator;
    Ice::LocatorPrx _defaultLocator;
};

};

#endif
