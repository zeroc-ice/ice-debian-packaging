// **********************************************************************
//
// Copyright (c) 2003-2017 ZeroC, Inc. All rights reserved.
//
// This copy of Ice is licensed to you under the terms described in the
// ICE_LICENSE file included in this distribution.
//
// **********************************************************************

package test.Ice.objects;

import java.io.PrintWriter;

import test.Ice.objects.Test.B;
import test.Ice.objects.Test.BHolder;
import test.Ice.objects.Test.C;
import test.Ice.objects.Test.CHolder;
import test.Ice.objects.Test.D;
import test.Ice.objects.Test.DHolder;
import test.Ice.objects.Test.E;
import test.Ice.objects.Test.F;
import test.Ice.objects.Test.H;
import test.Ice.objects.Test.I;
import test.Ice.objects.Test.A1;
import test.Ice.objects.Test.B1;
import test.Ice.objects.Test.D1;
import test.Ice.objects.Test.EDerived;
import test.Ice.objects.Test.Base;
import test.Ice.objects.Test.S;
import test.Ice.objects.Test.BaseSeqHolder;
import test.Ice.objects.Test.InitialPrx;
import test.Ice.objects.Test.InitialPrxHelper;
import test.Ice.objects.Test.J;
import test.Ice.objects.Test.UnexpectedObjectExceptionTestPrx;
import test.Ice.objects.Test.UnexpectedObjectExceptionTestPrxHelper;
import test.Ice.objects.Test.Recursive;

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

    public static InitialPrx
    allTests(Ice.Communicator communicator, PrintWriter out)
    {
        out.print("testing stringToProxy... ");
        out.flush();
        String ref = "initial:default -p 12010";
        Ice.ObjectPrx base = communicator.stringToProxy(ref);
        test(base != null);
        out.println("ok");

        out.print("testing checked cast... ");
        out.flush();
        InitialPrx initial = InitialPrxHelper.checkedCast(base);
        test(initial != null);
        test(initial.equals(base));
        out.println("ok");

        out.print("getting B1... ");
        out.flush();
        B b1 = initial.getB1();
        test(b1 != null);
        out.println("ok");

        out.print("getting B2... ");
        out.flush();
        B b2 = initial.getB2();
        test(b2 != null);
        out.println("ok");

        out.print("getting C... ");
        out.flush();
        C c = initial.getC();
        test(c != null);
        out.println("ok");

        out.print("getting D... ");
        out.flush();
        D d = initial.getD();
        test(d != null);
        out.println("ok");

        out.print("checking consistency... ");
        out.flush();
        test(b1 != b2);
        //test(b1 != c);
        //test(b1 != d);
        //test(b2 != c);
        //test(b2 != d);
        //test(c != d);
        test(b1.theB == b1);
        test(b1.theC == null);
        test(b1.theA instanceof B);
        test(((B)b1.theA).theA == b1.theA);
        test(((B)b1.theA).theB == b1);
        test(((B)b1.theA).theC instanceof C);
        test((((B)b1.theA).theC).theB == b1.theA);
        test(b1.preMarshalInvoked);
        test(b1.postUnmarshalInvoked(null));
        test(b1.theA.preMarshalInvoked);
        test(b1.theA.postUnmarshalInvoked(null));
        test(((B)b1.theA).theC.preMarshalInvoked);
        test(((B)b1.theA).theC.postUnmarshalInvoked(null));

        // More tests possible for b2 and d, but I think this is already
        // sufficient.
        test(b2.theA == b2);
        test(d.theC == null);
        out.println("ok");

        out.print("getting B1, B2, C, and D all at once... ");
        out.flush();
        BHolder b1H = new BHolder();
        BHolder b2H = new BHolder();
        CHolder cH = new CHolder();
        DHolder dH = new DHolder();
        initial.getAll(b1H, b2H, cH, dH);
        b1 = b1H.value;
        b2 = b2H.value;
        c = cH.value;
        d = dH.value;
        test(b1 != null);
        test(b2 != null);
        test(c != null);
        test(d != null);
        out.println("ok");

        out.print("checking consistency... ");
        out.flush();
        test(b1 != b2);
        //test(b1 != c);
        //test(b1 != d);
        //test(b2 != c);
        //test(b2 != d);
        //test(c != d);
        test(b1.theA == b2);
        test(b1.theB == b1);
        test(b1.theC == null);
        test(b2.theA == b2);
        test(b2.theB == b1);
        test(b2.theC == c);
        test(c.theB == b2);
        test(d.theA == b1);
        test(d.theB == b2);
        test(d.theC == null);
        test(d.preMarshalInvoked);
        test(d.postUnmarshalInvoked(null));
        test(d.theA.preMarshalInvoked);
        test(d.theA.postUnmarshalInvoked(null));
        test(d.theB.preMarshalInvoked);
        test(d.theB.postUnmarshalInvoked(null));
        test(d.theB.theC.preMarshalInvoked);
        test(d.theB.theC.postUnmarshalInvoked(null));

        out.println("ok");

        out.print("testing protected members... ");
        out.flush();
        E e = initial.getE();
        test(e.checkValues());
        try
        {
            test((E.class.getDeclaredField("i").getModifiers() & java.lang.reflect.Modifier.PROTECTED) != 0);
            test((E.class.getDeclaredField("s").getModifiers() & java.lang.reflect.Modifier.PROTECTED) != 0);
        }
        catch(Exception ex)
        {
            test(false);
        }
        F f = initial.getF();
        test(f.checkValues());
        test(f.e2.checkValues());
        try
        {
            test((F.class.getDeclaredField("e1").getModifiers() & java.lang.reflect.Modifier.PROTECTED) != 0);
            test((F.class.getDeclaredField("e2").getModifiers() & java.lang.reflect.Modifier.PROTECTED) == 0);
        }
        catch(Exception ex)
        {
            test(false);
        }
        out.println("ok");

        out.print("getting I, J and H... ");
        out.flush();
        I i = initial.getI();
        test(i != null);
        I j = initial.getJ();
        test(j != null && ((J)j) != null);
        I h = initial.getH();
        test(h != null && ((H)h) != null);
        out.println("ok");

        out.print("getting D1... ");
        out.flush();
        D1 d1 = new D1(new A1("a1"), new A1("a2"), new A1("a3"), new A1("a4"));
        d1 = initial.getD1(d1);
        test(d1.a1.name.equals("a1"));
        test(d1.a2.name.equals("a2"));
        test(d1.a3.name.equals("a3"));
        test(d1.a4.name.equals("a4"));
        out.println("ok");

        out.print("throw EDerived... ");
        out.flush();
        try
        {
            initial.throwEDerived();
            test(false);
        }
        catch(EDerived ederived)
        {
            test(ederived.a1.name.equals("a1"));
            test(ederived.a2.name.equals("a2"));
            test(ederived.a3.name.equals("a3"));
            test(ederived.a4.name.equals("a4"));
        }
        out.println("ok");

        out.print("setting I... ");
        out.flush();
       initial.setI(i);
        initial.setI(j);
        initial.setI(h);
        out.println("ok");

        out.print("testing sequences...");
        try
        {
            out.flush();
            Base[] inS = new Base[0];
            BaseSeqHolder outS = new BaseSeqHolder();
            Base[] retS;
            retS = initial.opBaseSeq(inS, outS);

            inS = new Base[1];
            inS[0] = new Base(new S(), "");
            retS = initial.opBaseSeq(inS, outS);
            test(retS.length == 1 && outS.value.length == 1);
        }
        catch(Ice.OperationNotExistException ex)
        {
        }
        out.println("ok");

        out.print("testing recursive type... ");
        out.flush();
        try
        {
            Recursive top = new Recursive();
            Recursive p = top;
            int depth = 0;
            try
            {
                for(; depth <= 20000; ++depth)
                {
                    p.v = new Recursive();
                    p = p.v;
                    if((depth < 10 && (depth % 10) == 0) ||
                       (depth < 1000 && (depth % 100) == 0) ||
                       (depth < 10000 && (depth % 1000) == 0) ||
                       (depth % 10000) == 0)
                    {
                        initial.setRecursive(top);
                    }
                }
                test(!initial.supportsClassGraphDepthMax());
            }
            catch(Ice.UnknownLocalException ex)
            {
                // Expected marshal exception from the server (max class graph depth reached)
            }
            catch(Ice.UnknownException ex)
            {
                // Expected stack overflow from the server (Java only)
            }
            catch(java.lang.StackOverflowError ex)
            {
                // Stack overflow while writing instances
            }
            initial.setRecursive(new Recursive());
        }
        catch(Ice.OperationNotExistException ex)
        {
            // Expected if running against server that doesn't support this method.
        }
        out.println("ok");

        out.print("testing compact ID...");
        out.flush();
        try
        {
            test(initial.getCompact() != null);
        }
        catch(Ice.OperationNotExistException ex)
        {
        }
        out.println("ok");

        out.print("testing UnexpectedObjectException...");
        out.flush();
        ref = "uoet:default -p 12010";
        base = communicator.stringToProxy(ref);
        test(base != null);
        UnexpectedObjectExceptionTestPrx uoet = UnexpectedObjectExceptionTestPrxHelper.uncheckedCast(base);
        test(uoet != null);
        try
        {
            uoet.op();
            test(false);
        }
        catch(Ice.UnexpectedObjectException ex)
        {
            test(ex.type.equals("::Test::AlsoEmpty"));
            test(ex.expectedType.equals("::Test::Empty"));
        }
        catch(java.lang.Exception ex)
        {
            out.println(ex);
            test(false);
        }
        out.println("ok");

        return initial;
    }
}
