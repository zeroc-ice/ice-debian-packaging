// **********************************************************************
//
// Copyright (c) 2003-2017 ZeroC, Inc. All rights reserved.
//
// This copy of Ice is licensed to you under the terms described in the
// ICE_LICENSE file included in this distribution.
//
// **********************************************************************

#ifndef ICE_BT_PLUGIN_I_H
#define ICE_BT_PLUGIN_I_H

#include <Ice/CommunicatorF.h>
#include <IceBT/Plugin.h>
#include <IceBT/EngineF.h>

namespace IceBT
{

class PluginI : public Plugin
{
public:

    PluginI(const Ice::CommunicatorPtr&);

    //
    // From Ice::Plugin.
    //
    virtual void initialize();
    virtual void destroy();

    //
    // From IceBT::Plugin.
    //
#ifdef ICE_CPP11_MAPPING
    virtual void startDiscovery(const std::string& address,
                                std::function<void(const std::string& addr, const PropertyMap& props)>);
#else
    virtual void startDiscovery(const std::string&, const DiscoveryCallbackPtr&);
#endif
    virtual void stopDiscovery(const std::string&);

    virtual DeviceMap getDevices() const;

private:

    EnginePtr _engine;
};

}

#endif
