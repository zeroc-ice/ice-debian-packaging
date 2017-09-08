// **********************************************************************
//
// Copyright (c) 2003-2017 ZeroC, Inc. All rights reserved.
//
// This copy of Ice is licensed to you under the terms described in the
// ICE_LICENSE file included in this distribution.
//
// **********************************************************************

package test.Ice.exceptions;


public final class ServantLocatorI implements Ice.ServantLocator
{
    @Override
    public Ice.Object locate(Ice.Current curr, Ice.LocalObjectHolder cookie)
    {
        return null;
    }

    @Override
    public void finished(Ice.Current curr, Ice.Object servant, java.lang.Object cookie)
    {
    }

    @Override
    public void deactivate(String category)
    {
    }
}
