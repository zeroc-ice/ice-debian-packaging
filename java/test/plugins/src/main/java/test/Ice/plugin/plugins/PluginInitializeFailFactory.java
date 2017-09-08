// **********************************************************************
//
// Copyright (c) 2003-2017 ZeroC, Inc. All rights reserved.
//
// This copy of Ice is licensed to you under the terms described in the
// ICE_LICENSE file included in this distribution.
//
// **********************************************************************

package test.Ice.plugin.plugins;

public class PluginInitializeFailFactory implements Ice.PluginFactory
{
    @Override
    public Ice.Plugin create(Ice.Communicator communicator, String name, String[] args)
    {
        return new PluginInitializeFail();
    }

    static class PluginInitializeFail implements Ice.Plugin
    {
        @Override
        public void initialize()
        {
            throw new PluginInitializeFailException();
        }

        @Override
        public void destroy()
        {
            test(false);
        }

        private static void test(boolean b)
        {
            if(!b)
            {
                throw new RuntimeException();
            }
        }
    }
}
