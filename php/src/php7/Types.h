// **********************************************************************
//
// Copyright (c) 2003-2017 ZeroC, Inc. All rights reserved.
//
// This copy of Ice is licensed to you under the terms described in the
// ICE_LICENSE file included in this distribution.
//
// **********************************************************************

#ifndef ICEPHP_TYPES_H
#define ICEPHP_TYPES_H

#include <Config.h>
#include <Communicator.h>
#include <Operation.h>
#include <Ice/Stream.h>
#include <IceUtil/OutputUtil.h>

//
// Global functions.
//
extern "C"
{
ZEND_FUNCTION(IcePHP_defineEnum);
ZEND_FUNCTION(IcePHP_defineStruct);
ZEND_FUNCTION(IcePHP_defineSequence);
ZEND_FUNCTION(IcePHP_defineDictionary);
ZEND_FUNCTION(IcePHP_declareProxy);
ZEND_FUNCTION(IcePHP_defineProxy);
ZEND_FUNCTION(IcePHP_declareClass);
ZEND_FUNCTION(IcePHP_defineClass);
ZEND_FUNCTION(IcePHP_defineException);
ZEND_FUNCTION(IcePHP_stringify);
ZEND_FUNCTION(IcePHP_stringifyException);
}

namespace IcePHP
{

//
// This class is raised as an exception when object marshaling needs to be aborted.
//
class AbortMarshaling
{
};

typedef std::map<unsigned int, Ice::ObjectPtr> ObjectMap;

class ObjectReader;
typedef IceUtil::Handle<ObjectReader> ObjectReaderPtr;

//
// This class keeps track of PHP objects (instances of Slice classes
// and exceptions) that have preserved slices.
//
class SlicedDataUtil
{
public:

    ~SlicedDataUtil();

    void add(const ObjectReaderPtr&);

    void update(TSRMLS_D);

    static void setMember(zval*, const Ice::SlicedDataPtr& TSRMLS_DC);
    static Ice::SlicedDataPtr getMember(zval*, ObjectMap* TSRMLS_DC);

private:

    std::set<ObjectReaderPtr> _readers;
    static zend_class_entry* _slicedDataType;
    static zend_class_entry* _sliceInfoType;
};

struct PrintObjectHistory
{
    int index;
    std::map<unsigned int, int> objects;
};

//
// The delayed nature of class unmarshaling in the Ice protocol requires us to
// handle unmarshaling using a callback strategy. An instance of UnmarshalCallback
// is supplied to each type's unmarshal() member function. For all types except
// classes, the callback is invoked with the unmarshaled value before unmarshal()
// returns. For class instances, however, the callback may not be invoked until
// the stream's finished() function is called.
//
class UnmarshalCallback : public IceUtil::Shared
{
public:

    virtual ~UnmarshalCallback();

    //
    // The unmarshaled() member function receives the unmarshaled value. The
    // last two arguments are the values passed to unmarshal() for use by
    // UnmarshalCallback implementations.
    //
    virtual void unmarshaled(zval*, zval*, void*) = 0;
};
typedef IceUtil::Handle<UnmarshalCallback> UnmarshalCallbackPtr;

//
// Base class for type information.
//
class TypeInfo : public UnmarshalCallback
{
public:

    virtual std::string getId() const = 0;

    virtual bool validate(zval*, bool) = 0; // Validate type data. Bool enables excpetion throwing.

    virtual bool variableLength() const = 0;
    virtual int wireSize() const = 0;
    virtual Ice::OptionalFormat optionalFormat() const = 0;

    virtual bool usesClasses() const; // Default implementation returns false.

    virtual void unmarshaled(zval*, zval*, void*); // Default implementation is assert(false).

    virtual void destroy();

protected:

    TypeInfo();

public:

    //
    // The marshal and unmarshal functions can raise Ice exceptions, and may raise
    // AbortMarshaling if an error occurs.
    //
    virtual void marshal(zval*, const Ice::OutputStreamPtr&, ObjectMap*, bool) = 0;
    virtual void unmarshal(const Ice::InputStreamPtr&, const UnmarshalCallbackPtr&, const CommunicatorInfoPtr&,
                           zval*, void*, bool) = 0;

    virtual void print(zval*, IceUtilInternal::Output&, PrintObjectHistory*) = 0;
};
typedef IceUtil::Handle<TypeInfo> TypeInfoPtr;

//
// Primitive type information.
//
class PrimitiveInfo : public TypeInfo
{
public:

    virtual std::string getId() const;

    virtual bool validate(zval*, bool);

    virtual bool variableLength() const;
    virtual int wireSize() const;
    virtual Ice::OptionalFormat optionalFormat() const;

    virtual void marshal(zval*, const Ice::OutputStreamPtr&, ObjectMap*, bool);
    virtual void unmarshal(const Ice::InputStreamPtr&, const UnmarshalCallbackPtr&, const CommunicatorInfoPtr&,
                           zval*, void*, bool);

    virtual void print(zval*, IceUtilInternal::Output&, PrintObjectHistory*);

    enum Kind
    {
        KindBool,
        KindByte,
        KindShort,
        KindInt,
        KindLong,
        KindFloat,
        KindDouble,
        KindString
    };

    Kind kind;
};
typedef IceUtil::Handle<PrimitiveInfo> PrimitiveInfoPtr;

//
// Enum information.
//
class EnumInfo : public TypeInfo
{
public:

    EnumInfo(const std::string&, zval*);

    virtual std::string getId() const;

    virtual bool validate(zval*, bool);

    virtual bool variableLength() const;
    virtual int wireSize() const;
    virtual Ice::OptionalFormat optionalFormat() const;

    virtual void marshal(zval*, const Ice::OutputStreamPtr&, ObjectMap*, bool);
    virtual void unmarshal(const Ice::InputStreamPtr&, const UnmarshalCallbackPtr&, const CommunicatorInfoPtr&,
                           zval*, void*, bool);

    virtual void print(zval*, IceUtilInternal::Output&, PrintObjectHistory*);

    const std::string id;
    const std::map<Ice::Int, std::string> enumerators;
    const Ice::Int maxValue;

};
typedef IceUtil::Handle<EnumInfo> EnumInfoPtr;

class DataMember : public UnmarshalCallback
{
public:

    virtual void unmarshaled(zval*, zval*, void*);

    void setMember(zval*, zval*);

    std::string name;
    TypeInfoPtr type;
    bool optional;
    int tag;
};
typedef IceUtil::Handle<DataMember> DataMemberPtr;
typedef std::vector<DataMemberPtr> DataMemberList;

//
// Struct information.
//
class StructInfo : public TypeInfo
{
public:

    StructInfo(const std::string&, const std::string&, zval*);

    virtual std::string getId() const;

    virtual bool validate(zval*, bool);

    virtual bool variableLength() const;
    virtual int wireSize() const;
    virtual Ice::OptionalFormat optionalFormat() const;

    virtual bool usesClasses() const;

    virtual void marshal(zval*, const Ice::OutputStreamPtr&, ObjectMap*, bool);
    virtual void unmarshal(const Ice::InputStreamPtr&, const UnmarshalCallbackPtr&, const CommunicatorInfoPtr&,
                           zval*, void*, bool);

    virtual void print(zval*, IceUtilInternal::Output&, PrintObjectHistory*);

    virtual void destroy();

    const std::string id;
    const std::string name; // PHP class name
    const DataMemberList members;
    const zend_class_entry* zce;

private:

    bool _variableLength;
    int _wireSize;
    zval _nullMarshalValue;
};
typedef IceUtil::Handle<StructInfo> StructInfoPtr;

//
// Sequence information.
//
class SequenceInfo : public TypeInfo
{
public:

    SequenceInfo(const std::string&, zval*);

    virtual std::string getId() const;

    virtual bool validate(zval*, bool);

    virtual bool variableLength() const;
    virtual int wireSize() const;
    virtual Ice::OptionalFormat optionalFormat() const;

    virtual bool usesClasses() const;

    virtual void marshal(zval*, const Ice::OutputStreamPtr&, ObjectMap*, bool);
    virtual void unmarshal(const Ice::InputStreamPtr&, const UnmarshalCallbackPtr&, const CommunicatorInfoPtr&,
                           zval*, void*, bool);

    virtual void print(zval*, IceUtilInternal::Output&, PrintObjectHistory*);

    virtual void unmarshaled(zval*, zval*, void*);

    virtual void destroy();

    const std::string id;
    const TypeInfoPtr elementType;

private:

    void marshalPrimitiveSequence(const PrimitiveInfoPtr&, zval*, const Ice::OutputStreamPtr&);
    void unmarshalPrimitiveSequence(const PrimitiveInfoPtr&, const Ice::InputStreamPtr&, const UnmarshalCallbackPtr&,
                                    zval*, void*);
};
typedef IceUtil::Handle<SequenceInfo> SequenceInfoPtr;

//
// Dictionary information.
//
class DictionaryInfo : public TypeInfo
{
public:

    DictionaryInfo(const std::string&, zval*, zval*);

    virtual std::string getId() const;

    virtual bool validate(zval*, bool);

    virtual bool variableLength() const;
    virtual int wireSize() const;
    virtual Ice::OptionalFormat optionalFormat() const;

    virtual bool usesClasses() const;

    virtual void marshal(zval*, const Ice::OutputStreamPtr&, ObjectMap*, bool);
    virtual void unmarshal(const Ice::InputStreamPtr&, const UnmarshalCallbackPtr&, const CommunicatorInfoPtr&,
                           zval*, void*, bool);

    virtual void print(zval*, IceUtilInternal::Output&, PrintObjectHistory*);

    virtual void destroy();

    class KeyCallback : public UnmarshalCallback
    {
    public:

        KeyCallback();
        ~KeyCallback();

        virtual void unmarshaled(zval*, zval*, void*);

        zval key;
    };
    typedef IceUtil::Handle<KeyCallback> KeyCallbackPtr;

    class ValueCallback : public UnmarshalCallback
    {
    public:

        ValueCallback(zval*);
        ~ValueCallback();

        virtual void unmarshaled(zval*, zval*, void*);

        zval key;
    };
    typedef IceUtil::Handle<ValueCallback> ValueCallbackPtr;

    std::string id;
    TypeInfoPtr keyType;
    TypeInfoPtr valueType;

private:

    bool _variableLength;
    int _wireSize;
};
typedef IceUtil::Handle<DictionaryInfo> DictionaryInfoPtr;

class ExceptionInfo;
typedef IceUtil::Handle<ExceptionInfo> ExceptionInfoPtr;
typedef std::vector<ExceptionInfoPtr> ExceptionInfoList;

class ClassInfo;
typedef IceUtil::Handle<ClassInfo> ClassInfoPtr;
typedef std::vector<ClassInfoPtr> ClassInfoList;

typedef std::vector<TypeInfoPtr> TypeInfoList;

class ClassInfo : public TypeInfo
{
public:

    ClassInfo(const std::string&);

    void define(const std::string&, Ice::Int, bool, bool, zval*, zval*, zval*);

    virtual std::string getId() const;

    virtual bool validate(zval*, bool);

    virtual bool variableLength() const;
    virtual int wireSize() const;
    virtual Ice::OptionalFormat optionalFormat() const;

    virtual bool usesClasses() const;

    virtual void marshal(zval*, const Ice::OutputStreamPtr&, ObjectMap*, bool);
    virtual void unmarshal(const Ice::InputStreamPtr&, const UnmarshalCallbackPtr&, const CommunicatorInfoPtr&,
                           zval*, void*, bool);

    virtual void print(zval*, IceUtilInternal::Output&, PrintObjectHistory*);

    virtual void destroy();

    void printMembers(zval*, IceUtilInternal::Output&, PrintObjectHistory*);

    bool isA(const std::string&) const;

    void addOperation(const std::string&, const OperationPtr&);
    OperationPtr getOperation(const std::string&) const;

    const std::string id;
    const std::string name; // PHP class name
    const Ice::Int compactId;
    const bool isAbstract;
    const bool preserve;
    const ClassInfoPtr base;
    const ClassInfoList interfaces;
    const DataMemberList members;
    const DataMemberList optionalMembers;
    const zend_class_entry* zce;
    bool defined;

    typedef std::map<std::string, OperationPtr> OperationMap;
    OperationMap operations;
};

//
// Proxy information.
//
class ProxyInfo : public TypeInfo
{
public:

    ProxyInfo(const std::string&);

    void define(const ClassInfoPtr&);

    virtual std::string getId() const;

    virtual bool validate(zval*, bool);

    virtual bool variableLength() const;
    virtual int wireSize() const;
    virtual Ice::OptionalFormat optionalFormat() const;

    virtual void marshal(zval*, const Ice::OutputStreamPtr&, ObjectMap*, bool);
    virtual void unmarshal(const Ice::InputStreamPtr&, const UnmarshalCallbackPtr&, const CommunicatorInfoPtr&,
                           zval*, void*, bool);

    virtual void print(zval*, IceUtilInternal::Output&, PrintObjectHistory*);

    virtual void destroy();

    const std::string id;
    const ClassInfoPtr cls;
    bool defined;
};
typedef IceUtil::Handle<ProxyInfo> ProxyInfoPtr;

//
// Exception information.
//
class ExceptionInfo : public IceUtil::Shared
{
public:

    zval* unmarshal(const Ice::InputStreamPtr&, const CommunicatorInfoPtr&);

    void print(zval*, IceUtilInternal::Output&);
    void printMembers(zval*, IceUtilInternal::Output&, PrintObjectHistory*);

    bool isA(const std::string&) const;

    std::string id;
    std::string name; // PHP class name
    bool preserve;
    ExceptionInfoPtr base;
    DataMemberList members;
    DataMemberList optionalMembers;
    bool usesClasses;
    zend_class_entry* zce;
};

ClassInfoPtr getClassInfoById(const std::string&);
ClassInfoPtr getClassInfoByName(const std::string&);
ExceptionInfoPtr getExceptionInfo(const std::string&);

bool isUnset(zval*);
void assignUnset(zval*);

bool typesInit(INIT_FUNC_ARGS);
bool typesRequestInit(void);
bool typesRequestShutdown(void);

//
// ObjectWriter wraps a PHP object for marshaling.
//
class ObjectWriter : public Ice::ObjectWriter
{
public:

    ObjectWriter(zval*, ObjectMap*, const ClassInfoPtr&);
    ~ObjectWriter();

    virtual void ice_preMarshal();

    virtual void write(const Ice::OutputStreamPtr&) const;

private:

    void writeMembers(const Ice::OutputStreamPtr&, const DataMemberList&) const;

    zval _object;
    ObjectMap* _map;
    ClassInfoPtr _info;
};

//
// ObjectReader unmarshals the state of an Ice object.
//
class ObjectReader : public Ice::ObjectReader
{
public:

    ObjectReader(zval*, const ClassInfoPtr&, const CommunicatorInfoPtr&);
    ~ObjectReader();

    virtual void ice_postUnmarshal();

    virtual void read(const Ice::InputStreamPtr&);

    virtual ClassInfoPtr getInfo() const;

    zval* getObject() const;

    Ice::SlicedDataPtr getSlicedData() const;

private:

    zval _object;
    ClassInfoPtr _info;
    CommunicatorInfoPtr _communicator;
    Ice::SlicedDataPtr _slicedData;
};

//
// ExceptionReader creates a PHP user exception and unmarshals it.
//
class ExceptionReader : public Ice::UserExceptionReader
{
public:

    ExceptionReader(const CommunicatorInfoPtr&, const ExceptionInfoPtr&);
    ~ExceptionReader() throw();

    virtual void read(const Ice::InputStreamPtr&) const;
    virtual bool usesClasses() const;

    virtual std::string ice_name() const;
    virtual ExceptionReader* ice_clone() const;
    virtual void ice_throw() const;

    ExceptionInfoPtr getInfo() const;

    zval* getException() const;

    Ice::SlicedDataPtr getSlicedData() const;

private:

    CommunicatorInfoPtr _communicatorInfo;
    ExceptionInfoPtr _info;
    zval* _ex;
    Ice::SlicedDataPtr _slicedData;
};

class IdResolver : public Ice::CompactIdResolver
{
public:

    IdResolver(void);
    virtual std::string resolve(Ice::Int) const;
};

} // End of namespace IcePHP

#endif
