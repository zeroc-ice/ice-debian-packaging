// **********************************************************************
//
// Copyright (c) 2003-2017 ZeroC, Inc. All rights reserved.
//
// This copy of Ice is licensed to you under the terms described in the
// ICE_LICENSE file included in this distribution.
//
// **********************************************************************

package test.Ice.plugin.plugins;

public class PluginThreeFactory implements Ice.PluginFactory
{
    @Override
    public Ice.Plugin create(Ice.Communicator communicator, String name, String[] args)
    {
        return new PluginThree(communicator);
    }

    static class PluginThree extends BasePlugin
    {
        public PluginThree(Ice.Communicator communicator)
        {
            super(communicator);
        }

        @Override
        public void initialize()
        {
            _other = (BasePlugin)_communicator.getPluginManager().getPlugin("PluginTwo");
            test(_other.isInitialized());
            _initialized = true;
        }

        @Override
        public void destroy()
        {
            _destroyed = true;
            test(!_other.isDestroyed());
        }
    }
}
