// **********************************************************************
//
// Copyright (c) 2003-2016 ZeroC, Inc. All rights reserved.
//
// This copy of Ice is licensed to you under the terms described in the
// ICE_LICENSE file included in this distribution.
//
// **********************************************************************

#include <Ice/Ice.h>
#include <IceSSL/Plugin.h>
#if ICE_USE_OPENSSL
#  include <openssl/ssl.h> // Required for OPENSSL_VERSION_NUMBER
#endif
#include <TestCommon.h>
#include <Test.h>
#include <fstream>

#ifdef __APPLE__
#  include <sys/sysctl.h>
#endif

using namespace std;
using namespace Ice;

#ifdef ICE_USE_OPENSSL
//
// With OpenSSL 1.1.0 we need to set SECLEVEL=0 to allow ADH ciphers
//
#  if OPENSSL_VERSION_NUMBER >= 0x10100000L
const string anonCiphers = "ADH:@SECLEVEL=0";
#  else
const string anonCiphers = "ADH";
#  endif
#endif

void
readFile(const string& file, vector<char>& buffer)
{
    ifstream is(file.c_str(), ios::in | ios::binary);
    if(!is.good())
    {
        throw "error opening file " + file;
    }

    is.seekg(0, is.end);
    buffer.resize(static_cast<int>(is.tellg()));
    is.seekg(0, is.beg);

    is.read(&buffer[0], buffer.size());

    if(!is.good())
    {
        throw "error reading file " + file;
    }
}

#ifdef ICE_USE_SCHANNEL
class ImportCerts
{
public:

    ImportCerts(const string& defaultDir, const char* certificates[])
    {
        //
        // First we need to import some certificates in the user store.
        //
        _store = CertOpenStore(CERT_STORE_PROV_SYSTEM, 0, 0, CERT_SYSTEM_STORE_CURRENT_USER, L"MY");
        test(_store);

        for(int i = 0; certificates[i] != 0; ++i)
        {
            vector<char> buffer;
            readFile(defaultDir + certificates[i], buffer);

            CRYPT_DATA_BLOB p12Blob;
            p12Blob.cbData = static_cast<DWORD>(buffer.size());
            p12Blob.pbData = reinterpret_cast<BYTE*>(&buffer[0]);

            HCERTSTORE p12 = PFXImportCertStore(&p12Blob, L"password", CRYPT_USER_KEYSET);
            _stores.push_back(p12);

            PCCERT_CONTEXT next = 0;
            PCCERT_CONTEXT newCert = 0;
            do
            {
                if((next = CertFindCertificateInStore(p12, X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, 0,
                                                      CERT_FIND_ANY, 0, next)))
                {
                    if(CertAddCertificateContextToStore(_store, next, CERT_STORE_ADD_ALWAYS, &newCert))
                    {
                        _certs.push_back(newCert);
                    }
                }
            }
            while(next);
        }
    }

    ~ImportCerts()
    {
        cleanup();
    }

    void cleanup()
    {
        for(vector<PCCERT_CONTEXT>::const_iterator i = _certs.begin(); i != _certs.end(); ++i)
        {
            PCCERT_CONTEXT cert = *i;

            // Retrieve the certificate CERT_KEY_PROV_INFO_PROP_ID property, we use the CRYPT_KEY_PROV_INFO
            // data to then remove the key set associated with the certificate.
            //
            DWORD size = 0;
            if(CertGetCertificateContextProperty(cert, CERT_KEY_PROV_INFO_PROP_ID, 0, &size))
            {
                vector<char> buf(size);
                if(CertGetCertificateContextProperty(cert, CERT_KEY_PROV_INFO_PROP_ID, &buf[0], &size))
                {
                    CRYPT_KEY_PROV_INFO* keyProvInfo = reinterpret_cast<CRYPT_KEY_PROV_INFO*>(&buf[0]);
                    HCRYPTPROV cryptProv = 0;
                    if(CryptAcquireContextW(&cryptProv, keyProvInfo->pwszContainerName, keyProvInfo->pwszProvName,
                                            keyProvInfo->dwProvType, 0))
                    {
                        CryptAcquireContextW(&cryptProv, keyProvInfo->pwszContainerName, keyProvInfo->pwszProvName,
                                             keyProvInfo->dwProvType, CRYPT_DELETEKEYSET);
                    }
                }
            }
            CertDeleteCertificateFromStore(cert);
        }
	_certs.clear();
        for(vector<HCERTSTORE>::const_iterator i = _stores.begin(); i != _stores.end(); ++i)
        {
            CertCloseStore(*i, 0);
        }
	_stores.clear();
	if(_store)
	{
	    CertCloseStore(_store, 0);
	    _store = 0;
	}
    }

private:

    HCERTSTORE _store;
    vector<HCERTSTORE> _stores;
    vector<PCCERT_CONTEXT> _certs;
};

#else
class ImportCerts
{
public:

    ImportCerts(const string& defaultDir, const char* certificates[])
    {
        // Nothing to do.
    }

    void cleanup()
    {
    }
};
#endif

class PasswordPromptI : public IceSSL::PasswordPrompt
{
public:

    PasswordPromptI(const string& password) : _password(password), _count(0)
    {
    }

    virtual string getPassword()
    {
        ++_count;
        return _password;
    }

    int count() const
    {
        return _count;
    }

private:

    string _password;
    int _count;
};
typedef IceUtil::Handle<PasswordPromptI> PasswordPromptIPtr;

class CertificateVerifierI : public IceSSL::CertificateVerifier
{
public:

    CertificateVerifierI()
    {
        reset();
    }

    virtual bool
    verify(const IceSSL::NativeConnectionInfoPtr& info)
    {
        if(info->nativeCerts.size() > 0)
        {
            //
            // Subject alternative name
            //
            {
                vector<pair<int, string> > altNames = info->nativeCerts[0]->getSubjectAlternativeNames();
                vector<string> ipAddresses;
                vector<string> dnsNames;
                for(vector<pair<int, string> >::const_iterator p = altNames.begin(); p != altNames.end(); ++p)
                {
                    if(p->first == 7)
                    {
                        ipAddresses.push_back(p->second);
                    }
                    else if(p->first == 2)
                    {
                        dnsNames.push_back(p->second);
                    }

                }

                test(find(dnsNames.begin(), dnsNames.end(), "server") != dnsNames.end());
                test(find(ipAddresses.begin(), ipAddresses.end(), "127.0.0.1") != ipAddresses.end());
            }

            //
            // Issuer alternative name
            //
            {
                vector<pair<int, string> > altNames = info->nativeCerts[0]->getIssuerAlternativeNames();
                vector<string> ipAddresses;
                vector<string> emailAddresses;
                for(vector<pair<int, string> >::const_iterator p = altNames.begin(); p != altNames.end(); ++p)
                {
                    if(p->first == 7)
                    {
                        ipAddresses.push_back(p->second);
                    }
                    else if(p->first == 1)
                    {
                        emailAddresses.push_back(p->second);
                    }
                }

                test(find(ipAddresses.begin(), ipAddresses.end(), "127.0.0.1") != ipAddresses.end());
                test(find(emailAddresses.begin(), emailAddresses.end(), "issuer@zeroc.com") != emailAddresses.end());
            }
        }

        _hadCert = info->nativeCerts.size() != 0;
        _invoked = true;
        return _returnValue;
    }

    void reset()
    {
        _returnValue = true;
        _invoked = false;
        _hadCert = false;
    }

    void returnValue(bool b)
    {
        _returnValue = b;
    }

    bool invoked() const
    {
        return _invoked;
    }

    bool hadCert() const
    {
        return _hadCert;
    }

private:

    bool _returnValue;
    bool _invoked;
    bool _hadCert;
};
typedef IceUtil::Handle<CertificateVerifierI> CertificateVerifierIPtr;

int keychainN = 0;

static PropertiesPtr
createClientProps(const Ice::PropertiesPtr& defaultProps, const string& defaultDir, const string& defaultHost, bool p12)
{
    PropertiesPtr result = createProperties();
    //
    // Don't set the plugin property, the client registered the plugin with registerIceSSL.
    //
    //result->setProperty("Ice.Plugin.IceSSL", "IceSSL:createIceSSL");
    if(!defaultDir.empty())
    {
        result->setProperty("IceSSL.DefaultDir", defaultDir);
    }
    if(!defaultProps->getProperty("Ice.IPv6").empty())
    {
        result->setProperty("Ice.IPv6", defaultProps->getProperty("Ice.IPv6"));
    }
    if(!defaultHost.empty())
    {
        result->setProperty("Ice.Default.Host", defaultHost);
    }
    if(p12)
    {
        result->setProperty("IceSSL.Password", "password");
    }
    //result->setProperty("IceSSL.Trace.Security", "1");
#ifdef ICE_USE_SECURE_TRANSPORT
    ostringstream keychainName;
    keychainName << "../certs/keychain/client" << keychainN++ << ".keychain";
    const string keychainPassword = "password";
    result->setProperty("IceSSL.Keychain", keychainName.str());
    result->setProperty("IceSSL.KeychainPassword", keychainPassword);
#endif
    return result;
}

static Test::Properties
createServerProps(const Ice::PropertiesPtr& defaultProps, const string& defaultDir, const string& defaultHost, bool p12)
{
    Test::Properties result;
    result["Ice.Plugin.IceSSL"] = "IceSSL:createIceSSL";
    if(!defaultDir.empty())
    {
        result["IceSSL.DefaultDir"] = defaultDir;
    }
    if(!defaultProps->getProperty("Ice.IPv6").empty())
    {
        result["Ice.IPv6"] = defaultProps->getProperty("Ice.IPv6");
    }
    if(!defaultHost.empty())
    {
        result["Ice.Default.Host"] = defaultHost;
    }
    if(p12)
    {
        result["IceSSL.Password"] = "password";
    }
    //result["IceSSL.Trace.Security"] = "1";
#ifdef ICE_USE_SECURE_TRANSPORT
    ostringstream keychainName;
    keychainName << "../certs/keychain/server" << keychainN << ".keychain";
    result["IceSSL.Keychain"] = keychainName.str();
    result["IceSSL.KeychainPassword"] = "password";
#endif
    return result;
}

static Test::Properties
createServerProps(const Ice::PropertiesPtr& defaultProps, const string& defaultDir, const string& defaultHost,
                  bool p12, const string& cert, const string& ca)
{
    Test::Properties d;

    //
    // If no CA is specified, we don't set IceSSL.DefaultDir since
    // with OpenSSL the CAs might still be found.
    //
    d = createServerProps(defaultProps, defaultDir, defaultHost, p12);
    if(!ca.empty())
    {
        d["IceSSL.CAs"] = ca + ".pem";
    }

    if(!cert.empty())
    {
        if(p12)
        {
            d["IceSSL.CertFile"] = cert + ".p12";
        }
        else
        {
            d["IceSSL.CertFile"] = cert + "_pub.pem";
            d["IceSSL.KeyFile"] = cert + "_priv.pem";
        }
    }
    return d;
}

static PropertiesPtr
createClientProps(const Ice::PropertiesPtr& defaultProps, const string& defaultDir, const string& defaultHost,
                  bool p12, const string& cert, const string& ca)
{
    Ice::PropertiesPtr properties;

    properties = createClientProps(defaultProps, defaultDir, defaultHost, p12);
    if(!ca.empty())
    {
        properties->setProperty("IceSSL.CAs", ca + ".pem");
    }

    if(!cert.empty())
    {
        if(p12)
        {
            properties->setProperty("IceSSL.CertFile", cert + ".p12");
        }
        else
        {
            properties->setProperty("IceSSL.CertFile", cert + "_pub.pem");
            properties->setProperty("IceSSL.KeyFile", cert + "_priv.pem");
        }
    }
    return properties;
}

void verify(const IceSSL::CertificatePtr& cert, const IceSSL::CertificatePtr& ca)
{
    cerr << "Verify signature: ";
    if(cert->verify(ca))
    {
        cerr << " VALID";
    }
    else
    {
        cerr << " INVALID";
    }
    cerr << endl;
}

void
allTests(const CommunicatorPtr& communicator, const string& testDir, bool p12, bool shutdown)
{
#ifdef __APPLE__
    bool isElCapitanOrGreater = false;
    vector<char> s(256);
    size_t size = s.size();
    int ret = sysctlbyname("kern.osrelease", &s[0], &size, NULL, 0);
    if(ret == 0)
    {
        // version format is x.y.z
        size_t first = string(&s[0]).find_first_of(".");
        int majorVersion = atoi(string(&s[0]).substr(0, first).c_str());
        isElCapitanOrGreater = majorVersion >= 15;
    }
#endif

    string factoryRef = "factory:tcp -p 12010";
    ObjectPrx base = communicator->stringToProxy(factoryRef);
    test(base);
    Test::ServerFactoryPrx factory = Test::ServerFactoryPrx::checkedCast(base);

    string defaultHost = communicator->getProperties()->getProperty("Ice.Default.Host");
    string defaultDir = testDir + "/../certs";
    Ice::PropertiesPtr defaultProps = communicator->getProperties();
#ifdef _WIN32
    string sep = ";";
#else
    string sep = ":";
#endif

    IceSSL::NativeConnectionInfoPtr info;

    cout << "testing manual initialization... " << flush;
    {
        InitializationData initData;
        initData.properties = createClientProps(defaultProps, defaultDir, defaultHost, p12);
        initData.properties->setProperty("Ice.InitPlugins", "0");
        CommunicatorPtr comm = initialize(initData);
        ObjectPrx p = comm->stringToProxy("dummy:ssl -p 9999");
        try
        {
            p->ice_ping();
            test(false);
        }
        catch(const PluginInitializationException&)
        {
            // Expected.
        }
        catch(const LocalException&)
        {
            test(false);
        }
        comm->destroy();
    }

//
// Anonymous cipher are not supported with SChannel
//
#ifndef ICE_USE_SCHANNEL
    {
        InitializationData initData;
        initData.properties = createClientProps(defaultProps, defaultDir, defaultHost, p12);
        initData.properties->setProperty("Ice.InitPlugins", "0");
#  ifdef ICE_USE_OPENSSL
        initData.properties->setProperty("IceSSL.Ciphers", anonCiphers);
#  else
        initData.properties->setProperty("IceSSL.Ciphers", "DH_anon_WITH_AES_256_CBC_SHA");
#  endif
        initData.properties->setProperty("IceSSL.VerifyPeer", "0");
        CommunicatorPtr comm = initialize(initData);
        PluginManagerPtr pm = comm->getPluginManager();
        pm->initializePlugins();
        ObjectPrx obj = comm->stringToProxy(factoryRef);
        test(obj);
        Test::ServerFactoryPrx fact = Test::ServerFactoryPrx::checkedCast(obj);
        Test::Properties d = createServerProps(defaultProps, defaultDir, defaultHost, p12);
#  ifdef ICE_USE_OPENSSL
        d["IceSSL.Ciphers"] = anonCiphers;
#  else
        d["IceSSL.Ciphers"] = "DH_anon_WITH_AES_256_CBC_SHA";
#  endif
        d["IceSSL.VerifyPeer"] = "0";
        Test::ServerPrx server = fact->createServer(d);
        try
        {
            server->ice_ping();
        }
        catch(const LocalException&)
        {
            test(false);
        }
        fact->destroyServer(server);
        comm->destroy();
    }
#endif
    cout << "ok" << endl;

    cout << "testing certificate verification... " << flush;
    {
        //
        // Test IceSSL.VerifyPeer=0. Client does not have a certificate,
        // and doesn't trust the server certificate.
        //
        InitializationData initData;
        initData.properties = createClientProps(defaultProps, defaultDir, defaultHost, p12, "", "");
        initData.properties->setProperty("IceSSL.VerifyPeer", "0");
        CommunicatorPtr comm = initialize(initData);
        Test::ServerFactoryPrx fact = Test::ServerFactoryPrx::checkedCast(comm->stringToProxy(factoryRef));
        test(fact);

        Test::Properties d = createServerProps(defaultProps, defaultDir, defaultHost, p12, "s_rsa_ca1", "");
        d["IceSSL.VerifyPeer"] = "0";
        Test::ServerPrx server = fact->createServer(d);
        try
        {
            server->noCert();
            test(!IceSSL::ConnectionInfoPtr::dynamicCast(server->ice_getConnection()->getInfo())->verified);
        }
        catch(const LocalException& ex)
        {
            cerr << ex << endl;
            test(false);
        }
        fact->destroyServer(server);
        comm->destroy();

        //
        // Test IceSSL.VerifyPeer=0. Client does not have a certificate,
        // but it still verifies the server's.
        //
        initData.properties = createClientProps(defaultProps, defaultDir, defaultHost, p12, "", "cacert1");
        comm = initialize(initData);
        fact = Test::ServerFactoryPrx::checkedCast(comm->stringToProxy(factoryRef));
        test(fact);

        d = createServerProps(defaultProps, defaultDir, defaultHost, p12, "s_rsa_ca1", "");
        d["IceSSL.VerifyPeer"] = "0";
        server = fact->createServer(d);
        try
        {
            server->noCert();
            test(IceSSL::ConnectionInfoPtr::dynamicCast(server->ice_getConnection()->getInfo())->verified);
        }
        catch(const LocalException&)
        {
            test(false);
        }
        fact->destroyServer(server);

        //
        // Test IceSSL.VerifyPeer=1. Client does not have a certificate.
        //
        d = createServerProps(defaultProps, defaultDir, defaultHost, p12, "s_rsa_ca1", "");
        d["IceSSL.VerifyPeer"] = "1";
        server = fact->createServer(d);
        try
        {
            server->noCert();
        }
        catch(const LocalException&)
        {
            test(false);
        }
        fact->destroyServer(server);

        //
        // Test IceSSL.VerifyPeer=2. This should fail because the client
        // does not supply a certificate.
        //
        d = createServerProps(defaultProps, defaultDir, defaultHost, p12, "s_rsa_ca1", "");
        d["IceSSL.VerifyPeer"] = "2";
        server = fact->createServer(d);
        try
        {
            server->ice_ping();
            test(false);
        }
        catch(const ProtocolException&)
        {
            // Expected, if reported as an SSL alert by the server.
        }
        catch(const ConnectionLostException&)
        {
            // Expected.
        }
        catch(const LocalException&)
        {
            test(false);
        }
        fact->destroyServer(server);
        comm->destroy();

        //
        // Test IceSSL.VerifyPeer=1. Client has a certificate.
        //
        // Provide "cacert1" to the client to verify the server
        // certificate (without this the client connection wouln't be
        // able to provide the certificate chain).
        //
        initData.properties = createClientProps(defaultProps, defaultDir, defaultHost, p12, "c_rsa_ca1", "cacert1");
        comm = initialize(initData);
        fact = Test::ServerFactoryPrx::checkedCast(comm->stringToProxy(factoryRef));
        test(fact);

        d = createServerProps(defaultProps, defaultDir, defaultHost, p12, "s_rsa_ca1", "cacert1");
        d["IceSSL.VerifyPeer"] = "1";
        server = fact->createServer(d);
        try
        {
            IceSSL::CertificatePtr clientCert = IceSSL::Certificate::load(defaultDir + "/c_rsa_ca1_pub.pem");
            server->checkCert(clientCert->getSubjectDN(), clientCert->getIssuerDN());

            //
            // Validate that we can get the connection info. Validate
            // that the certificates have the same DN.
            //
            // Validate some aspects of the Certificate class.
            //
            IceSSL::CertificatePtr serverCert = IceSSL::Certificate::load(defaultDir + "/s_rsa_ca1_pub.pem");
            test(IceSSL::Certificate::decode(serverCert->encode()) == serverCert);
            test(serverCert == serverCert);
            test(serverCert->checkValidity());
            test(!serverCert->checkValidity(IceUtil::Time::seconds(0)));

            IceSSL::CertificatePtr caCert = IceSSL::Certificate::load(defaultDir + "/cacert1.pem");
            test(caCert == caCert);
            test(caCert->checkValidity());
            test(!caCert->checkValidity(IceUtil::Time::seconds(0)));

            test(!serverCert->verify(serverCert));
            test(serverCert->verify(caCert));
            test(caCert->verify(caCert));

            info = IceSSL::NativeConnectionInfoPtr::dynamicCast(server->ice_getConnection()->getInfo());
            test(info->nativeCerts.size() == 2);
            test(info->verified);

            test(caCert == info->nativeCerts[1]);
            test(serverCert == info->nativeCerts[0]);

            test(serverCert != info->nativeCerts[1]);
            test(caCert != info->nativeCerts[0]);

            test(info->nativeCerts[0]->checkValidity() && info->nativeCerts[1]->checkValidity());
            test(!info->nativeCerts[0]->checkValidity(IceUtil::Time::seconds(0)) &&
                 !info->nativeCerts[1]->checkValidity(IceUtil::Time::seconds(0)));
            test(info->nativeCerts[0]->verify(info->nativeCerts[1]));
            test(info->nativeCerts.size() == 2 &&
                 info->nativeCerts[0]->getSubjectDN() == serverCert->getSubjectDN() &&
                 info->nativeCerts[0]->getIssuerDN() == serverCert->getIssuerDN());
        }
        catch(const LocalException& ex)
        {
            cerr << ex << endl;
            test(false);
        }
        fact->destroyServer(server);

        //
        // Test IceSSL.VerifyPeer=2. Client has a certificate.
        //
        d = createServerProps(defaultProps, defaultDir, defaultHost, p12, "s_rsa_ca1", "cacert1");
        d["IceSSL.VerifyPeer"] = "2";
        server = fact->createServer(d);
        try
        {
            IceSSL::CertificatePtr clientCert = IceSSL::Certificate::load(defaultDir + "/c_rsa_ca1_pub.pem");
            server->checkCert(clientCert->getSubjectDN(), clientCert->getIssuerDN());
        }
        catch(const LocalException&)
        {
            test(false);
        }
        fact->destroyServer(server);

        comm->destroy();

        //
        // Test IceSSL.VerifyPeer=1. This should fail because the client doesn't
        // trust the server's CA.
        //
        initData.properties = createClientProps(defaultProps, defaultDir, defaultHost, p12, "", "");
        initData.properties->setProperty("IceSSL.VerifyPeer", "1");
        comm = initialize(initData);
        fact = Test::ServerFactoryPrx::checkedCast(comm->stringToProxy(factoryRef));
        test(fact);
        d = createServerProps(defaultProps, defaultDir, defaultHost, p12, "s_rsa_ca1", "cacert1");
        d["IceSSL.VerifyPeer"] = "0";
        server = fact->createServer(d);
        try
        {
            server->ice_ping();
            test(false);
        }
        catch(const SecurityException&)
        {
            // Expected.
        }
        catch(const LocalException&)
        {
            test(false);
        }
        fact->destroyServer(server);

        comm->destroy();

        //
        // Test IceSSL.VerifyPeer=1. This should fail because the server doesn't
        // trust the client's CA.
        //
        initData.properties = createClientProps(defaultProps, defaultDir, defaultHost, p12, "c_rsa_ca2", "");
        initData.properties->setProperty("IceSSL.VerifyPeer", "0");
        comm = initialize(initData);
        fact = Test::ServerFactoryPrx::checkedCast(comm->stringToProxy(factoryRef));
        test(fact);
        d = createServerProps(defaultProps, defaultDir, defaultHost, p12, "s_rsa_ca1", "");
        d["IceSSL.VerifyPeer"] = "1";
        server = fact->createServer(d);
        try
        {
            server->ice_ping();
            test(false);
        }
        catch(const ConnectionLostException&)
        {
            // Expected.
        }
        catch(const LocalException&)
        {
           test(false);
        }
        fact->destroyServer(server);
        comm->destroy();

        //
        // This should succeed because the self signed certificate used by the server is
        // trusted.
        //
        initData.properties = createClientProps(defaultProps, defaultDir, defaultHost, p12, "", "cacert2");
        comm = initialize(initData);
        fact = Test::ServerFactoryPrx::checkedCast(comm->stringToProxy(factoryRef));
        test(fact);
        d = createServerProps(defaultProps, defaultDir, defaultHost, p12, "cacert2", "");
        d["IceSSL.VerifyPeer"] = "0";
        server = fact->createServer(d);
        try
        {
            server->ice_ping();
        }
        catch(const LocalException&)
        {
            test(false);
        }
        fact->destroyServer(server);
        comm->destroy();

        //
        // This should fail because the self signed certificate used by the server is not
        // trusted.
        //
        initData.properties = createClientProps(defaultProps, defaultDir, defaultHost, p12, "", "");
        comm = initialize(initData);
        fact = Test::ServerFactoryPrx::checkedCast(comm->stringToProxy(factoryRef));
        test(fact);
        d = createServerProps(defaultProps, defaultDir, defaultHost, p12, "cacert2", "");
        d["IceSSL.VerifyPeer"] = "0";
        server = fact->createServer(d);
        try
        {
            server->ice_ping();
            test(false);
        }
        catch(const SecurityException&)
        {
            // Expected.
        }
        catch(const LocalException&)
        {
            test(false);
        }
        fact->destroyServer(server);
        comm->destroy();

        //
        // Verify that IceSSL.CheckCertName has no effect in a server.
        //
        initData.properties = createClientProps(defaultProps, defaultDir, defaultHost, p12, "c_rsa_ca1", "cacert1");
        comm = initialize(initData);
        fact = Test::ServerFactoryPrx::checkedCast(comm->stringToProxy(factoryRef));
        test(fact);
        d = createServerProps(defaultProps, defaultDir, defaultHost, p12, "s_rsa_ca1", "cacert1");
        d["IceSSL.CheckCertName"] = "1";
        server = fact->createServer(d);
        try
        {
            server->ice_ping();
        }
        catch(const LocalException&)
        {
            test(false);
        }
        fact->destroyServer(server);
        comm->destroy();

        //
        // Test IceSSL.CheckCertName. The test certificates for the server contain "127.0.0.1"
        // as the common name or as a subject alternative name, so we only perform this test when
        // the default host is "127.0.0.1".
        //
        if(defaultHost == "127.0.0.1")
        {
            //
            // Test subject alternative name.
            //
            initData.properties = createClientProps(defaultProps, defaultDir, defaultHost, p12, "c_rsa_ca1", "cacert1");
            initData.properties->setProperty("IceSSL.CheckCertName", "1");
            comm = initialize(initData);

            fact = Test::ServerFactoryPrx::checkedCast(comm->stringToProxy(factoryRef));
            test(fact);
            d = createServerProps(defaultProps, defaultDir, defaultHost, p12, "s_rsa_ca1", "cacert1");
            server = fact->createServer(d);
            try
            {
                server->ice_ping();
            }
            catch(const LocalException&)
            {
                test(false);
            }
            fact->destroyServer(server);
            comm->destroy();

            //
            // Test common name.
            //
            initData.properties = createClientProps(defaultProps, defaultDir, defaultHost, p12, "c_rsa_ca1", "cacert1");
            initData.properties->setProperty("IceSSL.CheckCertName", "1");
            comm = initialize(initData);

            fact = Test::ServerFactoryPrx::checkedCast(comm->stringToProxy(factoryRef));
            test(fact);
            d = createServerProps(defaultProps, defaultDir, defaultHost, p12, "s_rsa_ca1_cn1", "cacert1");
            server = fact->createServer(d);
            try
            {
                server->ice_ping();
            }
            catch(const LocalException&)
            {
                test(false);
            }
            fact->destroyServer(server);
            comm->destroy();

            //
            // Test common name again. The certificate used in this test has "127.0.0.11" as its
            // common name, therefore the address "127.0.0.1" must NOT match.
            //
            initData.properties = createClientProps(defaultProps, defaultDir, defaultHost, p12, "c_rsa_ca1", "cacert1");
            initData.properties->setProperty("IceSSL.CheckCertName", "1");
            comm = initialize(initData);

            fact = Test::ServerFactoryPrx::checkedCast(comm->stringToProxy(factoryRef));
            test(fact);
            d = createServerProps(defaultProps, defaultDir, defaultHost, p12, "s_rsa_ca1_cn2", "cacert1");
            server = fact->createServer(d);
            try
            {
                server->ice_ping();
                test(false);
            }
            catch(const LocalException&)
            {
                // Expected.
            }
            fact->destroyServer(server);
            comm->destroy();
        }
    }
    cout << "ok" << endl;

    cout << "testing certificate chains... " << flush;
    {
        const char* certificates[] = {"/s_rsa_cai2.p12", 0};
        ImportCerts import(defaultDir, certificates);

        InitializationData initData;
        initData.properties = createClientProps(defaultProps, defaultDir, defaultHost, p12, "", "");
        initData.properties->setProperty("IceSSL.VerifyPeer", "0");
        CommunicatorPtr comm = initialize(initData);

        Test::ServerFactoryPrx fact = Test::ServerFactoryPrx::checkedCast(comm->stringToProxy(factoryRef));
        test(fact);

        //
        // The client can't verify the server certificate but it should
        // still provide it. "s_rsa_ca1" doesn't include the root so the
        // cert size should be 1.
        //
        Test::Properties d = createServerProps(defaultProps, defaultDir, defaultHost, p12, "s_rsa_ca1", "");
        d["IceSSL.VerifyPeer"] = "0";
        Test::ServerPrx server = fact->createServer(d);
        try
        {
            info = IceSSL::NativeConnectionInfoPtr::dynamicCast(server->ice_getConnection()->getInfo());
            test(info->nativeCerts.size() == 1);
            test(!info->verified);
        }
        catch(const Ice::LocalException&)
        {
            import.cleanup();
            test(false);
        }
        fact->destroyServer(server);

        //
        // Setting the CA for the server shouldn't change anything, it
        // shouldn't modify the cert chain sent to the client.
        //
        d = createServerProps(defaultProps, defaultDir, defaultHost, p12, "s_rsa_ca1", "cacert1");
        d["IceSSL.VerifyPeer"] = "0";
        server = fact->createServer(d);
        try
        {
            info = IceSSL::NativeConnectionInfoPtr::dynamicCast(server->ice_getConnection()->getInfo());
#ifdef ICE_USE_OPENSSL
            test(info->nativeCerts.size() == 2); // TODO: Fix OpenSSL
#else
            test(info->nativeCerts.size() == 1);
#endif
            test(!info->verified);
        }
        catch(const Ice::LocalException& ex)
        {
            cerr << ex << endl;
            import.cleanup();
            test(false);
        }
        fact->destroyServer(server);

        //
        // The client can't verify the server certificate but should
        // still provide it. "s_rsa_wroot_ca1" includes the root so
        // the cert size should be 2.
        //
        if(p12)
        {
            d = createServerProps(defaultProps, defaultDir, defaultHost, p12, "s_rsa_wroot_ca1", "");
            d["IceSSL.VerifyPeer"] = "0";
            server = fact->createServer(d);
            try
            {
                info = IceSSL::NativeConnectionInfoPtr::dynamicCast(server->ice_getConnection()->getInfo());
#ifdef ICE_USE_SCHANNEL
                test(info->nativeCerts.size() == 1); // SChannel never sends the root certificate
#else
                test(info->nativeCerts.size() == 2);
#endif
                test(!info->verified);
            }
            catch(const Ice::LocalException& ex)
            {
                cerr << ex << endl;
                import.cleanup();
                test(false);
            }
            fact->destroyServer(server);
        }
        comm->destroy();

        //
        // Now the client verifies the server certificate
        //
        initData.properties = createClientProps(defaultProps, defaultDir, defaultHost, p12, "", "cacert1");
        initData.properties->setProperty("IceSSL.VerifyPeer", "1");
        comm = initialize(initData);

        fact = Test::ServerFactoryPrx::checkedCast(comm->stringToProxy(factoryRef));
        test(fact);

        {
            Test::Properties d = createServerProps(defaultProps, defaultDir, defaultHost, p12, "s_rsa_ca1", "");
            d["IceSSL.VerifyPeer"] = "0";
            Test::ServerPrx server = fact->createServer(d);
            try
            {
                info = IceSSL::NativeConnectionInfoPtr::dynamicCast(server->ice_getConnection()->getInfo());
                test(info->nativeCerts.size() == 2);
                test(info->verified);
            }
            catch(const Ice::LocalException& ex)
            {
                cerr << ex << endl;
                import.cleanup();
                test(false);
            }
            fact->destroyServer(server);
        }
        comm->destroy();

        //
        // Try certificate with one intermediate and VerifyDepthMax=2
        //
        initData.properties = createClientProps(defaultProps, defaultDir, defaultHost, p12, "", "cacert1");
        initData.properties->setProperty("IceSSL.VerifyPeer", "1");
        initData.properties->setProperty("IceSSL.VerifyDepthMax", "2");
        comm = initialize(initData);

        fact = Test::ServerFactoryPrx::checkedCast(comm->stringToProxy(factoryRef));
        test(fact);

        {
            Test::Properties d = createServerProps(defaultProps, defaultDir, defaultHost, p12, "s_rsa_cai1", "");
            d["IceSSL.VerifyPeer"] = "0";
            Test::ServerPrx server = fact->createServer(d);
            try
            {
                IceSSL::NativeConnectionInfoPtr::dynamicCast(server->ice_getConnection()->getInfo());
                import.cleanup();
                test(false);
            }
            catch(const Ice::SecurityException&)
            {
                // Chain length too long
            }
            catch(const Ice::LocalException& ex)
            {
                cerr << ex << endl;
                import.cleanup();
                test(false);
            }
            fact->destroyServer(server);
        }
        comm->destroy();

        //
        // Try with VerifyDepthMax set to 3 (the default)
        //
        initData.properties = createClientProps(defaultProps, defaultDir, defaultHost, p12, "", "cacert1");
        initData.properties->setProperty("IceSSL.VerifyPeer", "1");
        //initData.properties->setProperty("IceSSL.VerifyDepthMax", "3");
        comm = initialize(initData);

        fact = Test::ServerFactoryPrx::checkedCast(comm->stringToProxy(factoryRef));
        test(fact);

        {
            Test::Properties d = createServerProps(defaultProps, defaultDir, defaultHost, p12, "s_rsa_cai1", "");
            d["IceSSL.VerifyPeer"] = "0";
            Test::ServerPrx server = fact->createServer(d);
            try
            {
                info = IceSSL::NativeConnectionInfoPtr::dynamicCast(server->ice_getConnection()->getInfo());
                test(info->nativeCerts.size() == 3);
                test(info->verified);
            }
            catch(const Ice::LocalException&)
            {
                import.cleanup();
                test(false);
            }
            fact->destroyServer(server);
        }

        {
            Test::Properties d = createServerProps(defaultProps, defaultDir, defaultHost, p12, "s_rsa_cai2", "");
            d["IceSSL.VerifyPeer"] = "0";
            Test::ServerPrx server = fact->createServer(d);
            try
            {
                IceSSL::NativeConnectionInfoPtr::dynamicCast(server->ice_getConnection()->getInfo());
                import.cleanup();
                test(false);
            }
            catch(const Ice::SecurityException&)
            {
                // Chain length too long
            }
            fact->destroyServer(server);
        }
        comm->destroy();

        //
        // Increase VerifyDepthMax to 4
        //
        initData.properties = createClientProps(defaultProps, defaultDir, defaultHost, p12, "", "cacert1");
        initData.properties->setProperty("IceSSL.VerifyPeer", "1");
        initData.properties->setProperty("IceSSL.VerifyDepthMax", "4");
        comm = initialize(initData);

        fact = Test::ServerFactoryPrx::checkedCast(comm->stringToProxy(factoryRef));
        test(fact);

        {
            Test::Properties d = createServerProps(defaultProps, defaultDir, defaultHost, p12, "s_rsa_cai2", "");
            d["IceSSL.VerifyPeer"] = "0";
            Test::ServerPrx server = fact->createServer(d);
            try
            {
                info = IceSSL::NativeConnectionInfoPtr::dynamicCast(server->ice_getConnection()->getInfo());
                test(info->nativeCerts.size() == 4);
                test(info->verified);
            }
            catch(const Ice::LocalException&)
            {
                import.cleanup();
                test(false);
            }
            fact->destroyServer(server);
        }

        comm->destroy();

        //
        // Increase VerifyDepthMax to 4
        //
        initData.properties = createClientProps(defaultProps, defaultDir, defaultHost, p12, "c_rsa_cai2", "cacert1");
        initData.properties->setProperty("IceSSL.VerifyPeer", "1");
        initData.properties->setProperty("IceSSL.VerifyDepthMax", "4");
        comm = initialize(initData);

        fact = Test::ServerFactoryPrx::checkedCast(comm->stringToProxy(factoryRef));
        test(fact);

        {
            Test::Properties d = createServerProps(defaultProps, defaultDir, defaultHost, p12, "s_rsa_cai2", "cacert1");
            d["IceSSL.VerifyPeer"] = "2";
            Test::ServerPrx server = fact->createServer(d);
            try
            {
                server->ice_getConnection();
                import.cleanup();
                test(false);
            }
            catch(const Ice::ProtocolException&)
            {
                // Expected
            }
            catch(const Ice::ConnectionLostException&)
            {
                // Expected
            }
            catch(const Ice::LocalException&)
            {
                import.cleanup();
                test(false);
            }
            fact->destroyServer(server);
        }

        {
            Test::Properties d = createServerProps(defaultProps, defaultDir, defaultHost, p12, "s_rsa_cai2", "cacert1");
            d["IceSSL.VerifyPeer"] = "2";
            d["IceSSL.VerifyDepthMax"] = "4";
            Test::ServerPrx server = fact->createServer(d);
            try
            {
                server->ice_getConnection();
            }
            catch(const Ice::LocalException&)
            {
                import.cleanup();
                test(false);
            }
            fact->destroyServer(server);
        }

        comm->destroy();
        import.cleanup();
    }
    cout << "ok" << endl;

    cout << "testing custom certificate verifier... " << flush;
    {
//
// Anonymous ciphers are not supported with SChannel.
//
#ifndef ICE_USE_SCHANNEL
        //
        // ADH is allowed but will not have a certificate.
        //
        InitializationData initData;
        initData.properties = createClientProps(defaultProps, defaultDir, defaultHost, p12);
#  ifdef ICE_USE_OPENSSL
        initData.properties->setProperty("IceSSL.Ciphers", anonCiphers);
#  else
        initData.properties->setProperty("IceSSL.Ciphers", "(DH_anon*)");
#  endif
        initData.properties->setProperty("IceSSL.VerifyPeer", "0");
        CommunicatorPtr comm = initialize(initData);
        IceSSL::PluginPtr plugin = IceSSL::PluginPtr::dynamicCast(comm->getPluginManager()->getPlugin("IceSSL"));
        test(plugin);
        CertificateVerifierIPtr verifier = new CertificateVerifierI;
        plugin->setCertificateVerifier(verifier);

        Test::ServerFactoryPrx fact = Test::ServerFactoryPrx::checkedCast(comm->stringToProxy(factoryRef));
        test(fact);
        Test::Properties d = createServerProps(defaultProps, defaultDir, defaultHost, p12);
#  ifdef ICE_USE_OPENSSL
        //
        // With OpenSSL 1.1.0 we need to set SECLEVEL=0 to allow ADH ciphers
        //
        string cipherSub = "ADH-";
        d["IceSSL.Ciphers"] = anonCiphers;
#  else
        string cipherSub = "DH_anon";
        d["IceSSL.Ciphers"] = "(DH_anon*)";
#  endif
        d["IceSSL.VerifyPeer"] = "0";
        Test::ServerPrx server = fact->createServer(d);
        try
        {
            server->checkCipher(cipherSub);
            info = IceSSL::NativeConnectionInfoPtr::dynamicCast(server->ice_getConnection()->getInfo());
            test(info->cipher.compare(0, cipherSub.size(), cipherSub) == 0);
        }
        catch(const LocalException&)
        {
            test(false);
        }
        test(verifier->invoked());
        test(!verifier->hadCert());

        //
        // Have the verifier return false. Close the connection explicitly
        // to force a new connection to be established.
        //
        verifier->reset();
        verifier->returnValue(false);
        server->ice_getConnection()->close(false);
        try
        {
            server->ice_ping();
            test(false);
        }
        catch(const SecurityException&)
        {
            // Expected.
        }
        catch(const LocalException&)
        {
            test(false);
        }
        test(verifier->invoked());
        test(!verifier->hadCert());

        fact->destroyServer(server);
        comm->destroy();
#endif
    }
    {
        //
        // Verify that a server certificate is present.
        //
        InitializationData initData;
        initData.properties = createClientProps(defaultProps, defaultDir, defaultHost, p12, "c_rsa_ca1", "cacert1");
        initData.properties->setProperty("IceSSL.VerifyPeer", "0");
        CommunicatorPtr comm = initialize(initData);
        IceSSL::PluginPtr plugin = IceSSL::PluginPtr::dynamicCast(comm->getPluginManager()->getPlugin("IceSSL"));
        test(plugin);
        CertificateVerifierIPtr verifier = new CertificateVerifierI;
        plugin->setCertificateVerifier(verifier);

        Test::ServerFactoryPrx fact = Test::ServerFactoryPrx::checkedCast(comm->stringToProxy(factoryRef));
        test(fact);
        Test::Properties d = createServerProps(defaultProps, defaultDir, defaultHost, p12, "s_rsa_ca1", "cacert1");
        d["IceSSL.VerifyPeer"] = "2";
        Test::ServerPrx server = fact->createServer(d);
        try
        {
            server->ice_ping();
        }
        catch(const LocalException&)
        {
            test(false);
        }
        test(verifier->invoked());
        test(verifier->hadCert());
        fact->destroyServer(server);
        comm->destroy();
    }
    cout << "ok" << endl;


    cout << "testing protocols... " << flush;
    {
#ifndef ICE_USE_SECURE_TRANSPORT
        //
        // This should fail because the client and server have no protocol
        // in common.
        //
        InitializationData initData;
        initData.properties = createClientProps(defaultProps, defaultDir, defaultHost, p12, "c_rsa_ca1", "cacert1");
        initData.properties->setProperty("IceSSL.VerifyPeer", "0");
        initData.properties->setProperty("IceSSL.Protocols", "tls1_1");
        CommunicatorPtr comm = initialize(initData);

        Test::ServerFactoryPrx fact = Test::ServerFactoryPrx::checkedCast(comm->stringToProxy(factoryRef));
        test(fact);
        Test::Properties d = createServerProps(defaultProps, defaultDir, defaultHost, p12, "s_rsa_ca1", "cacert1");
        d["IceSSL.VerifyPeer"] = "0";
        d["IceSSL.Protocols"] = "tls1_2";
        Test::ServerPrx server = fact->createServer(d);
        try
        {
            server->ice_ping();
            test(false);
        }
        catch(const ProtocolException&)
        {
            // Expected on some platforms.
        }
        catch(const ConnectionLostException&)
        {
            // Expected on some platforms.
        }
        catch(const LocalException&)
        {
            test(false);
        }
        fact->destroyServer(server);
        comm->destroy();

        //
        // This should succeed.
        //
        comm = initialize(initData);
        fact = Test::ServerFactoryPrx::checkedCast(comm->stringToProxy(factoryRef));
        test(fact);
        d = createServerProps(defaultProps, defaultDir, defaultHost, p12, "s_rsa_ca1", "cacert1");
        d["IceSSL.VerifyPeer"] = "0";
        d["IceSSL.Protocols"] = "tls1_1, tls1_2";
        server = fact->createServer(d);
        try
        {
            server->ice_ping();
        }
        catch(const LocalException& ex)
        {
            //
            // OpenSSL < 1.0 doesn't support tls 1.1 so it will also fail, we ignore in this
            // case.
            //
#if defined(ICE_USE_SCHANNEL) || (defined(OPENSSL_VERSION_NUMBER) && OPENSSL_VERSION_NUMBER >= 0x10000000L)
            cerr << ex << endl;
            test(false);
#endif
        }
        fact->destroyServer(server);
        comm->destroy();

        //
        // This should fail because the client only accept SSLv3 and the server
        // use the default protocol set that disables SSLv3
        //
        {
            InitializationData initData;
            initData.properties = createClientProps(defaultProps, defaultDir, defaultHost, p12, "c_rsa_ca1", "cacert1");
            initData.properties->setProperty("IceSSL.VerifyPeer", "0");
            initData.properties->setProperty("IceSSL.Protocols", "ssl3");
            CommunicatorPtr comm = initialize(initData);

            Test::ServerFactoryPrx fact = Test::ServerFactoryPrx::checkedCast(comm->stringToProxy(factoryRef));
            test(fact);
            Test::Properties d = createServerProps(defaultProps, defaultDir, defaultHost, p12, "s_rsa_ca1", "cacert1");
            d["IceSSL.VerifyPeer"] = "0";
            Test::ServerPrx server = fact->createServer(d);
            try
            {
                server->ice_ping();
                test(false);
            }
            catch(const ProtocolException&)
            {
                // Expected on some platforms.
            }
            catch(const ConnectionLostException&)
            {
                // Expected on some platforms.
            }
            catch(const LocalException&)
            {
                test(false);
            }
            fact->destroyServer(server);
            comm->destroy();
        }

	//
	// SSLv3 is now disabled by default with some SSL implementations.
	//
        // //
        // // This should success because both have SSLv3 enabled
        // //
        // {
        //     InitializationData initData;
        //     initData.properties = createClientProps(defaultProps, defaultDir, defaultHost, p12, "", "cacert1");
        //     initData.properties->setProperty("IceSSL.Protocols", "ssl3");
        //     CommunicatorPtr comm = initialize(initData);

        //     Test::ServerFactoryPrx fact = Test::ServerFactoryPrx::checkedCast(comm->stringToProxy(factoryRef));
        //     test(fact);
        //     Test::Properties d = createServerProps(defaultProps, defaultDir, defaultHost, p12, "s_rsa_ca1", "");
        //     d["IceSSL.VerifyPeer"] = "0";
        //     d["IceSSL.Protocols"] = "ssl3, tls, tls1_1, tls1_2";
        //     Test::ServerPrx server = fact->createServer(d);
        //     try
        //     {
        //         server->ice_ping();
        //     }
        //     catch(const LocalException& ex)
        //     {
	//         test(false);
        //     }
        //     fact->destroyServer(server);
        //     comm->destroy();
        // }
#else
        //
        // In OS X we don't support IceSSL.Protocols as secure transport doesn't allow to set the enabled protocols
        // instead we use IceSSL.ProtocolVersionMax IceSSL.ProtocolVersionMin to set the maximun and minimum
        // enabled protocol versions. See the test bellow.
        //

        //
        // This should fail because the client and server have no protocol
        // in common.
        //
        InitializationData initData;
        initData.properties = createClientProps(defaultProps, defaultDir, defaultHost, p12);
        initData.properties->setProperty("IceSSL.Ciphers", "(DH_anon*)");
        initData.properties->setProperty("IceSSL.VerifyPeer", "0");
        initData.properties->setProperty("IceSSL.ProtocolVersionMax", "tls1");
        initData.properties->setProperty("IceSSL.ProtocolVersionMin", "tls1");
        CommunicatorPtr comm = initialize(initData);
        Test::ServerFactoryPrx fact = Test::ServerFactoryPrx::checkedCast(comm->stringToProxy(factoryRef));
        test(fact);
        Test::Properties d = createServerProps(defaultProps, defaultDir, defaultHost, p12);
        d["IceSSL.Ciphers"] = "(DH_anon*)";
        d["IceSSL.VerifyPeer"] = "0";
        d["IceSSL.ProtocolVersionMax"] = "tls1_2";
        d["IceSSL.ProtocolVersionMin"] = "tls1_2";
        Test::ServerPrx server = fact->createServer(d);
        try
        {
            server->ice_ping();
            test(false);
        }
        catch(const ProtocolException&)
        {
            // Expected on some platforms.
        }
        catch(const ConnectionLostException&)
        {
            // Expected on some platforms.
        }
        catch(const LocalException&)
        {
            test(false);
        }
        fact->destroyServer(server);
        comm->destroy();

        //
        // This should succeed.
        //
        comm = initialize(initData);
        fact = Test::ServerFactoryPrx::checkedCast(comm->stringToProxy(factoryRef));
        test(fact);
        d = createServerProps(defaultProps, defaultDir, defaultHost, p12);
        d["IceSSL.Ciphers"] = "(DH_anon*)";
        d["IceSSL.VerifyPeer"] = "0";
        d["IceSSL.ProtocolVersionMax"] = "tls1";
        d["IceSSL.ProtocolVersionMin"] = "ssl3";
        server = fact->createServer(d);
        try
        {
            server->ice_ping();
        }
        catch(const LocalException&)
        {
            test(false);
        }
        fact->destroyServer(server);
        comm->destroy();

        //
        // This should fail because the client only accept SSLv3 and the server
        // use the default protocol set that disables SSLv3
        //
        {
            InitializationData initData;
            initData.properties = createClientProps(defaultProps, defaultDir, defaultHost, p12, "c_rsa_ca1", "cacert1");
            initData.properties->setProperty("IceSSL.VerifyPeer", "0");
            initData.properties->setProperty("IceSSL.ProtocolVersionMin", "ssl3");
            initData.properties->setProperty("IceSSL.ProtocolVersionMax", "ssl3");
            CommunicatorPtr comm = initialize(initData);

            Test::ServerFactoryPrx fact = Test::ServerFactoryPrx::checkedCast(comm->stringToProxy(factoryRef));
            test(fact);
            Test::Properties d = createServerProps(defaultProps, defaultDir, defaultHost, p12, "s_rsa_ca1", "cacert1");
            d["IceSSL.VerifyPeer"] = "0";
            Test::ServerPrx server = fact->createServer(d);
            try
            {
                server->ice_ping();
                test(false);
            }
            catch(const ProtocolException&)
            {
                // Expected on some platforms.
            }
            catch(const ConnectionLostException&)
            {
                // Expected on some platforms.
            }
            catch(const LocalException&)
            {
                test(false);
            }
            fact->destroyServer(server);
            comm->destroy();
        }

        //
        // This should succeed because both have SSLv3 enabled
        //
        {
            InitializationData initData;
            initData.properties = createClientProps(defaultProps, defaultDir, defaultHost, p12, "c_rsa_ca1", "cacert1");
            initData.properties->setProperty("IceSSL.VerifyPeer", "0");
            initData.properties->setProperty("IceSSL.ProtocolVersionMin", "ssl3");
            initData.properties->setProperty("IceSSL.ProtocolVersionMax", "ssl3");
            CommunicatorPtr comm = initialize(initData);

            Test::ServerFactoryPrx fact = Test::ServerFactoryPrx::checkedCast(comm->stringToProxy(factoryRef));
            test(fact);
            Test::Properties d = createServerProps(defaultProps, defaultDir, defaultHost, p12, "s_rsa_ca1", "cacert1");
            d["IceSSL.VerifyPeer"] = "0";
            d["IceSSL.ProtocolVersionMin"] = "ssl3";
            Test::ServerPrx server = fact->createServer(d);
            try
            {
                // OS X 10.11 versions prior to 10.11.2 will throw an exception as SSLv3 is totally disabled.
                server->ice_ping();
            }
            catch(const LocalException&)
            {
                test(false);
            }
            fact->destroyServer(server);
            comm->destroy();
        }
#endif
    }
    cout << "ok" << endl;

    cout << "testing expired certificates... " << flush;
    {
        //
        // This should fail because the server's certificate is expired.
        //
        {
            IceSSL::CertificatePtr cert = IceSSL::Certificate::load(defaultDir + "/s_rsa_ca1_exp_pub.pem");
            test(!cert->checkValidity());
        }

        InitializationData initData;
        initData.properties = createClientProps(defaultProps, defaultDir, defaultHost, p12, "c_rsa_ca1", "cacert1");
        CommunicatorPtr comm = initialize(initData);
        Test::ServerFactoryPrx fact = Test::ServerFactoryPrx::checkedCast(comm->stringToProxy(factoryRef));
        test(fact);
        Test::Properties d = createServerProps(defaultProps, defaultDir, defaultHost, p12, "s_rsa_ca1_exp", "cacert1");
        Test::ServerPrx server = fact->createServer(d);
        try
        {
            server->ice_ping();
            test(false);
        }
        catch(const SecurityException&)
        {
            // Expected.
        }
        catch(const LocalException& ex)
        {
            cerr << ex << endl;
            test(false);
        }
        fact->destroyServer(server);
        comm->destroy();

        //
        // This should fail because the client's certificate is expired.
        //
        {
            IceSSL::CertificatePtr cert = IceSSL::Certificate::load(defaultDir + "/c_rsa_ca1_exp_pub.pem");
            test(!cert->checkValidity());
        }

        initData.properties = createClientProps(defaultProps, defaultDir, defaultHost, p12, "c_rsa_ca1_exp", "cacert1");
        comm = initialize(initData);
        fact = Test::ServerFactoryPrx::checkedCast(comm->stringToProxy(factoryRef));
        test(fact);
        d = createServerProps(defaultProps, defaultDir, defaultHost, p12, "s_rsa_ca1", "cacert1");
        server = fact->createServer(d);
        try
        {
            server->ice_ping();
            test(false);
        }
        catch(const ConnectionLostException&)
        {
            // Expected.
        }
        catch(const LocalException& ex)
        {
            cerr << ex << endl;
            test(false);
        }
        fact->destroyServer(server);
        comm->destroy();
    }
    cout << "ok" << endl;

#ifdef ICE_USE_OPENSSL
    cout << "testing CA certificate directory... " << flush;
    {
        //
        // Don't specify CAs explicitly; we let OpenSSL find the CA
        // certificate in the default directory.
        //
        InitializationData initData;
        initData.properties = createClientProps(defaultProps, defaultDir, defaultHost, p12, "c_rsa_ca1", "");
        initData.properties->setProperty("IceSSL.CAs", defaultDir);
        CommunicatorPtr comm = initialize(initData);
        Test::ServerFactoryPrx fact = Test::ServerFactoryPrx::checkedCast(comm->stringToProxy(factoryRef));
        test(fact);
        Test::Properties d = createServerProps(defaultProps, defaultDir, defaultHost, p12, "s_rsa_ca1", "");
        d["IceSSL.CAs"] = defaultDir;
        Test::ServerPrx server = fact->createServer(d);
        try
        {
            server->ice_ping();
        }
        catch(const LocalException& ex)
        {
            cerr << ex << endl;
            test(false);
        }
        fact->destroyServer(server);
        comm->destroy();
    }
    cout << "ok" << endl;
#endif

    cout << "testing multiple CA certificates... " << flush;
    {
        InitializationData initData;
        initData.properties = createClientProps(defaultProps, defaultDir, defaultHost, p12, "c_rsa_ca1", "cacerts");
        CommunicatorPtr comm = initialize(initData);
        Test::ServerFactoryPrx fact = Test::ServerFactoryPrx::checkedCast(comm->stringToProxy(factoryRef));
        test(fact);
        Test::Properties d = createServerProps(defaultProps, defaultDir, defaultHost, p12, "s_rsa_ca2", "cacerts");
        d["IceSSL.VerifyPeer"] = "2";
        Test::ServerPrx server = fact->createServer(d);
        try
        {
            server->ice_ping();
        }
        catch(const Ice::LocalException&)
        {
            test(false);
        }
        fact->destroyServer(server);
        comm->destroy();
    }
    cout << "ok" << endl;

#ifndef ICE_USE_OPENSSL
    cout << "testing DER CA certificate... " << flush;
    {
        InitializationData initData;
        initData.properties = createClientProps(defaultProps, defaultDir, defaultHost, p12, "c_rsa_ca1", "");
        initData.properties->setProperty("IceSSL.CAs", "cacert1.der");
        CommunicatorPtr comm = initialize(initData);
        Test::ServerFactoryPrx fact = Test::ServerFactoryPrx::checkedCast(comm->stringToProxy(factoryRef));
        test(fact);
        Test::Properties d = createServerProps(defaultProps, defaultDir, defaultHost, p12, "s_rsa_ca1", "");
        d["IceSSL.VerifyPeer"] = "2";
        d["IceSSL.CAs"] = "cacert1.der";
        Test::ServerPrx server = fact->createServer(d);
        try
        {
            server->ice_ping();
        }
        catch(const Ice::LocalException&)
        {
            test(false);
        }
        fact->destroyServer(server);
        comm->destroy();
    }
    cout << "ok" << endl;
#endif

    //
    // SChannel doesn't support PEM Password protected certificates certificates
    //
#ifdef ICE_USE_SCHANNEL
    if(p12)
    {
#endif
    cout << "testing password prompt... " << flush;
    {
        //
        // Use the correct password.
        //
        InitializationData initData;
        initData.properties = createClientProps(defaultProps, defaultDir, defaultHost, p12, "c_rsa_pass_ca1","cacert1");
        initData.properties->setProperty("IceSSL.Password", ""); // Clear the password

        initData.properties->setProperty("Ice.InitPlugins", "0");
        CommunicatorPtr comm = initialize(initData);
        PluginManagerPtr pm = comm->getPluginManager();
        IceSSL::PluginPtr plugin = IceSSL::PluginPtr::dynamicCast(pm->getPlugin("IceSSL"));
        test(plugin);
        PasswordPromptIPtr prompt = new PasswordPromptI("client");
        plugin->setPasswordPrompt(prompt);
        pm->initializePlugins();
        test(prompt->count() == 1);
        Test::ServerFactoryPrx fact = Test::ServerFactoryPrx::checkedCast(comm->stringToProxy(factoryRef));
        test(fact);
        Test::Properties d = createServerProps(defaultProps, defaultDir, defaultHost, p12, "s_rsa_ca1", "cacert1");
        Test::ServerPrx server = fact->createServer(d);
        try
        {
            server->ice_ping();
        }
        catch(const LocalException& ex)
        {
            cerr << ex << endl;
            test(false);
        }
        fact->destroyServer(server);
        comm->destroy();

        //
        // Use an incorrect password and check that retries are attempted.
        //
        initData.properties = createClientProps(defaultProps, defaultDir, defaultHost, p12, "c_rsa_pass_ca1","cacert1");
        initData.properties->setProperty("IceSSL.Password", ""); // Clear password
        initData.properties->setProperty("IceSSL.PasswordRetryMax", "4");
        initData.properties->setProperty("Ice.InitPlugins", "0");
        comm = initialize(initData);
        pm = comm->getPluginManager();
        plugin = IceSSL::PluginPtr::dynamicCast(pm->getPlugin("IceSSL"));
        test(plugin);
        prompt = new PasswordPromptI("invalid");
        plugin->setPasswordPrompt(prompt);
        try
        {
            pm->initializePlugins();
            test(false);
        }
        catch(const PluginInitializationException&)
        {
            // Expected.
        }
        catch(const LocalException& ex)
        {
            cerr << ex << endl;
            test(false);
        }
        test(prompt->count() == 4);
        comm->destroy();
    }
    cout << "ok" << endl;
#ifdef ICE_USE_SCHANNEL
    }
#endif
    cout << "testing ciphers... " << flush;
#ifndef ICE_USE_SCHANNEL
    {
        //
        // The server has a certificate but the client doesn't. They should
        // negotiate to use ADH since we explicitly enable it.
        //
        InitializationData initData;
        initData.properties = createClientProps(defaultProps, defaultDir, defaultHost, p12);
#  ifdef ICE_USE_OPENSSL
        initData.properties->setProperty("IceSSL.Ciphers", anonCiphers);
#  else
        initData.properties->setProperty("IceSSL.Ciphers", "(DH_anon*)");
#  endif
        CommunicatorPtr comm = initialize(initData);
        Test::ServerFactoryPrx fact = Test::ServerFactoryPrx::checkedCast(comm->stringToProxy(factoryRef));
        test(fact);
        Test::Properties d = createServerProps(defaultProps, defaultDir, defaultHost, p12, "s_rsa_ca1", "cacert1");
#  ifdef ICE_USE_OPENSSL
        //
        // With OpenSSL 1.1.0 we need to set SECLEVEL=0 to allow ADH ciphers
        //
        string cipherSub = "ADH-";
        d["IceSSL.Ciphers"] = "RSA:" + anonCiphers;
#  else
        string cipherSub = "DH_";
        d["IceSSL.Ciphers"] = "(RSA_*) (DH_anon*)";
#  endif
        d["IceSSL.VerifyPeer"] = "1";
        Test::ServerPrx server = fact->createServer(d);
        try
        {
            server->checkCipher(cipherSub);
            info = IceSSL::NativeConnectionInfoPtr::dynamicCast(server->ice_getConnection()->getInfo());
            test(info->cipher.compare(0, cipherSub.size(), cipherSub) == 0);
        }
        catch(const LocalException& ex)
        {
#  ifndef ICE_USE_SECURE_TRANSPORT
            //
            // OS X 10.10 bug the handshake fails attempting client auth
            // with anon cipher.
            //
            cerr << ex << endl;
            test(false);
#  endif
        }
        fact->destroyServer(server);
        comm->destroy();
    }

    //
    // This should fail because we disabled all anonymous ciphers and the server doesn't
    // provide a certificate.
    //
    InitializationData initData;
    initData.properties = createClientProps(defaultProps, defaultDir, defaultHost, p12);
#  ifdef ICE_USE_OPENSSL
    initData.properties->setProperty("IceSSL.Ciphers", "ALL:!ADH");
#  else
    initData.properties->setProperty("IceSSL.Ciphers", "ALL !(DH_anon*)");
#  endif
    CommunicatorPtr comm = initialize(initData);
    Test::ServerFactoryPrx fact = Test::ServerFactoryPrx::checkedCast(comm->stringToProxy(factoryRef));
    test(fact);
    Test::Properties d = createServerProps(defaultProps, defaultDir, defaultHost, p12);
    d["IceSSL.VerifyPeer"] = "0";
    Test::ServerPrx server = fact->createServer(d);
    try
    {
        server->ice_ping();
        test(false);
    }
    catch(const ProtocolException&)
    {
        // Expected
    }
    catch(const ConnectionLostException&)
    {
        // Expected
    }
    catch(const LocalException& ex)
    {
        cerr << ex << endl;
        test(false);
    }
    fact->destroyServer(server);
    comm->destroy();

#  ifdef ICE_USE_SECURE_TRANSPORT
    {
        //
        // This should fail because the client disabled all ciphers.
        //
        InitializationData initData;
        initData.properties = createClientProps(defaultProps, defaultDir, defaultHost, p12, "c_rsa_ca1", "cacert1");
        initData.properties->setProperty("IceSSL.Ciphers", "NONE");
        try
        {
            CommunicatorPtr comm = initialize(initData);
            test(false);
        }
        catch(const Ice::PluginInitializationException&)
        {
            //Expected when disabled all cipher suites.
        }
        catch(const Ice::LocalException& ex)
        {
            cerr << ex << endl;
            test(false);
        }
    }
    {
        //
        // Test IceSSL.DHParams
        //
        InitializationData initData;
        initData.properties = createClientProps(defaultProps, defaultDir, defaultHost, p12);
        initData.properties->setProperty("IceSSL.Ciphers", "(DH_anon*)");
        CommunicatorPtr comm = initialize(initData);
        Test::ServerFactoryPrx fact = Test::ServerFactoryPrx::checkedCast(comm->stringToProxy(factoryRef));
        test(fact);
        Test::Properties d = createServerProps(defaultProps, defaultDir, defaultHost, p12);
        d["IceSSL.Ciphers"] = "(DH_anon*)";
        d["IceSSL.DHParams"] = "dh_params512.der";
        d["IceSSL.VerifyPeer"] = "0";
        Test::ServerPrx server = fact->createServer(d);
        try
        {
            server->checkCipher("DH_anon");
        }
        catch(const LocalException& ex)
        {
            if(!isElCapitanOrGreater) // DH params too weak for El Capitan
            {
                cerr << ex << endl;
                test(false);
            }
        }
        fact->destroyServer(server);
        comm->destroy();
    }

    {
        //
        // Test IceSSL.DHParams
        //
        InitializationData initData;
        initData.properties = createClientProps(defaultProps, defaultDir, defaultHost, p12);
        initData.properties->setProperty("IceSSL.Ciphers", "(DH_anon*)");
        CommunicatorPtr comm = initialize(initData);
        Test::ServerFactoryPrx fact = Test::ServerFactoryPrx::checkedCast(comm->stringToProxy(factoryRef));
        test(fact);
        Test::Properties d = createServerProps(defaultProps, defaultDir, defaultHost, p12);
        d["IceSSL.Ciphers"] = "(DH_anon*)";
        d["IceSSL.DHParams"] = "dh_params1024.der";
        d["IceSSL.VerifyPeer"] = "0";
        Test::ServerPrx server = fact->createServer(d);
        try
        {
            server->checkCipher("DH_anon");
        }
        catch(const LocalException& ex)
        {
            cerr << ex << endl;
            test(false);
        }
        fact->destroyServer(server);
        comm->destroy();
    }
#  endif
#else // SChannel ciphers
    {
        //
        // Client and server should negotiate to use 3DES as it is enabled in both.
        //
        InitializationData initData;
        initData.properties = createClientProps(defaultProps, defaultDir, defaultHost, p12, "c_rsa_ca1", "cacert1");
        initData.properties->setProperty("IceSSL.Ciphers", "3DES");

        CommunicatorPtr comm = initialize(initData);
        Test::ServerFactoryPrx fact = Test::ServerFactoryPrx::checkedCast(comm->stringToProxy(factoryRef));
        test(fact);

        Test::Properties d = createServerProps(defaultProps, defaultDir, defaultHost, p12, "s_rsa_ca1", "cacert1");
        d["IceSSL.Ciphers"] = "3DES AES_256";

        Test::ServerPrx server = fact->createServer(d);
        try
        {
            server->checkCipher("3DES");
            info = IceSSL::NativeConnectionInfoPtr::dynamicCast(server->ice_getConnection()->getInfo());
            test(info->cipher.compare(0, 4, "3DES") == 0);
        }
        catch(const LocalException& ex)
        {
            cerr << ex << endl;
            test(false);
        }
        fact->destroyServer(server);
        comm->destroy();
    }
    {
        //
        // Client and server doesn't enable a common cipher negotiate to use 3DES as it is enabled in both.
        //
        InitializationData initData;
        initData.properties = createClientProps(defaultProps, defaultDir, defaultHost, p12, "c_rsa_ca1", "cacert1");
        initData.properties->setProperty("IceSSL.Ciphers", "3DES");

        CommunicatorPtr comm = initialize(initData);
        Test::ServerFactoryPrx fact = Test::ServerFactoryPrx::checkedCast(comm->stringToProxy(factoryRef));
        test(fact);

        Test::Properties d = createServerProps(defaultProps, defaultDir, defaultHost, p12, "s_rsa_ca1", "cacert1");
        d["IceSSL.Ciphers"] = "AES_256";
        Test::ServerPrx server = fact->createServer(d);
        try
        {
            server->checkCipher("3DES");
            test(false);
        }
        catch(const Ice::ConnectionLostException&)
        {
            //expected
        }
        catch(const LocalException& ex)
        {
            cerr << ex << endl;
            test(false);
        }
        fact->destroyServer(server);
        comm->destroy();
    }
#endif

    //
    // No DSA support in Secure Transport / AIX 7.1
    //
#if !defined(ICE_USE_SECURE_TRANSPORT) && !defined(_AIX)
    {

    //
    // DSA PEM certificates are not supported with SChannel.
    //
#   ifdef ICE_USE_SCHANNEL
    if(p12)
    {
#   endif
        //
        // Configure a server with RSA and DSA certificates.
        //
        // First try a client with a DSA certificate.
        //
        InitializationData initData;
        initData.properties = createClientProps(defaultProps, defaultDir, defaultHost, p12, "c_dsa_ca1", "cacert1");
        initData.properties->setProperty("IceSSL.Ciphers", "DHE:DSS");
        CommunicatorPtr comm = initialize(initData);
        Test::ServerFactoryPrx fact = Test::ServerFactoryPrx::checkedCast(comm->stringToProxy(factoryRef));
        test(fact);
        Test::Properties d = createServerProps(defaultProps, defaultDir, defaultHost, p12, "s_dsa_ca1", "cacert1");
        d["IceSSL.Ciphers"] = "DHE:DSS";
        d["IceSSL.VerifyPeer"] = "1";

        Test::ServerPrx server = fact->createServer(d);
        try
        {
            server->ice_ping();
        }
        catch(const LocalException&)
        {
            test(false);
        }
        fact->destroyServer(server);
        comm->destroy();
        //
        // Next try a client with an RSA certificate.
        //
        initData.properties = createClientProps(defaultProps, defaultDir, defaultHost, p12, "c_rsa_ca1", "cacert1");
        comm = initialize(initData);
        fact = Test::ServerFactoryPrx::checkedCast(comm->stringToProxy(factoryRef));
        test(fact);
        d = createServerProps(defaultProps, defaultDir, defaultHost, p12, "", "cacert1");
        if(p12)
        {
            d["IceSSL.CertFile"] = "s_rsa_ca1.p12" + sep + "s_dsa_ca1.p12";
        }
        else
        {
            d["IceSSL.CertFile"] = "s_rsa_ca1_pub.pem" + sep + "s_dsa_ca1_pub.pem";
            d["IceSSL.KeyFile"] = "s_rsa_ca1_priv.pem" + sep + "s_dsa_ca1_priv.pem";
        }
        d["IceSSL.Ciphers"] = "DEFAULT:DSS";
        d["IceSSL.VerifyPeer"] = "1";
        server = fact->createServer(d);

        try
        {
            server->ice_ping();
        }
        catch(const LocalException&)
        {
            test(false);
        }
        fact->destroyServer(server);
        comm->destroy();
#   ifdef ICE_USE_SCHANNEL
    }
#   endif

#   ifndef ICE_USE_SCHANNEL
        //
        // Next try a client with ADH. This should fail.
        //
        initData.properties = createClientProps(defaultProps, defaultDir, defaultHost, p12);
        initData.properties->setProperty("IceSSL.Ciphers", "ADH");
        comm = initialize(initData);
        fact = Test::ServerFactoryPrx::checkedCast(comm->stringToProxy(factoryRef));
        test(fact);
        d = createServerProps(defaultProps, defaultDir, defaultHost, p12, "", "cacert1");
        d["IceSSL.CertFile"] = "s_rsa_ca1_pub.pem" + sep + "s_dsa_ca1_pub.pem";
        d["IceSSL.KeyFile"] = "s_rsa_ca1_priv.pem" + sep + "s_dsa_ca1_priv.pem";
        d["IceSSL.Ciphers"] = "DEFAULT:DSS";
        d["IceSSL.VerifyPeer"] = "1";
        server = fact->createServer(d);
        try
        {
            server->ice_ping();
            test(false);
        }
        catch(const ProtocolException&)
        {
            // Expected.
        }
        catch(const LocalException&)
        {
            test(false);
        }
        fact->destroyServer(server);
        comm->destroy();
#   endif
    }
#   ifndef ICE_USE_SCHANNEL
    {
        //
        // Configure a server with RSA and a client with DSA. This should fail.
        //
        InitializationData initData;
        initData.properties = createClientProps(defaultProps, defaultDir, defaultHost, p12, "c_dsa_ca1", "cacert1");
        initData.properties->setProperty("IceSSL.Ciphers", "DSS");

        CommunicatorPtr comm = initialize(initData);
        Test::ServerFactoryPrx fact = Test::ServerFactoryPrx::checkedCast(comm->stringToProxy(factoryRef));
        test(fact);
        Test::Properties d = createServerProps(defaultProps, defaultDir, defaultHost, p12, "s_rsa_ca1", "cacert1");
        d["IceSSL.VerifyPeer"] = "2";

        Test::ServerPrx server = fact->createServer(d);
        try
        {
            server->ice_ping();
            test(false);
        }
        catch(const ProtocolException&)
        {
            // Expected.
        }
        catch(const ConnectionLostException&)
        {
            // Expected.
        }
        catch(const LocalException& ex)
        {
            cerr << ex << endl;
            test(false);
        }
        fact->destroyServer(server);
        comm->destroy();
    }
#   endif
#endif
    cout << "ok" << endl;

    cout << "testing IceSSL.TrustOnly... " << flush;
    {
        InitializationData initData;
        initData.properties = createClientProps(defaultProps, defaultDir, defaultHost, p12, "c_rsa_ca1", "cacert1");
        initData.properties->setProperty("IceSSL.TrustOnly", "C=US, ST=Florida, O=ZeroC\\, Inc.,"
                                         "OU=Ice, emailAddress=info@zeroc.com, CN=Server");
        CommunicatorPtr comm = initialize(initData);

        Test::ServerFactoryPrx fact = Test::ServerFactoryPrx::checkedCast(comm->stringToProxy(factoryRef));
        test(fact);
        Test::Properties d = createServerProps(defaultProps, defaultDir, defaultHost, p12, "s_rsa_ca1", "cacert1");
        Test::ServerPrx server = fact->createServer(d);
        try
        {
            server->ice_ping();
        }
        catch(const LocalException& ex)
        {
            cerr << ex << endl;
            test(false);
        }
        fact->destroyServer(server);
        comm->destroy();
    }
    {
        InitializationData initData;
        initData.properties = createClientProps(defaultProps, defaultDir, defaultHost, p12, "c_rsa_ca1", "cacert1");
        initData.properties->setProperty("IceSSL.TrustOnly", "!C=US, ST=Florida, O=ZeroC\\, Inc.,"
                                         "OU=Ice, emailAddress=info@zeroc.com, CN=Server");
        CommunicatorPtr comm = initialize(initData);

        Test::ServerFactoryPrx fact = Test::ServerFactoryPrx::checkedCast(comm->stringToProxy(factoryRef));
        test(fact);
        Test::Properties d = createServerProps(defaultProps, defaultDir, defaultHost, p12, "s_rsa_ca1", "cacert1");
        Test::ServerPrx server = fact->createServer(d);
        try
        {
            server->ice_ping();
            test(false);
        }
        catch(const LocalException&)
        {
        }
        fact->destroyServer(server);
        comm->destroy();
    }
    {
        InitializationData initData;
        initData.properties = createClientProps(defaultProps, defaultDir, defaultHost, p12, "c_rsa_ca1", "cacert1");
        initData.properties->setProperty("IceSSL.TrustOnly", "C=US, ST=Florida, O=\"ZeroC, Inc.\","
                                         "OU=Ice, emailAddress=info@zeroc.com, CN=Server");
        CommunicatorPtr comm = initialize(initData);

        Test::ServerFactoryPrx fact = Test::ServerFactoryPrx::checkedCast(comm->stringToProxy(factoryRef));
        test(fact);
        Test::Properties d = createServerProps(defaultProps, defaultDir, defaultHost, p12, "s_rsa_ca1", "cacert1");
        Test::ServerPrx server = fact->createServer(d);
        try
        {
            server->ice_ping();
        }
        catch(const LocalException&)
        {
            test(false);
        }
        fact->destroyServer(server);
        comm->destroy();
    }
    {
        InitializationData initData;
        initData.properties = createClientProps(defaultProps, defaultDir, defaultHost, p12, "c_rsa_ca1", "cacert1");
        CommunicatorPtr comm = initialize(initData);

        Test::ServerFactoryPrx fact = Test::ServerFactoryPrx::checkedCast(comm->stringToProxy(factoryRef));
        test(fact);
        Test::Properties d = createServerProps(defaultProps, defaultDir, defaultHost, p12, "s_rsa_ca1", "cacert1");
        d["IceSSL.TrustOnly"] = "C=US, ST=Florida, O=ZeroC\\, Inc., OU=Ice, emailAddress=info@zeroc.com,CN=Client";
        Test::ServerPrx server = fact->createServer(d);
        try
        {
            server->ice_ping();
        }
        catch(const LocalException&)
        {
            test(false);
        }
        fact->destroyServer(server);
        comm->destroy();
    }
    {
        InitializationData initData;
        initData.properties = createClientProps(defaultProps, defaultDir, defaultHost, p12, "c_rsa_ca1", "cacert1");
        CommunicatorPtr comm = initialize(initData);

        Test::ServerFactoryPrx fact = Test::ServerFactoryPrx::checkedCast(comm->stringToProxy(factoryRef));
        test(fact);
        Test::Properties d = createServerProps(defaultProps, defaultDir, defaultHost, p12, "s_rsa_ca1", "cacert1");
        d["IceSSL.TrustOnly"] = "!C=US, ST=Florida, O=ZeroC\\, Inc., OU=Ice, emailAddress=info@zeroc.com, CN=Client";
        Test::ServerPrx server = fact->createServer(d);
        try
        {
            server->ice_ping();
            test(false);
        }
        catch(const LocalException&)
        {
        }
        fact->destroyServer(server);
        comm->destroy();
    }
    {
        InitializationData initData;
        initData.properties = createClientProps(defaultProps, defaultDir, defaultHost, p12, "c_rsa_ca1", "cacert1");
        initData.properties->setProperty("IceSSL.TrustOnly", "CN=Server");
        CommunicatorPtr comm = initialize(initData);

        Test::ServerFactoryPrx fact = Test::ServerFactoryPrx::checkedCast(comm->stringToProxy(factoryRef));
        test(fact);
        Test::Properties d = createServerProps(defaultProps, defaultDir, defaultHost, p12, "s_rsa_ca1", "cacert1");
        Test::ServerPrx server = fact->createServer(d);
        try
        {
            server->ice_ping();
        }
        catch(const LocalException&)
        {
            test(false);
        }
        fact->destroyServer(server);
        comm->destroy();
    }
    {
        InitializationData initData;
        initData.properties = createClientProps(defaultProps, defaultDir, defaultHost, p12, "c_rsa_ca1", "cacert1");
        initData.properties->setProperty("IceSSL.TrustOnly", "!CN=Server");
        CommunicatorPtr comm = initialize(initData);

        Test::ServerFactoryPrx fact = Test::ServerFactoryPrx::checkedCast(comm->stringToProxy(factoryRef));
        test(fact);
        Test::Properties d = createServerProps(defaultProps, defaultDir, defaultHost, p12, "s_rsa_ca1", "cacert1");
        Test::ServerPrx server = fact->createServer(d);
        try
        {
            server->ice_ping();
            test(false);
        }
        catch(const LocalException&)
        {
        }
        fact->destroyServer(server);
        comm->destroy();
    }
    {
        InitializationData initData;
        initData.properties = createClientProps(defaultProps, defaultDir, defaultHost, p12, "c_rsa_ca1", "cacert1");
        CommunicatorPtr comm = initialize(initData);

        Test::ServerFactoryPrx fact = Test::ServerFactoryPrx::checkedCast(comm->stringToProxy(factoryRef));
        test(fact);
        Test::Properties d = createServerProps(defaultProps, defaultDir, defaultHost, p12, "s_rsa_ca1", "cacert1");
        d["IceSSL.TrustOnly"] = "CN=Client";
        Test::ServerPrx server = fact->createServer(d);
        try
        {
            server->ice_ping();
        }
        catch(const LocalException&)
        {
            test(false);
        }
        fact->destroyServer(server);
        comm->destroy();
    }
    {
        InitializationData initData;
        initData.properties = createClientProps(defaultProps, defaultDir, defaultHost, p12, "c_rsa_ca1", "cacert1");
        CommunicatorPtr comm = initialize(initData);

        Test::ServerFactoryPrx fact = Test::ServerFactoryPrx::checkedCast(comm->stringToProxy(factoryRef));
        test(fact);
        Test::Properties d = createServerProps(defaultProps, defaultDir, defaultHost, p12, "s_rsa_ca1", "cacert1");
        d["IceSSL.TrustOnly"] = "!CN=Client";
        Test::ServerPrx server = fact->createServer(d);
        try
        {
            server->ice_ping();
            test(false);
        }
        catch(const LocalException&)
        {
        }
        fact->destroyServer(server);
        comm->destroy();
    }
    {
        InitializationData initData;
        initData.properties = createClientProps(defaultProps, defaultDir, defaultHost, p12, "c_rsa_ca1", "cacert1");
        initData.properties->setProperty("IceSSL.TrustOnly", "CN=Client");
        CommunicatorPtr comm = initialize(initData);

        Test::ServerFactoryPrx fact = Test::ServerFactoryPrx::checkedCast(comm->stringToProxy(factoryRef));
        test(fact);
        Test::Properties d = createServerProps(defaultProps, defaultDir, defaultHost, p12, "s_rsa_ca1", "cacert1");
        Test::ServerPrx server = fact->createServer(d);
        try
        {
            server->ice_ping();
            test(false);
        }
        catch(const LocalException&)
        {
        }
        fact->destroyServer(server);
        comm->destroy();
    }
    {
        InitializationData initData;
        initData.properties = createClientProps(defaultProps, defaultDir, defaultHost, p12, "c_rsa_ca1", "cacert1");
        CommunicatorPtr comm = initialize(initData);

        Test::ServerFactoryPrx fact = Test::ServerFactoryPrx::checkedCast(comm->stringToProxy(factoryRef));
        test(fact);
        Test::Properties d = createServerProps(defaultProps, defaultDir, defaultHost, p12, "s_rsa_ca1", "cacert1");
        d["IceSSL.TrustOnly"] = "CN=Server";
        Test::ServerPrx server = fact->createServer(d);
        try
        {
            server->ice_ping();
            test(false);
        }
        catch(const LocalException&)
        {
        }
        fact->destroyServer(server);
        comm->destroy();
    }
    {
        InitializationData initData;
        initData.properties = createClientProps(defaultProps, defaultDir, defaultHost, p12, "c_rsa_ca1", "cacert1");
        initData.properties->setProperty("IceSSL.TrustOnly", "C=Canada,CN=Server");
        CommunicatorPtr comm = initialize(initData);

        Test::ServerFactoryPrx fact = Test::ServerFactoryPrx::checkedCast(comm->stringToProxy(factoryRef));
        test(fact);
        Test::Properties d = createServerProps(defaultProps, defaultDir, defaultHost, p12, "s_rsa_ca1", "cacert1");
        Test::ServerPrx server = fact->createServer(d);
        try
        {
            server->ice_ping();
            test(false);
        }
        catch(const LocalException&)
        {
        }
        fact->destroyServer(server);
        comm->destroy();
    }
    {
        InitializationData initData;
        initData.properties = createClientProps(defaultProps, defaultDir, defaultHost, p12, "c_rsa_ca1", "cacert1");
        initData.properties->setProperty("IceSSL.TrustOnly", "!C=Canada,CN=Server");
        CommunicatorPtr comm = initialize(initData);

        Test::ServerFactoryPrx fact = Test::ServerFactoryPrx::checkedCast(comm->stringToProxy(factoryRef));
        test(fact);
        Test::Properties d = createServerProps(defaultProps, defaultDir, defaultHost, p12, "s_rsa_ca1", "cacert1");
        Test::ServerPrx server = fact->createServer(d);
        try
        {
            server->ice_ping();
        }
        catch(const LocalException&)
        {
            test(false);
        }
        fact->destroyServer(server);
        comm->destroy();
    }
    {
        InitializationData initData;
        initData.properties = createClientProps(defaultProps, defaultDir, defaultHost, p12, "c_rsa_ca1", "cacert1");
        initData.properties->setProperty("IceSSL.TrustOnly", "C=Canada;CN=Server");
        CommunicatorPtr comm = initialize(initData);

        Test::ServerFactoryPrx fact = Test::ServerFactoryPrx::checkedCast(comm->stringToProxy(factoryRef));
        test(fact);
        Test::Properties d = createServerProps(defaultProps, defaultDir, defaultHost, p12, "s_rsa_ca1", "cacert1");
        Test::ServerPrx server = fact->createServer(d);
        try
        {
            server->ice_ping();
        }
        catch(const LocalException&)
        {
            test(false);
        }
        fact->destroyServer(server);
        comm->destroy();
    }
    {
        InitializationData initData;
        initData.properties = createClientProps(defaultProps, defaultDir, defaultHost, p12, "c_rsa_ca1", "cacert1");
        initData.properties->setProperty("IceSSL.TrustOnly", "!C=Canada;!CN=Server");
        CommunicatorPtr comm = initialize(initData);

        Test::ServerFactoryPrx fact = Test::ServerFactoryPrx::checkedCast(comm->stringToProxy(factoryRef));
        test(fact);
        Test::Properties d = createServerProps(defaultProps, defaultDir, defaultHost, p12, "s_rsa_ca1", "cacert1");
        Test::ServerPrx server = fact->createServer(d);
        try
        {
            server->ice_ping();
            test(false);
        }
        catch(const LocalException&)
        {
        }
        fact->destroyServer(server);
        comm->destroy();
    }
    {
        InitializationData initData;
        initData.properties = createClientProps(defaultProps, defaultDir, defaultHost, p12, "c_rsa_ca1", "cacert1");
        initData.properties->setProperty("IceSSL.TrustOnly", "!CN=Server1"); // Should not match "Server"
        CommunicatorPtr comm = initialize(initData);

        Test::ServerFactoryPrx fact = Test::ServerFactoryPrx::checkedCast(comm->stringToProxy(factoryRef));
        test(fact);
        Test::Properties d = createServerProps(defaultProps, defaultDir, defaultHost, p12, "s_rsa_ca1", "cacert1");
        Test::ServerPrx server = fact->createServer(d);
        try
        {
            server->ice_ping();
        }
        catch(const LocalException&)
        {
            test(false);
        }
        fact->destroyServer(server);
        comm->destroy();
    }
    {
        InitializationData initData;
        initData.properties = createClientProps(defaultProps, defaultDir, defaultHost, p12, "c_rsa_ca1", "cacert1");
        CommunicatorPtr comm = initialize(initData);

        Test::ServerFactoryPrx fact = Test::ServerFactoryPrx::checkedCast(comm->stringToProxy(factoryRef));
        test(fact);
        Test::Properties d = createServerProps(defaultProps, defaultDir, defaultHost, p12, "s_rsa_ca1", "cacert1");
        d["IceSSL.TrustOnly"] = "!CN=Client1"; // Should not match "Client"
        Test::ServerPrx server = fact->createServer(d);
        try
        {
            server->ice_ping();
        }
        catch(const LocalException&)
        {
            test(false);
        }
        fact->destroyServer(server);
        comm->destroy();
    }

    {
        //
        // Test rejection when client does not supply a certificate.
        //
        InitializationData initData;
        initData.properties = createClientProps(defaultProps, defaultDir, defaultHost, p12, "", "");
        initData.properties->setProperty("IceSSL.VerifyPeer", "0");
        CommunicatorPtr comm = initialize(initData);

        Test::ServerFactoryPrx fact = Test::ServerFactoryPrx::checkedCast(comm->stringToProxy(factoryRef));
        test(fact);
        Test::Properties d = createServerProps(defaultProps, defaultDir, defaultHost, p12, "s_rsa_ca1", "cacert1");
        d["IceSSL.VerifyPeer"] = "0";
        d["IceSSL.TrustOnly"] = "C=US, ST=Florida, O=ZeroC\\, Inc.,OU=Ice, emailAddress=info@zeroc.com, CN=Client";
        Test::ServerPrx server = fact->createServer(d);
        try
        {
            server->ice_ping();
            test(false);
        }
        catch(const LocalException&)
        {
        }
        fact->destroyServer(server);
        comm->destroy();
    }
    {
        //
        // Test rejection when client does not supply a certificate.
        //
        InitializationData initData;
        initData.properties = createClientProps(defaultProps, defaultDir, defaultHost, p12, "", "");
        initData.properties->setProperty("IceSSL.VerifyPeer", "0");
        CommunicatorPtr comm = initialize(initData);

        Test::ServerFactoryPrx fact = Test::ServerFactoryPrx::checkedCast(comm->stringToProxy(factoryRef));
        test(fact);
        Test::Properties d = createServerProps(defaultProps, defaultDir, defaultHost, p12, "s_rsa_ca1", "cacert1");
        d["IceSSL.TrustOnly"] = "!C=US, ST=Florida, O=ZeroC\\, Inc.,OU=Ice, emailAddress=info@zeroc.com, CN=Client";
        d["IceSSL.VerifyPeer"] = "0";
        Test::ServerPrx server = fact->createServer(d);
        try
        {
            server->ice_ping();
            test(false);
        }
        catch(const LocalException&)
        {
        }
        fact->destroyServer(server);
        comm->destroy();
    }
    {
        //
        // Rejection takes precedence (client).
        //
        InitializationData initData;
        initData.properties = createClientProps(defaultProps, defaultDir, defaultHost, p12, "c_rsa_ca1", "cacert1");
        initData.properties->setProperty("IceSSL.TrustOnly", "ST=Florida;!CN=Server;C=US");
        CommunicatorPtr comm = initialize(initData);

        Test::ServerFactoryPrx fact = Test::ServerFactoryPrx::checkedCast(comm->stringToProxy(factoryRef));
        test(fact);
        Test::Properties d = createServerProps(defaultProps, defaultDir, defaultHost, p12, "s_rsa_ca1", "cacert1");
        Test::ServerPrx server = fact->createServer(d);
        try
        {
            server->ice_ping();
            test(false);
        }
        catch(const LocalException&)
        {
        }
        fact->destroyServer(server);
        comm->destroy();
    }
    {
        //
        // Rejection takes precedence (server).
        //
        InitializationData initData;
        initData.properties = createClientProps(defaultProps, defaultDir, defaultHost, p12, "c_rsa_ca1", "cacert1");
        CommunicatorPtr comm = initialize(initData);

        Test::ServerFactoryPrx fact = Test::ServerFactoryPrx::checkedCast(comm->stringToProxy(factoryRef));
        test(fact);
        Test::Properties d = createServerProps(defaultProps, defaultDir, defaultHost, p12, "s_rsa_ca1", "cacert1");
        d["IceSSL.TrustOnly"] = "C=US;!CN=Client;ST=Florida";
        Test::ServerPrx server = fact->createServer(d);
        try
        {
            server->ice_ping();
            test(false);
        }
        catch(const LocalException&)
        {
        }
        fact->destroyServer(server);
        comm->destroy();
    }
    cout << "ok" << endl;

    cout << "testing IceSSL.TrustOnly.Client... " << flush;
    {
        InitializationData initData;
        initData.properties = createClientProps(defaultProps, defaultDir, defaultHost, p12, "c_rsa_ca1", "cacert1");
        initData.properties->setProperty("IceSSL.TrustOnly.Client", "C=US, ST=Florida, O=ZeroC\\, Inc.,"
                                         "OU=Ice, emailAddress=info@zeroc.com, CN=Server");
        CommunicatorPtr comm = initialize(initData);

        Test::ServerFactoryPrx fact = Test::ServerFactoryPrx::checkedCast(comm->stringToProxy(factoryRef));
        test(fact);
        Test::Properties d = createServerProps(defaultProps, defaultDir, defaultHost, p12, "s_rsa_ca1", "cacert1");
        // Should have no effect.
        d["IceSSL.TrustOnly.Client"] = "C=US, ST=Florida, O=ZeroC\\, Inc., OU=Ice, emailAddress=info@zeroc.com,"
                                       "CN=Server";

        Test::ServerPrx server = fact->createServer(d);
        try
        {
            server->ice_ping();
        }
        catch(const LocalException&)
        {
            test(false);
        }
        fact->destroyServer(server);
        comm->destroy();
    }
    {
        InitializationData initData;
        initData.properties = createClientProps(defaultProps, defaultDir, defaultHost, p12, "c_rsa_ca1", "cacert1");
        initData.properties->setProperty("IceSSL.TrustOnly.Client", "!C=US, ST=Florida, O=ZeroC\\, Inc.,"
                                         "OU=Ice, emailAddress=info@zeroc.com, CN=Server");
        CommunicatorPtr comm = initialize(initData);

        Test::ServerFactoryPrx fact = Test::ServerFactoryPrx::checkedCast(comm->stringToProxy(factoryRef));
        test(fact);
        Test::Properties d = createServerProps(defaultProps, defaultDir, defaultHost, p12, "s_rsa_ca1", "cacert1");
        Test::ServerPrx server = fact->createServer(d);
        try
        {
            server->ice_ping();
            test(false);
        }
        catch(const LocalException&)
        {
        }
        fact->destroyServer(server);
        comm->destroy();
    }
    {
        InitializationData initData;
        initData.properties = createClientProps(defaultProps, defaultDir, defaultHost, p12, "c_rsa_ca1", "cacert1");
        CommunicatorPtr comm = initialize(initData);

        Test::ServerFactoryPrx fact = Test::ServerFactoryPrx::checkedCast(comm->stringToProxy(factoryRef));
        test(fact);
        Test::Properties d = createServerProps(defaultProps, defaultDir, defaultHost, p12, "s_rsa_ca1", "cacert1");
        // Should have no effect.
        d["IceSSL.TrustOnly.Client"] = "!CN=Client";
        Test::ServerPrx server = fact->createServer(d);
        try
        {
            server->ice_ping();
        }
        catch(const LocalException&)
        {
            test(false);
        }
        fact->destroyServer(server);
        comm->destroy();
    }
    {
        InitializationData initData;
        initData.properties = createClientProps(defaultProps, defaultDir, defaultHost, p12, "c_rsa_ca1", "cacert1");
        initData.properties->setProperty("IceSSL.TrustOnly.Client", "CN=Client");
        CommunicatorPtr comm = initialize(initData);

        Test::ServerFactoryPrx fact = Test::ServerFactoryPrx::checkedCast(comm->stringToProxy(factoryRef));
        test(fact);
        Test::Properties d = createServerProps(defaultProps, defaultDir, defaultHost, p12, "s_rsa_ca1", "cacert1");
        Test::ServerPrx server = fact->createServer(d);
        try
        {
            server->ice_ping();
            test(false);
        }
        catch(const LocalException&)
        {
        }
        fact->destroyServer(server);
        comm->destroy();
    }
    {
        InitializationData initData;
        initData.properties = createClientProps(defaultProps, defaultDir, defaultHost, p12, "c_rsa_ca1", "cacert1");
        initData.properties->setProperty("IceSSL.TrustOnly.Client", "!CN=Client");
        CommunicatorPtr comm = initialize(initData);

        Test::ServerFactoryPrx fact = Test::ServerFactoryPrx::checkedCast(comm->stringToProxy(factoryRef));
        test(fact);
        Test::Properties d = createServerProps(defaultProps, defaultDir, defaultHost, p12, "s_rsa_ca1", "cacert1");
        Test::ServerPrx server = fact->createServer(d);
        try
        {
            server->ice_ping();
        }
        catch(const LocalException&)
        {
            test(false);
        }
        fact->destroyServer(server);
        comm->destroy();
    }
    cout << "ok" << endl;

    cout << "testing IceSSL.TrustOnly.Server... " << flush;
    {
        InitializationData initData;
        initData.properties = createClientProps(defaultProps, defaultDir, defaultHost, p12, "c_rsa_ca1", "cacert1");
        // Should have no effect.
        initData.properties->setProperty("IceSSL.TrustOnly.Server", "C=US, ST=Florida, O=ZeroC\\, Inc., OU=Ice,"
                                         "emailAddress=info@zeroc.com,CN=Client");
        CommunicatorPtr comm = initialize(initData);

        Test::ServerFactoryPrx fact = Test::ServerFactoryPrx::checkedCast(comm->stringToProxy(factoryRef));
        test(fact);
        Test::Properties d = createServerProps(defaultProps, defaultDir, defaultHost, p12, "s_rsa_ca1", "cacert1");
        d["IceSSL.TrustOnly.Server"] = "C=US, ST=Florida, O=ZeroC\\, Inc., OU=Ice, emailAddress=info@zeroc.com,"
                                       "CN=Client";

        Test::ServerPrx server = fact->createServer(d);
        try
        {
            server->ice_ping();
        }
        catch(const LocalException&)
        {
            test(false);
        }
        fact->destroyServer(server);
        comm->destroy();
    }
    {
        InitializationData initData;
        initData.properties = createClientProps(defaultProps, defaultDir, defaultHost, p12, "c_rsa_ca1", "cacert1");
        CommunicatorPtr comm = initialize(initData);

        Test::ServerFactoryPrx fact = Test::ServerFactoryPrx::checkedCast(comm->stringToProxy(factoryRef));
        test(fact);
        Test::Properties d = createServerProps(defaultProps, defaultDir, defaultHost, p12, "s_rsa_ca1", "cacert1");
        d["IceSSL.TrustOnly.Server"] =
            "!C=US, ST=Florida, O=ZeroC\\, Inc., OU=Ice, emailAddress=info@zeroc.com, CN=Client";
        Test::ServerPrx server = fact->createServer(d);
        try
        {
            server->ice_ping();
            test(false);
        }
        catch(const LocalException&)
        {
        }
        fact->destroyServer(server);
        comm->destroy();
    }
    {
        InitializationData initData;
        initData.properties = createClientProps(defaultProps, defaultDir, defaultHost, p12, "c_rsa_ca1", "cacert1");
        // Should have no effect.
        initData.properties->setProperty("IceSSL.TrustOnly.Server", "!CN=Server");
        CommunicatorPtr comm = initialize(initData);

        Test::ServerFactoryPrx fact = Test::ServerFactoryPrx::checkedCast(comm->stringToProxy(factoryRef));
        test(fact);
        Test::Properties d = createServerProps(defaultProps, defaultDir, defaultHost, p12, "s_rsa_ca1", "cacert1");
        Test::ServerPrx server = fact->createServer(d);
        try
        {
            server->ice_ping();
        }
        catch(const LocalException&)
        {
            test(false);
        }
        fact->destroyServer(server);
        comm->destroy();
    }
    {
        InitializationData initData;
        initData.properties = createClientProps(defaultProps, defaultDir, defaultHost, p12, "c_rsa_ca1", "cacert1");
        CommunicatorPtr comm = initialize(initData);

        Test::ServerFactoryPrx fact = Test::ServerFactoryPrx::checkedCast(comm->stringToProxy(factoryRef));
        test(fact);
        Test::Properties d = createServerProps(defaultProps, defaultDir, defaultHost, p12, "s_rsa_ca1", "cacert1");
        d["IceSSL.TrustOnly.Server"] = "CN=Server";
        Test::ServerPrx server = fact->createServer(d);
        try
        {
            server->ice_ping();
            test(false);
        }
        catch(const LocalException&)
        {
        }
        fact->destroyServer(server);
        comm->destroy();
    }
    {
        InitializationData initData;
        initData.properties = createClientProps(defaultProps, defaultDir, defaultHost, p12, "c_rsa_ca1", "cacert1");
        CommunicatorPtr comm = initialize(initData);

        Test::ServerFactoryPrx fact = Test::ServerFactoryPrx::checkedCast(comm->stringToProxy(factoryRef));
        test(fact);
        Test::Properties d = createServerProps(defaultProps, defaultDir, defaultHost, p12, "s_rsa_ca1", "cacert1");
        d["IceSSL.TrustOnly.Server"] = "!CN=Client";
        Test::ServerPrx server = fact->createServer(d);
        try
        {
            server->ice_ping();
            test(false);
        }
        catch(const LocalException&)
        {
        }
        fact->destroyServer(server);
        comm->destroy();
    }
    cout << "ok" << endl;

    cout << "testing IceSSL.TrustOnly.Server.<AdapterName>... " << flush;
    {
        InitializationData initData;
        initData.properties = createClientProps(defaultProps, defaultDir, defaultHost, p12, "c_rsa_ca1", "cacert1");
        CommunicatorPtr comm = initialize(initData);

        Test::ServerFactoryPrx fact = Test::ServerFactoryPrx::checkedCast(comm->stringToProxy(factoryRef));
        test(fact);
        Test::Properties d = createServerProps(defaultProps, defaultDir, defaultHost, p12, "s_rsa_ca1", "cacert1");
        d["IceSSL.TrustOnly.Server.ServerAdapter"] =
            "C=US, ST=Florida, O=ZeroC\\, Inc., OU=Ice, emailAddress=info@zeroc.com,CN=Client";
        d["IceSSL.TrustOnly.Server"] = "CN=bogus";
        Test::ServerPrx server = fact->createServer(d);
        try
        {
            server->ice_ping();
        }
        catch(const LocalException&)
        {
            test(false);
        }
        fact->destroyServer(server);
        comm->destroy();
    }
    {
        InitializationData initData;
        initData.properties = createClientProps(defaultProps, defaultDir, defaultHost, p12, "c_rsa_ca1", "cacert1");
        CommunicatorPtr comm = initialize(initData);

        Test::ServerFactoryPrx fact = Test::ServerFactoryPrx::checkedCast(comm->stringToProxy(factoryRef));
        test(fact);
        Test::Properties d = createServerProps(defaultProps, defaultDir, defaultHost, p12, "s_rsa_ca1", "cacert1");
        d["IceSSL.TrustOnly.Server.ServerAdapter"] =
            "!C=US, ST=Florida, O=ZeroC\\, Inc., OU=Ice, emailAddress=info@zeroc.com, CN=Client";
        Test::ServerPrx server = fact->createServer(d);
        try
        {
            server->ice_ping();
            test(false);
        }
        catch(const LocalException&)
        {
        }
        fact->destroyServer(server);
        comm->destroy();
    }
    {
        InitializationData initData;
        initData.properties = createClientProps(defaultProps, defaultDir, defaultHost, p12, "c_rsa_ca1", "cacert1");
        CommunicatorPtr comm = initialize(initData);

        Test::ServerFactoryPrx fact = Test::ServerFactoryPrx::checkedCast(comm->stringToProxy(factoryRef));
        test(fact);
        Test::Properties d = createServerProps(defaultProps, defaultDir, defaultHost, p12, "s_rsa_ca1", "cacert1");
        d["IceSSL.TrustOnly.Server.ServerAdapter"] = "CN=bogus";
        Test::ServerPrx server = fact->createServer(d);
        try
        {
            server->ice_ping();
            test(false);
        }
        catch(const LocalException&)
        {
        }
        fact->destroyServer(server);
        comm->destroy();
    }
    {
        InitializationData initData;
        initData.properties = createClientProps(defaultProps, defaultDir, defaultHost, p12, "c_rsa_ca1", "cacert1");
        CommunicatorPtr comm = initialize(initData);

        Test::ServerFactoryPrx fact = Test::ServerFactoryPrx::checkedCast(comm->stringToProxy(factoryRef));
        test(fact);
        Test::Properties d = createServerProps(defaultProps, defaultDir, defaultHost, p12, "s_rsa_ca1", "cacert1");
        d["IceSSL.TrustOnly.Server.ServerAdapter"] = "!CN=bogus";
        Test::ServerPrx server = fact->createServer(d);
        try
        {
            server->ice_ping();
        }
        catch(const LocalException&)
        {
            test(false);
        }
        fact->destroyServer(server);
        comm->destroy();
    }
    cout << "ok" << endl;

    {
#if defined(ICE_USE_SCHANNEL)
        cout << "testing IceSSL.FindCert... " << flush;
        const char* clientFindCertProperties[] =
        {
            "SUBJECTDN:'CN=Client, OU=Ice, O=\"ZeroC, Inc.\", L=Jupiter, S=Florida, C=US, E=info@zeroc.com'",
            "ISSUER:'ZeroC, Inc.' SUBJECT:Client SERIAL:02",
            "ISSUERDN:'CN=ZeroC Test CA 1, OU=Ice, O=\"ZeroC, Inc.\",L=Jupiter, S=Florida, C=US,E=info@zeroc.com' SUBJECT:Client",
            "THUMBPRINT:'82 30 1E 35 9E 39 C1 D0 63 0D 67 3D 12 DD D4 96 90 1E EF 54'",
            "SUBJECTKEYID:'FC 5D 4F AB F0 6C 03 11 B8 F3 68 CF 89 54 92 3F F9 79 2A 06'",
            0
        };

        const char* serverFindCertProperties[] =
        {
            "SUBJECTDN:'CN=Server, OU=Ice, O=\"ZeroC, Inc.\", L=Jupiter, S=Florida, C=US, E=info@zeroc.com'",
            "ISSUER:'ZeroC, Inc.' SUBJECT:Server SERIAL:01",
            "ISSUERDN:'CN=ZeroC Test CA 1, OU=Ice, O=\"ZeroC, Inc.\", L=Jupiter, S=Florida, C=US,E=info@zeroc.com' SUBJECT:Server",
            "THUMBPRINT:'C0 01 FF 9C C9 DA C8 0D 34 F6 2F DE 09 FB 28 0D 69 AB 78 BA'",
            "SUBJECTKEYID:'47 84 AE F9 F2 85 3D 99 30 6A 03 38 41 1A B9 EB C3 9C B5 4D'",
            0
        };

        const char* failFindCertProperties[] =
        {
            "nolabel",
            "unknownlabel:foo",
            "LABEL:",
            "SUBJECTDN:'CN = Client, E = infox@zeroc.com, OU = Ice, O = \"ZeroC, Inc.\", S = Florida, C = US'",
            "ISSUER:'ZeroC, Inc.' SUBJECT:Client SERIAL:'02 02'",
            "ISSUERDN:'E = info@zeroc.com, CN = \"ZeroC Test CA 1\", OU = Ice, O = \"ZeroC, Inc.\","
                " L = \"Palm Beach Gardens\", S = Florida, C = ES' SUBJECT:Client",
            "THUMBPRINT:'27 e0 18 c9 23 12 6c f0 5c da fa 36 5a 4c 63 5a e2 53 07 ff'",
            "SUBJECTKEYID:'a6 42 aa 17 04 41 86 56 67 e4 04 64 59 34 30 c7 4c 6b ef ff'",
            0
        };

        const char* certificates[] = {"/s_rsa_ca1.p12", "/c_rsa_ca1.p12", 0};
        ImportCerts import(defaultDir, certificates);

        for(int i = 0; clientFindCertProperties[i] != 0; i++)
        {
            InitializationData initData;
            initData.properties = createClientProps(defaultProps, defaultDir, defaultHost, p12);
            initData.properties->setProperty("IceSSL.CAs", "cacert1.pem");
            initData.properties->setProperty("IceSSL.CertStore", "My");
            initData.properties->setProperty("IceSSL.CertStoreLocation", "CurrentUser");
            initData.properties->setProperty("IceSSL.FindCert", clientFindCertProperties[i]);
            //
            // Use TrustOnly to ensure the peer has pick the expected certificate.
            //
            initData.properties->setProperty("IceSSL.TrustOnly", "CN=Server");

            CommunicatorPtr comm = initialize(initData);

            Test::ServerFactoryPrx fact = Test::ServerFactoryPrx::checkedCast(comm->stringToProxy(factoryRef));
            test(fact);
            Test::Properties d = createServerProps(defaultProps, defaultDir, defaultHost, p12, "s_rsa_ca1", "cacert1");
            d["IceSSL.CAs"] = "cacert1.pem";
            d["IceSSL.FindCert"] = serverFindCertProperties[i];
            //
            // Use TrustOnly to ensure the peer has pick the expected certificate.
            //
            d["IceSSL.TrustOnly"] = "CN=Client";

            Test::ServerPrx server = fact->createServer(d);
            try
            {
                server->ice_ping();
            }
            catch(const LocalException& ex)
            {
                cerr << ex << endl;
                import.cleanup();
                test(false);
            }
            fact->destroyServer(server);
            comm->destroy();
        }

        //
        // These must fail because the search criteria does not match any certificates.
        //
        for(int i = 0; failFindCertProperties[i] != 0; i++)
        {
            InitializationData initData;
            initData.properties = createClientProps(defaultProps, defaultDir, defaultHost, p12);
            initData.properties->setProperty("IceSSL.CAs", "cacert1.pem");
            initData.properties->setProperty("IceSSL.FindCert", failFindCertProperties[i]);
            try
            {
                CommunicatorPtr comm = initialize(initData);
                cerr << failFindCertProperties[i] << endl;
                import.cleanup();
                test(false);
            }
            catch(const PluginInitializationException&)
            {
                // expected
            }
            catch(const Ice::LocalException& ex)
            {
                cerr << ex << endl;
                import.cleanup();
                test(false);
            }
        }

        import.cleanup();

        //
        // These must fail because we have already remove the certificates.
        //
        for(int i = 0; clientFindCertProperties[i] != 0; i++)
        {
            InitializationData initData;
            initData.properties = createClientProps(defaultProps, defaultDir, defaultHost, p12);
            initData.properties->setProperty("IceSSL.CAs", "cacert1.pem");
            initData.properties->setProperty("IceSSL.FindCert", clientFindCertProperties[i]);
            try
            {
                CommunicatorPtr comm = initialize(initData);
                test(false);
            }
            catch(const PluginInitializationException&)
            {
                //expected
            }
            catch(const Ice::LocalException& ex)
            {
                cerr << ex << endl;
                test(false);
            }
        }
        cout << "ok" << endl;
#elif defined(ICE_USE_SECURE_TRANSPORT)
        cout << "testing IceSSL.FindCert... " << flush;
        const char* clientFindCertProperties[] =
        {
            "SUBJECT:Client",
            "LABEL:'Client'",
            "SUBJECTKEYID:'FC 5D 4F AB F0 6C 03 11 B8 F3 68 CF 89 54 92 3F F9 79 2A 06'",
            "SERIAL:02",
            "SERIAL:02 LABEL:Client",
            0
        };

        const char* serverFindCertProperties[] =
        {
            "SUBJECT:Server",
            "LABEL:'Server'",
            "SUBJECTKEYID:'47 84 AE F9 F2 85 3D 99 30 6A 03 38 41 1A B9 EB C3 9C B5 4D'",
            "SERIAL:01",
            "SERIAL:01 LABEL:Server",
            0
        };

        const char* failFindCertProperties[] =
        {
            "nolabel",
            "unknownlabel:foo",
            "LABEL:",
            "SUBJECT:ServerX",
            "LABEL:'ServerX'",
            "SUBJECTKEYID:'a6 42 aa 17 04 41 86 56 67 e4 04 64 59 34 30 c7 4c 6b ef ff'",
            "SERIAL:04",
            "SERIAL:04 LABEL:Client",
            0
        };

        for(int i = 0; clientFindCertProperties[i] != 0; i++)
        {
            InitializationData initData;
            initData.properties = createClientProps(defaultProps, defaultDir, defaultHost, p12);
            initData.properties->setProperty("IceSSL.CAs", "cacert1.pem");
            initData.properties->setProperty("IceSSL.Keychain", "../certs/Find.keychain");
            initData.properties->setProperty("IceSSL.KeychainPassword", "password");
            initData.properties->setProperty("IceSSL.FindCert", clientFindCertProperties[i]);
            //
            // Use TrustOnly to ensure the peer has pick the expected certificate.
            //
            initData.properties->setProperty("IceSSL.TrustOnly", "CN=Server");

            CommunicatorPtr comm = initialize(initData);

            Test::ServerFactoryPrx fact = Test::ServerFactoryPrx::checkedCast(comm->stringToProxy(factoryRef));
            test(fact);
            Test::Properties d = createServerProps(defaultProps, defaultDir, defaultHost, p12);
            d["IceSSL.CAs"] = "cacert1.pem";
            d["IceSSL.Keychain"] = "../certs/Find.keychain";
            d["IceSSL.KeychainPassword"] = "password";
            d["IceSSL.FindCert"] = serverFindCertProperties[i];
            //
            // Use TrustOnly to ensure the peer has pick the expected certificate.
            //
            d["IceSSL.TrustOnly"] = "CN=Client";

            Test::ServerPrx server = fact->createServer(d);
            try
            {
                server->ice_ping();
            }
            catch(const LocalException& ex)
            {
                cerr << ex << endl;
                test(false);
            }
            fact->destroyServer(server);
            comm->destroy();
        }

        for(int i = 0; failFindCertProperties[i] != 0; i++)
        {
            InitializationData initData;
            initData.properties = createClientProps(defaultProps, defaultDir, defaultHost, p12);
            initData.properties->setProperty("IceSSL.Keychain", "../certs/Find.keychain");
            initData.properties->setProperty("IceSSL.KeychainPassword", "password");
            initData.properties->setProperty("IceSSL.FindCert", failFindCertProperties[i]);
            try
            {
                CommunicatorPtr comm = initialize(initData);
                test(false);
            }
            catch(const PluginInitializationException&)
            {
                // expected
            }
            catch(const Ice::LocalException& ex)
            {
                cerr << ex << endl;
                test(false);
            }
        }
        cout << "ok" << endl;
#endif
    }

#ifndef _AIX
    // On AIX 6.1, the default root certificates don't validate demo.zeroc.com

    cout << "testing system CAs... " << flush;
    {
        InitializationData initData;
        initData.properties = createClientProps(defaultProps, "", defaultHost, false);
        initData.properties->setProperty("IceSSL.VerifyDepthMax", "4");
        initData.properties->setProperty("Ice.Override.Timeout", "5000"); // 5s timeout
        CommunicatorPtr comm = initialize(initData);
        Ice::ObjectPrx p = comm->stringToProxy("dummy:wss -h demo.zeroc.com -p 5064");
        try
        {
            p->ice_ping();
            test(false);
        }
        catch(const Ice::SecurityException&)
        {
            // Expected, by default we don't check for system CAs.
        }
        catch(const Ice::LocalException& ex)
        {
            cerr << "warning: unable to connect to demo.zeroc.com to check system CA:\n" << ex << endl;
        }
        comm->destroy();

        initData.properties = createClientProps(defaultProps, "", defaultHost, false);
        initData.properties->setProperty("IceSSL.VerifyDepthMax", "4");
        initData.properties->setProperty("Ice.Override.Timeout", "5000"); // 5s timeout
        initData.properties->setProperty("IceSSL.UsePlatformCAs", "1");
        comm = initialize(initData);
        p = comm->stringToProxy("dummy:wss -h demo.zeroc.com -p 5064");

        IceSSL::WSSConnectionInfoPtr info;
        try
        {
            info = IceSSL::WSSConnectionInfoPtr::dynamicCast(p->ice_getConnection()->getInfo());
            test(info->verified);
        }
        catch(const Ice::LocalException& ex)
        {
            cerr << ex << endl;
            test(false);
        }
        comm->destroy();
    }
    cout << "ok" << endl;
#endif

    if(shutdown)
    {
        factory->shutdown();
    }
}
