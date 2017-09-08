// **********************************************************************
//
// Copyright (c) 2003-2017 ZeroC, Inc. All rights reserved.
//
// This copy of Ice is licensed to you under the terms described in the
// ICE_LICENSE file included in this distribution.
//
// **********************************************************************

package test.Ice.classLoader;

import test.Ice.classLoader.Test.AbstractClass;
import test.Ice.classLoader.Test.ConcreteClass;
import test.Ice.classLoader.Test.E;
import test.Ice.classLoader.Test._InitialDisp;

public final class InitialI extends _InitialDisp
{
    public
    InitialI(Ice.ObjectAdapter adapter)
    {
        _adapter = adapter;
    }

    @Override
    public ConcreteClass
    getConcreteClass(Ice.Current current)
    {
        return new ConcreteClass();
    }

    @Override
    public AbstractClass
    getAbstractClass(Ice.Current current)
    {
        return new AbstractClassI();
    }

    @Override
    public void
    throwException(Ice.Current current)
        throws E
    {
        throw new E();
    }

    @Override
    public void
    shutdown(Ice.Current current)
    {
        _adapter.getCommunicator().shutdown();
    }

    private Ice.ObjectAdapter _adapter;
}
