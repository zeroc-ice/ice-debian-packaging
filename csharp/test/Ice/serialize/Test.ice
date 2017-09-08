// **********************************************************************
//
// Copyright (c) 2003-2017 ZeroC, Inc. All rights reserved.
//
// This copy of Ice is licensed to you under the terms described in the
// ICE_LICENSE file included in this distribution.
//
// **********************************************************************

#pragma once

#include <Ice/BuiltinSequences.ice>

module Test
{

enum MyEnum
{
    enum1,
    enum2,
    enum3
};

class MyClass;

struct ValStruct
{
    bool bo;
    byte by;
    short sh;
    int i;
    long l;
    MyEnum e;
};

sequence<MyClass*> ProxySeq;

["clr:property"]
struct RefStruct
{
    string s;
    string sp;
    MyClass* p;
    ProxySeq seq;
};

sequence<ValStruct> ValStructS;
["clr:generic:List"]
sequence<ValStruct> ValStructList;
["clr:generic:LinkedList"]
sequence<ValStruct> ValStructLinkedList;
["clr:generic:Stack"]
sequence<ValStruct> ValStructStack;
["clr:generic:Queue"]
sequence<ValStruct> ValStructQueue;
["clr:collection"]
sequence<ValStruct> ValStructCollection;

dictionary<int, string> IntStringD;
dictionary<int, ValStruct> IntValStructD;
dictionary<int, MyClass*> IntProxyD;
["clr:generic:SortedDictionary"]
dictionary<int, string> IntStringSD;
["clr:collection"]
dictionary<int, string> IntStringDC;

class Base
{
    bool bo;
    byte by;
    short sh;
    int i;
    long l;
    MyEnum e;
};

class MyClass extends Base
{
    MyClass c;
    Object o;
    ValStruct s;
};

exception MyException
{
    string name;
    byte b;
    short s;
    int i;
    long l;
    ValStruct vs;
    RefStruct rs;

    ValStructS vss;
    ValStructList vsl;
    ValStructLinkedList vsll;
    ValStructStack vssk;
    ValStructQueue vsq;
    ValStructCollection vsc;

    IntStringD isd;
    IntValStructD ivd;
    IntProxyD ipd;
    IntStringSD issd;
    IntStringDC isdc;

    optional(1) string optName;
    optional(2) int optInt;
    optional(3) ValStruct optValStruct;
    optional(4) RefStruct optRefStruct;
    optional(5) MyEnum optEnum;
    optional(6) MyClass* optProxy;
};

};
