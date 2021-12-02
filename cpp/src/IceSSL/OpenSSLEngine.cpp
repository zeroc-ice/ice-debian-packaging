//
// Copyright (c) ZeroC, Inc. All rights reserved.
//

#include <IceSSL/Config.h>

#include <IceSSL/OpenSSLEngineF.h>
#include <IceSSL/OpenSSLEngine.h>
#include <IceSSL/OpenSSLTransceiverI.h>
#include <IceSSL/Util.h>
#include <IceSSL/TrustManager.h>

#include <Ice/Communicator.h>
#include <Ice/LocalException.h>
#include <Ice/Logger.h>
#include <Ice/LoggerUtil.h>
#include <Ice/Properties.h>

#include <IceUtil/StringUtil.h>
#include <IceUtil/Mutex.h>
#include <IceUtil/MutexPtrLock.h>
#include <IceUtil/FileUtil.h>

#include <openssl/rand.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/pkcs12.h>

#ifdef _MSC_VER
#   pragma warning(disable:4127) // conditional expression is constant
#elif defined(__GNUC__)
#  // Ignore OpenSSL 3.0 deprecation warning
#  pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif

using namespace std;
using namespace Ice;
using namespace IceSSL;

namespace
{

IceUtil::Mutex* staticMutex = 0;
int instanceCount = 0;
bool initOpenSSL = false;

#if OPENSSL_VERSION_NUMBER < 0x10100000L || defined(LIBRESSL_VERSION_NUMBER)
IceUtil::Mutex* locks = 0;
#endif

}
extern "C"
{

//
// OpenSSL 1.1.0 introduces a new thread API and removes
// the need to use a custom thread callback.
//
#if OPENSSL_VERSION_NUMBER < 0x10100000L || defined(LIBRESSL_VERSION_NUMBER)
//
// OpenSSL mutex callback.
//
void
IceSSL_opensslLockCallback(int mode, int n, const char* /*file*/, int /*line*/)
{
    assert(locks);
    if(mode & CRYPTO_LOCK)
    {
        locks[n].lock();
    }
    else
    {
        locks[n].unlock();
    }
}

//
// OpenSSL thread id callback.
//
unsigned long
IceSSL_opensslThreadIdCallback()
{
#  if defined(_WIN32)
    return static_cast<unsigned long>(GetCurrentThreadId());
#  elif defined(__FreeBSD__) || defined(__APPLE__) || defined(__osf1__)
    //
    // On some platforms, pthread_t is a pointer to a per-thread structure.
    //
    return reinterpret_cast<unsigned long>(pthread_self());
#  elif defined(__linux__) || defined(__sun) || defined(__hpux) || defined(_AIX) || defined(__GLIBC__)
    //
    // On Linux, Solaris, HP-UX and AIX, pthread_t is an integer.
    //
    return static_cast<unsigned long>(pthread_self());
#  else
#    error "Unknown platform"
#  endif
}

#endif

int
IceSSL_opensslPasswordCallback(char* buf, int size, int flag, void* userData)
{
    OpenSSL::SSLEngine* p = reinterpret_cast<OpenSSL::SSLEngine*>(userData);
    assert(p);
    string passwd = p->password(flag == 1);
    int sz = static_cast<int>(passwd.size());
    if(sz > size)
    {
        sz = size - 1;
    }
#if defined(_WIN32)
    strncpy_s(buf, size, passwd.c_str(), sz);
#else
    strncpy(buf, passwd.c_str(), sz);
#endif
    buf[sz] = '\0';

    for(string::iterator i = passwd.begin(); i != passwd.end(); ++i)
    {
        *i = '\0';
    }

    return sz;
}

#ifndef OPENSSL_NO_DH
DH*
IceSSL_opensslDHCallback(SSL* ssl, int /*isExport*/, int keyLength)
{
#  if OPENSSL_VERSION_NUMBER >= 0x10100000L && !defined(LIBRESSL_VERSION_NUMBER)
    SSL_CTX* ctx = SSL_get_SSL_CTX(ssl);
#  else
    SSL_CTX* ctx = ssl->ctx;
#  endif
    OpenSSL::SSLEngine* p = reinterpret_cast<OpenSSL::SSLEngine*>(SSL_CTX_get_ex_data(ctx, 0));
    return p->dhParams(keyLength);
}
#endif

}

namespace
{

class Init
{
public:

    Init()
    {
        staticMutex = new IceUtil::Mutex;
    }

    ~Init()
    {
        //
        // OpenSSL 1.1.0 introduces a new thread API and removes
        // the need to use a custom thread callback.
        //
#if OPENSSL_VERSION_NUMBER < 0x10100000L || defined(LIBRESSL_VERSION_NUMBER)
        if(CRYPTO_get_locking_callback() == IceSSL_opensslLockCallback)
        {
            assert(locks);
            CRYPTO_set_locking_callback(0);
            delete[] locks;
            locks = 0;
        }

        if(CRYPTO_get_id_callback() == IceSSL_opensslThreadIdCallback)
        {
            CRYPTO_set_id_callback(0);
        }
#endif
        delete staticMutex;
        staticMutex = 0;
    }
};

Init init;

bool
passwordError()
{
    int reason = ERR_GET_REASON(ERR_peek_error());
    return (reason == PEM_R_BAD_BASE64_DECODE ||
            reason == PEM_R_BAD_DECRYPT ||
            reason == PEM_R_BAD_PASSWORD_READ ||
            reason == PEM_R_PROBLEMS_GETTING_PASSWORD ||
            reason == PKCS12_R_MAC_VERIFY_FAILURE);
}

}

IceUtil::Shared*
OpenSSL::upCast(OpenSSL::SSLEngine* p)
{
    return p;
}

OpenSSL::SSLEngine::SSLEngine(const CommunicatorPtr& communicator) :
    IceSSL::SSLEngine(communicator),
    _ctx(0)
{
    //
    // Initialize OpenSSL if necessary.
    //
    IceUtilInternal::MutexPtrLock<IceUtil::Mutex> sync(staticMutex);
    instanceCount++;

    if(instanceCount == 1)
    {
        PropertiesPtr properties = communicator->getProperties();

        //
        // The IceSSL.InitOpenSSL property specifies whether we should perform the global
        // startup (and shutdown) tasks for the OpenSSL library.
        //
        // If an application uses multiple components that each depend on OpenSSL, the
        // application should disable OpenSSL initialization in those components and
        // perform the initialization itself.
        //
        initOpenSSL = properties->getPropertyAsIntWithDefault("IceSSL.InitOpenSSL", 1) > 0;
        if(initOpenSSL)
        {

            //
            // OpenSSL 1.1.0 remove the need for library initialization and cleanup.
            //
#if OPENSSL_VERSION_NUMBER < 0x10100000L || defined(LIBRESSL_VERSION_NUMBER)
            if(!CRYPTO_get_id_callback())
            {
                CRYPTO_set_id_callback(IceSSL_opensslThreadIdCallback);
            }

            //
            // Create the mutexes and set the callbacks.
            //
            if(!CRYPTO_get_locking_callback())
            {
                assert(!locks);
                locks = new IceUtil::Mutex[CRYPTO_num_locks()];
                CRYPTO_set_locking_callback(IceSSL_opensslLockCallback);
            }

            //
            // Load human-readable error messages.
            //
            SSL_load_error_strings();

            //
            // Initialize the SSL library.
            //
            SSL_library_init();

            //
            // This is necessary to allow programs that use OpenSSL 0.9.x to
            // load private key files generated by OpenSSL 1.x.
            //
            OpenSSL_add_all_algorithms();
#endif

            //
            // Initialize the PRNG.
            //
            char randFile[1024];
            if(RAND_file_name(randFile, sizeof(randFile))) // Gets the name of a default seed file.
            {
                RAND_load_file(randFile, 1024);
            }

            string randFiles = properties->getProperty("IceSSL.Random");

            if(!randFiles.empty())
            {
                vector<string> files;
                const string defaultDir = properties->getProperty("IceSSL.DefaultDir");

                if(!IceUtilInternal::splitString(randFiles, IceUtilInternal::pathsep, files))
                {
                    cleanup();
                    throw PluginInitializationException(__FILE__, __LINE__,
                                                        "IceSSL: invalid value for IceSSL.Random:\n" + randFiles);
                }
                for(vector<string>::iterator p = files.begin(); p != files.end(); ++p)
                {
                    string file = *p;
                    string resolved;
                    if(!checkPath(file, defaultDir, false, resolved))
                    {
                        cleanup();
                        throw PluginInitializationException(__FILE__, __LINE__,
                                                            "IceSSL: entropy data file not found:\n" + file);
                    }
                    if(!RAND_load_file(resolved.c_str(), 1024))
                    {
                        cleanup();
                        throw PluginInitializationException(__FILE__, __LINE__,
                                                            "IceSSL: unable to load entropy data from " + resolved);
                    }
                }
            }
#if !defined (_WIN32) && !defined (OPENSSL_NO_EGD)
            //
            // The Entropy Gathering Daemon (EGD) is not available on Windows.
            // The file should be a Unix domain socket for the daemon.
            //
            string entropyDaemon = properties->getProperty("IceSSL.EntropyDaemon");
            if(!entropyDaemon.empty())
            {
                if(RAND_egd(entropyDaemon.c_str()) <= 0)
                {
                    cleanup();
                    throw PluginInitializationException(__FILE__, __LINE__,
                                                        "IceSSL: EGD failure using file " + entropyDaemon);
                }
            }
#endif
            if(!RAND_status())
            {
                getLogger()->warning("IceSSL: insufficient data to initialize PRNG");
            }
        }
        else
        {
            if(!properties->getProperty("IceSSL.Random").empty())
            {
                getLogger()->warning("IceSSL: ignoring IceSSL.Random because OpenSSL initialization is disabled");
            }
#ifndef _WIN32
            else if(!properties->getProperty("IceSSL.EntropyDaemon").empty())
            {
                getLogger()->warning("IceSSL: ignoring IceSSL.EntropyDaemon because OpenSSL initialization is disabled");
            }
#endif
        }
    }
}

void
OpenSSL::SSLEngine::cleanup()
{
    //
    // Must be called with the static mutex locked.
    //
    --instanceCount;
    //
    // OpenSSL 1.1.0 remove the need for library initialization and cleanup. We
    // still need to decrement instanceCount
    //
#if OPENSSL_VERSION_NUMBER < 0x10100000L || defined(LIBRESSL_VERSION_NUMBER)
    //
    // Clean up OpenSSL resources.
    //
    if(instanceCount == 0 && initOpenSSL)
    {
        //
        // NOTE: We can't destroy the locks here: threads which might have called openssl methods
        // might access openssl locks upon termination (from DllMain/THREAD_DETACHED). Instead,
        // we release the locks in the ~Init() static destructor. See bug #4156.
        //
        //CRYPTO_set_locking_callback(0);
        //CRYPTO_set_id_callback(0);
        //delete[] locks;
        //locks = 0;

        CRYPTO_cleanup_all_ex_data();
        RAND_cleanup();
        ERR_free_strings();
        EVP_cleanup();
    }
#endif
}

OpenSSL::SSLEngine::~SSLEngine()
{
    IceUtilInternal::MutexPtrLock<IceUtil::Mutex> sync(staticMutex);
    cleanup();
}

void
OpenSSL::SSLEngine::initialize()
{
    IceUtil::Mutex::Lock lock(_mutex);
    if(_initialized)
    {
        return;
    }

    try
    {
        IceSSL::SSLEngine::initialize();

        const string propPrefix = "IceSSL.";
        PropertiesPtr properties = communicator()->getProperties();

        //
        // Protocols selects which protocols to enable, by default we only enable TLS1.0
        // TLS1.1 and TLS1.2 to avoid security issues with SSLv3
        //
        vector<string> defaultProtocols;
#if defined(TLS1_VERSION) && !defined(OPENSSL_NO_TLS1_METHOD)
        defaultProtocols.push_back("tls1_0");
#endif

#if defined(TLS1_1_VERSION) && !defined(OPENSSL_NO_TLS1_1_METHOD)
        defaultProtocols.push_back("tls1_1");
#endif

#if defined(TLS1_2_VERSION) && !defined(OPENSSL_NO_TLS1_2_METHOD)
        defaultProtocols.push_back("tls1_2");
#endif

#if defined(TLS1_3_VERSION) && !defined(OPENSSL_NO_TLS1_3_METHOD)
        defaultProtocols.push_back("tls1_3");
#endif

        const int protocols =
                parseProtocols(properties->getPropertyAsListWithDefault(propPrefix + "Protocols", defaultProtocols));

        //
        // Create an SSL context if the application hasn't supplied one.
        //
        if(!_ctx)
        {
            _ctx = SSL_CTX_new(getMethod(protocols));
            if(!_ctx)
            {
                throw PluginInitializationException(__FILE__, __LINE__,
                                                    "IceSSL: unable to create SSL context:\n" + sslErrors());
            }

#if OPENSSL_VERSION_NUMBER >= 0x10100000L && !defined(LIBRESSL_VERSION_NUMBER)
            int securityLevel = properties->getPropertyAsIntWithDefault(propPrefix + "SecurityLevel", -1);
            if(securityLevel != -1)
            {
                SSL_CTX_set_security_level(_ctx, securityLevel);
                if(SSL_CTX_get_security_level(_ctx) != securityLevel)
                {
                    throw PluginInitializationException(__FILE__, __LINE__,
                                                        "IceSSL: unable to set SSL security level:\n" + sslErrors());
                }
            }
#endif
            //
            // Check for a default directory. We look in this directory for
            // files mentioned in the configuration.
            //
            const string defaultDir = properties->getProperty(propPrefix + "DefaultDir");

            //
            // If the configuration defines a password, or the application has supplied
            // a password prompt object, then register a password callback. Otherwise,
            // let OpenSSL use its default behavior.
            //
            {
                // TODO: Support quoted value?
                string password = properties->getProperty(propPrefix + "Password");
                if(!password.empty() || getPasswordPrompt())
                {
                    SSL_CTX_set_default_passwd_cb(_ctx, IceSSL_opensslPasswordCallback);
                    SSL_CTX_set_default_passwd_cb_userdata(_ctx, this);
                    setPassword(password);
                }
            }

            int passwordRetryMax = properties->getPropertyAsIntWithDefault(propPrefix + "PasswordRetryMax", 3);

            //
            // Establish the location of CA certificates.
            //
            {
                string path = properties->getProperty(propPrefix + "CAs");
                string resolved;
                const char* file = 0;
                const char* dir = 0;
                if(!path.empty())
                {
                    if(checkPath(path, defaultDir, false, resolved))
                    {
                        path = resolved;
                        file = path.c_str();
                    }

                    if(!file)
                    {
                        if(checkPath(path, defaultDir, true, resolved))
                        {
                            path = resolved;
                            dir = path.c_str();
                        }
                    }

                    if(!file && !dir)
                    {
                        throw PluginInitializationException(__FILE__, __LINE__,
                                                            "IceSSL: CA certificate path not found:\n" + path);
                    }
                }

                if(file || dir)
                {
                    //
                    // The certificate may be stored in an encrypted file, so handle
                    // password retries.
                    //
                    int count = 0;
                    int success = 0;
                    while(count < passwordRetryMax)
                    {
                        ERR_clear_error();
                        if((success = SSL_CTX_load_verify_locations(_ctx, file, dir)) != 0 || !passwordError())
                        {
                            break;
                        }
                        ++count;
                    }
                    if(!success)
                    {
                        string msg = "IceSSL: unable to establish CA certificates";
                        if(passwordError())
                        {
                            msg += ":\ninvalid password";
                        }
                        else
                        {
                            string err = sslErrors();
                            if(!err.empty())
                            {
                                msg += ":\n" + err;
                            }
                        }
                        throw PluginInitializationException(__FILE__, __LINE__, msg);
                    }
                }
                else if(properties->getPropertyAsInt("IceSSL.UsePlatformCAs") > 0)
                {
                    SSL_CTX_set_default_verify_paths(_ctx);
                }
            }

            //
            // Establish the certificate chains and private keys. One RSA certificate and
            // one DSA certificate are allowed.
            //
            string certFile = properties->getProperty(propPrefix + "CertFile");
            string keyFile = properties->getProperty(propPrefix + "KeyFile");
            bool keyLoaded = false;

            vector<string>::size_type numCerts = 0;
            if(!certFile.empty())
            {
                vector<string> files;
                if(!IceUtilInternal::splitString(certFile, IceUtilInternal::pathsep, files) || files.size() > 2)
                {
                    throw PluginInitializationException(__FILE__, __LINE__,
                                                        "IceSSL: invalid value for " + propPrefix + "CertFile:\n" +
                                                        certFile);
                }
                numCerts = files.size();
                for(vector<string>::iterator p = files.begin(); p != files.end(); ++p)
                {
                    string file = *p;
                    string resolved;
                    if(!checkPath(file, defaultDir, false, resolved))
                    {
                        throw PluginInitializationException(__FILE__, __LINE__,
                                                            "IceSSL: certificate file not found:\n" + file);
                    }
                    file = resolved;

                    //
                    // First we try to load the certificate using PKCS12 format if that fails
                    // we fallback to PEM format.
                    //
                    vector<char> buffer;
                    readFile(file, buffer);
                    int success = 0;

                    const unsigned char* b = const_cast<const unsigned char*>(reinterpret_cast<unsigned char*>(&buffer[0]));
                    PKCS12* p12 = d2i_PKCS12(0, &b, static_cast<long>(buffer.size()));
                    if(p12)
                    {
                        EVP_PKEY* key = 0;
                        X509* cert = 0;
                        STACK_OF(X509)* chain = 0;

                        int count = 0;
                        try
                        {
                            while(count < passwordRetryMax)
                            {
                                ERR_clear_error();
                                //
                                // chain may have a bogus value from a previous call to PKCS12_parse, so we
                                // reset it prior to each call.
                                //
                                key = 0;
                                cert = 0;
                                chain = 0;
                                if((success = PKCS12_parse(p12, password(false).c_str(), &key, &cert, &chain)) == 0)
                                {
                                    if(passwordError())
                                    {
                                        count++;
                                        continue;
                                    }
                                    break;
                                }

                                if(!cert || !SSL_CTX_use_certificate(_ctx, cert))
                                {
                                    throw PluginInitializationException(__FILE__, __LINE__,
                                                                "IceSSL: unable to load SSL certificate:\n" +
                                                                (cert ? sslErrors() : "certificate not found"));
                                }

                                if(!key || !SSL_CTX_use_PrivateKey(_ctx, key))
                                {
                                    throw PluginInitializationException(__FILE__, __LINE__,
                                                                "IceSSL: unable to load SSL private key:\n" +
                                                                (key ? sslErrors() : "key not found"));
                                }
                                keyLoaded = true;

                                if(chain && sk_X509_num(chain))
                                {
                                    // Pop each cert from the stack so we can free the stack later.
                                    // The CTX destruction will take care of the certificates
                                    X509 *c = 0;
                                    while((c = sk_X509_pop(chain)) != 0)
                                    {
                                        if(!SSL_CTX_add_extra_chain_cert(_ctx, c))
                                        {
                                            throw PluginInitializationException(__FILE__, __LINE__,
                                                    "IceSSL: unable to add extra SSL certificate:\n" + sslErrors());
                                        }
                                    }
                                }

                                if(chain)
                                {
                                    // This chain should now be empty. No need to call sk_X509_pop_free()
                                    sk_X509_free(chain);
                                }
                                assert(key && cert);
                                EVP_PKEY_free(key);
                                X509_free(cert);
                                break;
                            }
                            PKCS12_free(p12);
                        }
                        catch(...)
                        {
                            PKCS12_free(p12);
                            if(chain)
                            {
                                sk_X509_pop_free(chain, X509_free);
                            }

                            if(key)
                            {
                                EVP_PKEY_free(key);
                            }

                            if(cert)
                            {
                                X509_free(cert);
                            }
                            throw;
                        }
                    }
                    else
                    {
                        //
                        // The certificate may be stored in an encrypted file, so handle
                        // password retries.
                        //
                        int count = 0;
                        while(count < passwordRetryMax)
                        {
                            ERR_clear_error();
                            if((success = SSL_CTX_use_certificate_chain_file(_ctx, file.c_str())) == 0)
                            {
                                if(passwordError())
                                {
                                    count++;
                                    continue;
                                }
                            }
                            count++;
                        }
                    }

                    if(!success)
                    {
                        string msg = "IceSSL: unable to load certificate chain from file " + file;
                        if(passwordError())
                        {
                            msg += ":\ninvalid password";
                        }
                        else
                        {
                            string err = sslErrors();
                            if(!err.empty())
                            {
                                msg += ":\n" + err;
                            }
                        }
                        throw PluginInitializationException(__FILE__, __LINE__, msg);
                    }
                }
            }

            if(keyFile.empty())
            {
                keyFile = certFile; // Assume the certificate file also contains the private key.
            }
            if(!keyLoaded && !keyFile.empty())
            {
                vector<string> files;
                if(!IceUtilInternal::splitString(keyFile, IceUtilInternal::pathsep, files) || files.size() > 2)
                {
                    throw PluginInitializationException(__FILE__, __LINE__,
                                                "IceSSL: invalid value for " + propPrefix + "KeyFile:\n" + keyFile);
                }
                if(files.size() != numCerts)
                {
                    throw PluginInitializationException(__FILE__, __LINE__,
                                "IceSSL: " + propPrefix + "KeyFile does not agree with " + propPrefix + "CertFile");
                }
                for(vector<string>::iterator p = files.begin(); p != files.end(); ++p)
                {
                    string file = *p;
                    string resolved;
                    if(!checkPath(file, defaultDir, false, resolved))
                    {
                        throw PluginInitializationException(__FILE__, __LINE__, "IceSSL: key file not found:\n" + file);
                    }
                    file = resolved;
                    //
                    // The private key may be stored in an encrypted file, so handle password retries.
                    //
                    int count = 0;
                    int err = 0;
                    while(count < passwordRetryMax)
                    {
                        ERR_clear_error();
                        err = SSL_CTX_use_PrivateKey_file(_ctx, file.c_str(), SSL_FILETYPE_PEM);
                        if(err)
                        {
                            break;
                        }
                        ++count;
                    }
                    if(err == 0)
                    {
                        string msg = "IceSSL: unable to load private key from file " + file;
                        if(passwordError())
                        {
                            msg += ":\ninvalid password";
                        }
                        else
                        {
                            string errStr = sslErrors();
                            if(!errStr.empty())
                            {
                                msg += ":\n" + errStr;
                            }
                        }
                        throw PluginInitializationException(__FILE__, __LINE__, msg);
                    }
                }
                keyLoaded = true;
            }

            if(keyLoaded && !SSL_CTX_check_private_key(_ctx))
            {
                throw PluginInitializationException(__FILE__, __LINE__,
                                                    "IceSSL: unable to validate private key(s):\n" + sslErrors());
            }

            //
            // Diffie Hellman configuration.
            //
            {
#ifndef OPENSSL_NO_DH
                _dhParams = new DHParams;
                SSL_CTX_set_options(_ctx, SSL_OP_SINGLE_DH_USE);
                SSL_CTX_set_tmp_dh_callback(_ctx, IceSSL_opensslDHCallback);
#endif
                //
                // Properties have the following form:
                //
                // ...DH.<keyLength>=file
                //
                const string dhPrefix = propPrefix + "DH.";
                PropertyDict d = properties->getPropertiesForPrefix(dhPrefix);
                if(!d.empty())
                {
#ifdef OPENSSL_NO_DH
                    getLogger()->warning("IceSSL: OpenSSL is not configured for Diffie Hellman");
#else
                    for(PropertyDict::iterator p = d.begin(); p != d.end(); ++p)
                    {
                        string s = p->first.substr(dhPrefix.size());
                        int keyLength = atoi(s.c_str());
                        if(keyLength > 0)
                        {
                            string file = p->second;
                            string resolved;
                            if(!checkPath(file, defaultDir, false, resolved))
                            {
                                throw PluginInitializationException(__FILE__, __LINE__,
                                                                    "IceSSL: DH parameter file not found:\n" + file);
                            }
                            file = resolved;
                            if(!_dhParams->add(keyLength, file))
                            {
                                throw PluginInitializationException(__FILE__, __LINE__,
                                                                    "IceSSL: unable to read DH parameter file " + file);
                            }
                        }
                    }
#endif
                }
            }

            int revocationCheck = getRevocationCheck();
            if(revocationCheck > 0)
            {
                vector<string> crlFiles =
                    properties->getPropertyAsList(propPrefix + "CertificateRevocationListFiles");
                if(crlFiles.empty())
                {
                    throw PluginInitializationException(
                        __FILE__,
                        __LINE__,
                        "IceSSL: cannot enable revocation checks without setting certificate revocation list files");
                }

                X509_STORE* store = SSL_CTX_get_cert_store(_ctx);
                if(!store)
                {
                    throw PluginInitializationException(
                        __FILE__,
                        __LINE__,
                        "IceSSL: unable to obtain the certificate store");
                }

                X509_LOOKUP* lookup = X509_STORE_add_lookup(store, X509_LOOKUP_file());
                if(!lookup)
                {
                    throw PluginInitializationException(__FILE__, __LINE__, "IceSSL: add lookup failed");
                }

                for(vector<string>::const_iterator it = crlFiles.begin(); it != crlFiles.end(); it++)
                {
                    string file;
                    if(!checkPath(*it, defaultDir, false, file))
                    {
                        throw PluginInitializationException(
                            __FILE__,
                            __LINE__,
                            "IceSSL: CRL file not found `" + *it + "'");
                    }

                    if(X509_LOOKUP_load_file(lookup, file.c_str(), X509_FILETYPE_PEM) == 0)
                    {
                        throw PluginInitializationException(
                            __FILE__,
                            __LINE__,
                            "IceSSL: CRL load failure `" + *it + "'");
                    }
                }

                unsigned long flags = X509_V_FLAG_CRL_CHECK;
                if(revocationCheck > 1)
                {
                    flags |= X509_V_FLAG_CRL_CHECK_ALL;
                }
                X509_STORE_set_flags(store, flags);
            }

            SSL_CTX_set_mode(_ctx, SSL_MODE_ENABLE_PARTIAL_WRITE);
        }

        //
        // Store a pointer to ourself for use in OpenSSL callbacks.
        //
        SSL_CTX_set_ex_data(_ctx, 0, this);

        //
        // This is necessary for successful interop with Java. Without it, a Java
        // client would fail to reestablish a connection: the server gets the
        // error "session id context uninitialized" and the client receives
        // "SSLHandshakeException: Remote host closed connection during handshake".
        //
        SSL_CTX_set_session_cache_mode(_ctx, SSL_SESS_CACHE_OFF);

        //
        // Although we disable session caching, we still need to set a session ID
        // context (ICE-5103). The value can be anything; here we just use the
        // pointer to this SharedInstance object.
        //
        SSL_CTX_set_session_id_context(_ctx, reinterpret_cast<unsigned char*>(this),
                                        static_cast<unsigned int>(sizeof(this)));

        //
        // Select protocols.
        //
        if(protocols != 0)
        {
            setOptions(protocols);
        }

        //
        // Establish the cipher list.
        //
        string ciphersStr = properties->getProperty(propPrefix + "Ciphers");
        if(!ciphersStr.empty())
        {
            if(!SSL_CTX_set_cipher_list(_ctx, ciphersStr.c_str()))
            {
                throw PluginInitializationException(__FILE__, __LINE__,
                                        "IceSSL: unable to set ciphers using `" + ciphersStr + "':\n" + sslErrors());
            }
        }

        if(securityTraceLevel() >= 1)
        {
            ostringstream os;
            os << "enabling SSL ciphersuites:";

            SSL* ssl = SSL_new(_ctx);
            STACK_OF(SSL_CIPHER)* ciphers =  SSL_get_ciphers(ssl);
            if(ciphers)
            {
                for(int i = 0, length = sk_SSL_CIPHER_num(ciphers); i < length; ++i)
                {
                    os << "\n" << SSL_CIPHER_get_name(sk_SSL_CIPHER_value(ciphers, i));
                }
            }
            SSL_free(ssl);
            getLogger()->trace(securityTraceCategory(), os.str());
        }
    }
    catch(...)
    {
        //
        // We free the SSL context regardless of whether the plugin created it
        // or the application supplied it.
        //
        SSL_CTX_free(_ctx);
        _ctx = 0;
        throw;
    }

    _initialized = true;
}

void
OpenSSL::SSLEngine::context(SSL_CTX* context)
{
    if(initialized())
    {
        throw PluginInitializationException(__FILE__, __LINE__, "IceSSL: plug-in is already initialized");
    }

    assert(!_ctx);
    _ctx = context;
}

SSL_CTX*
OpenSSL::SSLEngine::context() const
{
    return _ctx;
}

string
OpenSSL::SSLEngine::sslErrors() const
{
    return getSslErrors(securityTraceLevel() >= 1);
}

void
OpenSSL::SSLEngine::destroy()
{
    if(_ctx)
    {
        SSL_CTX_free(_ctx);
        _ctx = 0;
    }
}

IceInternal::TransceiverPtr
OpenSSL::SSLEngine::createTransceiver(const InstancePtr& instance,
                                      const IceInternal::TransceiverPtr& delegate,
                                      const string& hostOrAdapterName,
                                      bool incoming)
{
    return new OpenSSL::TransceiverI(instance, delegate, hostOrAdapterName, incoming);
}

#ifndef OPENSSL_NO_DH
DH*
OpenSSL::SSLEngine::dhParams(int keyLength)
{
    return _dhParams->get(keyLength);
}
#endif

int
OpenSSL::SSLEngine::parseProtocols(const StringSeq& protocols) const
{
    int v = 0;

    for(Ice::StringSeq::const_iterator p = protocols.begin(); p != protocols.end(); ++p)
    {
        string prot = IceUtilInternal::toUpper(*p);
        if(prot == "SSL3" || prot == "SSLV3")
        {
#if defined(OPENSSL_NO_SSL3_METHOD) || !defined(SSL3_VERSION)
            throw PluginInitializationException(__FILE__, __LINE__,
                                                "IceSSL: OpenSSL was build without SSLv3 support");
#else
            v |= SSLv3;
#endif
        }
        else if(prot == "TLS" || prot == "TLS1" || prot == "TLSV1" || prot == "TLS1_0" || prot == "TLSV1_0")
        {
#if defined(OPENSSL_NO_TLS1_METHOD) || !defined(TLS1_VERSION)
            throw PluginInitializationException(__FILE__, __LINE__,
                                                "IceSSL: OpenSSL was build without TLS 1.0 support");
#else
            v |= TLSv1_0;
#endif
        }
        else if(prot == "TLS1_1" || prot == "TLSV1_1")
        {
#if defined(OPENSSL_NO_TLS1_1_METHOD) || !defined(TLS1_1_VERSION)
            throw PluginInitializationException(__FILE__, __LINE__,
                                                "IceSSL: OpenSSL was build without TLS 1.1 support");
#else
            v |= TLSv1_1;
#endif
        }
        else if(prot == "TLS1_2" || prot == "TLSV1_2")
        {
#if defined(OPENSSL_NO_TLS1_2_METHOD) || !defined(TLS1_2_VERSION)
            throw PluginInitializationException(__FILE__, __LINE__,
                                                "IceSSL: OpenSSL was build without TLS 1.2 support");
#else
            v |= TLSv1_2;
#endif
        }
        else if(prot == "TLS1_3" || prot == "TLSV1_3")
        {
#if defined(OPENSSL_NO_TLS1_3_METHOD) || !defined(TLS1_3_VERSION)
            throw PluginInitializationException(__FILE__, __LINE__,
                                                "IceSSL: OpenSSL was build without TLS 1.3 support");
#else
            v |= TLSv1_3;
#endif
        }
        else
        {
            throw PluginInitializationException(__FILE__, __LINE__, "IceSSL: unrecognized protocol `" + *p + "'");
        }
    }

    return v;
}

SSL_METHOD*
OpenSSL::SSLEngine::getMethod(int /*protocols*/)
{
#if OPENSSL_VERSION_NUMBER >= 0x10100000L && !defined(LIBRESSL_VERSION_NUMBER)
    SSL_METHOD* meth = const_cast<SSL_METHOD*>(TLS_method());
#else
    //
    // Despite its name, the SSLv23 method can negotiate SSL3, TLS1.0, TLS1.1, and TLS1.2.
    // We use the const_cast for backward compatibility with older OpenSSL releases.
    //
    SSL_METHOD* meth = const_cast<SSL_METHOD*>(SSLv23_method());

    /*
        * Early versions of OpenSSL 1.0.1 would not negotiate a TLS1.2 connection using
        * the SSLv23 method. You can enable the code below to override the method.
    if(protocols & TLSv1_2)
    {
        meth = const_cast<SSL_METHOD*>(TLSv1_2_method());
    }
    */
#endif
    return meth;
}

void
OpenSSL::SSLEngine::setOptions(int protocols)
{
    long opts = SSL_OP_NO_SSLv2; // SSLv2 is not supported.

#ifdef SSL_OP_NO_SSLv3
    if(!(protocols & SSLv3))
    {
        opts |= SSL_OP_NO_SSLv3;
    }
#endif

#ifdef SSL_OP_NO_TLSv1
    if(!(protocols & TLSv1_0))
    {
        opts |= SSL_OP_NO_TLSv1;
    }
#endif

#ifdef SSL_OP_NO_TLSv1_1
    if(!(protocols & TLSv1_1))
    {
        opts |= SSL_OP_NO_TLSv1_1;
        //
        // The value of SSL_OP_NO_TLSv1_1 changed between 1.0.1a and 1.0.1b.
        //
        if(SSL_OP_NO_TLSv1_1 == 0x00000400L)
        {
            opts |= 0x10000000L; // New value of SSL_OP_NO_TLSv1_1.
        }
    }
#endif

#ifdef SSL_OP_NO_TLSv1_2
    if(!(protocols & TLSv1_2))
    {
        opts |= SSL_OP_NO_TLSv1_2;
    }
#endif

#ifdef SSL_OP_NO_TLSv1_3
    if(!(protocols & TLSv1_3))
    {
        opts |= SSL_OP_NO_TLSv1_3;
    }
#endif
    SSL_CTX_set_options(_ctx, opts);
}
