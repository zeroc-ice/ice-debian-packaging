// **********************************************************************
//
// Copyright (c) 2003-2017 ZeroC, Inc. All rights reserved.
//
// This copy of Ice is licensed to you under the terms described in the
// ICE_LICENSE file included in this distribution.
//
// **********************************************************************

package test.IceSSL.configuration;
import java.io.PrintWriter;

import test.IceSSL.configuration.Test.ServerFactoryPrx;
import test.IceSSL.configuration.Test.ServerFactoryPrxHelper;
import test.IceSSL.configuration.Test.ServerPrx;

//
// NOTE: This test is not interoperable with other language mappings.
//

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

    private static java.security.cert.X509Certificate
    loadCertificate(String path, String alias)
    {
        try
        {
            java.security.KeyStore keystore = java.security.KeyStore.getInstance("JKS");
            keystore.load(new java.io.FileInputStream(path), "password".toCharArray());
            return (java.security.cert.X509Certificate)keystore.getCertificate(alias);
        }
        catch(Exception ex)
        {
            test(false);
            return null;
        }
    }

    private static Ice.InitializationData
    createClientProps(Ice.Properties defaultProperties, String defaultDir, String defaultHost)
    {
        Ice.InitializationData initData = new Ice.InitializationData();
        initData.properties = Ice.Util.createProperties();
        initData.properties.setProperty("Ice.Plugin.IceSSL", "IceSSL.PluginFactory");
        if(defaultProperties.getProperty("Ice.IPv6").length() > 0)
        {
            initData.properties.setProperty("Ice.IPv6", defaultProperties.getProperty("Ice.IPv6"));
        }
        initData.properties.setProperty("Ice.RetryIntervals", "-1");
        initData.properties.setProperty("IceSSL.DefaultDir", defaultDir);
        initData.properties.setProperty("IceSSL.Random", "seed.dat");
        if(defaultHost.length() > 0)
        {
            initData.properties.setProperty("Ice.Default.Host", defaultHost);
        }
        return initData;
    }

    private static Ice.InitializationData
    createClientProps(Ice.Properties defaultProperties, String defaultDir, String defaultHost, String ks, String ts)
    {
        Ice.InitializationData initData = createClientProps(defaultProperties, defaultDir, defaultHost);
        if(!ks.isEmpty())
        {
            initData.properties.setProperty("IceSSL.Keystore", ks + ".jks");
        }
        if(!ts.isEmpty())
        {
            initData.properties.setProperty("IceSSL.Truststore", ts + ".jks");
        }
        initData.properties.setProperty("IceSSL.Password", "password");
        return initData;
    }

    private static java.util.Map<String, String>
    createServerProps(Ice.Properties defaultProperties, String defaultDir, String defaultHost)
    {
        java.util.Map<String, String> result = new java.util.HashMap<String, String>();
        result.put("Ice.Plugin.IceSSL", "IceSSL.PluginFactory");
        if(defaultProperties.getProperty("Ice.IPv6").length() > 0)
        {
            result.put("Ice.IPv6", defaultProperties.getProperty("Ice.IPv6"));
        }
        result.put("IceSSL.DefaultDir", defaultDir);
        result.put("IceSSL.Random", "seed.dat");
        if(defaultHost.length() > 0)
        {
            result.put("Ice.Default.Host", defaultHost);
        }
        return result;
    }

    private static java.util.Map<String, String>
    createServerProps(Ice.Properties defaultProperties, String defaultDir, String defaultHost, String ks, String ts)
    {
        java.util.Map<String, String> d = createServerProps(defaultProperties, defaultDir, defaultHost);
        if(!ks.isEmpty())
        {
            d.put("IceSSL.Keystore", ks + ".jks");
        }
        if(!ts.isEmpty())
        {
            d.put("IceSSL.Truststore", ts + ".jks");
        }
        d.put("IceSSL.Password", "password");
        return d;
    }

    public static ServerFactoryPrx
    allTests(test.Util.Application app, String testDir, PrintWriter out)
    {
        Ice.Communicator communicator = app.communicator();
        final String factoryRef = "factory:tcp -p 12010";
        Ice.ObjectPrx b = communicator.stringToProxy(factoryRef);
        test(b != null);
        ServerFactoryPrx factory = ServerFactoryPrxHelper.checkedCast(b);

        final String defaultHost = communicator.getProperties().getProperty("Ice.Default.Host");
        final String defaultDir = testDir + "/../certs";
        final Ice.Properties defaultProperties = communicator.getProperties();
        final String[] args = new String[0];

        out.print("testing manual initialization... ");
        out.flush();
        {
            Ice.InitializationData initData = createClientProps(defaultProperties, defaultDir, defaultHost);
            initData.properties.setProperty("Ice.InitPlugins", "0");
            Ice.Communicator comm = Ice.Util.initialize(args, initData);
            Ice.ObjectPrx p = comm.stringToProxy("dummy:ssl -p 9999");
            try
            {
                p.ice_ping();
                test(false);
            }
            catch(Ice.PluginInitializationException ex)
            {
                // Expected.
            }
            catch(Ice.LocalException ex)
            {
                test(false);
            }
            comm.destroy();
        }
        {
            Ice.InitializationData initData = createClientProps(defaultProperties, defaultDir, defaultHost);
            initData.properties.setProperty("Ice.InitPlugins", "0");
            initData.properties.setProperty("IceSSL.Ciphers", "NONE (.*DH_anon.*AES.*)");
            initData.properties.setProperty("IceSSL.VerifyPeer", "0");
            Ice.Communicator comm = Ice.Util.initialize(args, initData);
            Ice.PluginManager pm = comm.getPluginManager();
            pm.initializePlugins();
            Ice.ObjectPrx obj = comm.stringToProxy(factoryRef);
            test(obj != null);
            ServerFactoryPrx fact = ServerFactoryPrxHelper.checkedCast(obj);
            java.util.Map<String, String> d = createServerProps(defaultProperties, defaultDir, defaultHost);
            d.put("IceSSL.Ciphers", "NONE (.*DH_anon.*AES.*)");
            d.put("IceSSL.VerifyPeer", "0");
            ServerPrx server = fact.createServer(d);
            try
            {
                server.ice_ping();
            }
            catch(Ice.LocalException ex)
            {
                test(false);
            }
            fact.destroyServer(server);
            comm.destroy();
        }
        out.println("ok");

        out.print("testing certificate verification... ");
        out.flush();
        {
            java.util.Map<String, String> d;
            Ice.InitializationData initData;

            //
            // Test IceSSL.VerifyPeer=0. Client does not have a certificate,
            // and it doesn't trust the server certificate.
            //
            initData = createClientProps(defaultProperties, defaultDir, defaultHost, "", "");
            initData.properties.setProperty("IceSSL.VerifyPeer", "0");
            Ice.Communicator comm = Ice.Util.initialize(args, initData);
            ServerFactoryPrx fact = ServerFactoryPrxHelper.checkedCast(comm.stringToProxy(factoryRef));
            test(fact != null);
            d = createServerProps(defaultProperties, defaultDir, defaultHost, "s_rsa_ca1", "");
            d.put("IceSSL.VerifyPeer", "0");
            ServerPrx server = fact.createServer(d);
            try
            {
                server.noCert();
                test(!((IceSSL.ConnectionInfo)server.ice_getConnection().getInfo()).verified);
            }
            catch(Ice.LocalException ex)
            {
                ex.printStackTrace();
                test(false);
            }
            fact.destroyServer(server);
            comm.destroy();

            //
            // Test IceSSL.VerifyPeer=0. Client does not have a certificate,
            // but it still verifies the server's.
            //
            initData = createClientProps(defaultProperties, defaultDir, defaultHost, "", "cacert1");
            comm = Ice.Util.initialize(args, initData);
            fact = ServerFactoryPrxHelper.checkedCast(comm.stringToProxy(factoryRef));
            test(fact != null);
            d = createServerProps(defaultProperties, defaultDir, defaultHost, "s_rsa_ca1", "");
            d.put("IceSSL.VerifyPeer", "0");
            server = fact.createServer(d);
            try
            {
                server.noCert();
                test(((IceSSL.ConnectionInfo)server.ice_getConnection().getInfo()).verified);
            }
            catch(Ice.LocalException ex)
            {
                test(false);
            }
            fact.destroyServer(server);

            //
            // Test IceSSL.VerifyPeer=1. Client does not have a certificate.
            //
            initData = createClientProps(defaultProperties, defaultDir, defaultHost, "", "cacert1");
            comm = Ice.Util.initialize(args, initData);
            fact = ServerFactoryPrxHelper.checkedCast(comm.stringToProxy(factoryRef));
            test(fact != null);
            d = createServerProps(defaultProperties, defaultDir, defaultHost, "s_rsa_ca1", "");
            d.put("IceSSL.VerifyPeer", "1");
            server = fact.createServer(d);
            try
            {
                server.noCert();
            }
            catch(Ice.LocalException ex)
            {
                test(false);
            }
            fact.destroyServer(server);

            //
            // Test IceSSL.VerifyPeer=2. This should fail because the client
            // does not supply a certificate.
            //
            d = createServerProps(defaultProperties, defaultDir, defaultHost, "s_rsa_ca1", "");
            d.put("IceSSL.VerifyPeer", "2");
            server = fact.createServer(d);
            try
            {
                server.ice_ping();
                test(false);
            }
            catch(Ice.SecurityException ex)
            {
                // Expected.
            }
            catch(Ice.ConnectionLostException ex)
            {
                // Expected.
            }
            catch(Ice.LocalException ex)
            {
                test(false);
            }
            fact.destroyServer(server);

            comm.destroy();

            //
            // Test IceSSL.VerifyPeer=1. Client has a certificate.
            //
            // Provide "cacert1" to the client to verify the server
            // certificate (without this the client connection wouln't be
            // able to provide the certificate chain).
            //
            initData = createClientProps(defaultProperties, defaultDir, defaultHost, "c_rsa_ca1", "cacert1");
            comm = Ice.Util.initialize(args, initData);
            fact = ServerFactoryPrxHelper.checkedCast(comm.stringToProxy(factoryRef));
            test(fact != null);
            d = createServerProps(defaultProperties, defaultDir, defaultHost, "s_rsa_ca1", "cacert1");
            d.put("IceSSL.VerifyPeer", "1");
            server = fact.createServer(d);
            try
            {
                java.security.cert.X509Certificate clientCert = loadCertificate(defaultDir + "/c_rsa_ca1.jks", "cert");
                server.checkCert(clientCert.getSubjectDN().toString(), clientCert.getIssuerDN().toString());

                java.security.cert.X509Certificate serverCert = loadCertificate(defaultDir + "/s_rsa_ca1.jks", "cert");
                java.security.cert.X509Certificate caCert = loadCertificate(defaultDir + "/cacert1.jks", "ca");

                IceSSL.NativeConnectionInfo info = (IceSSL.NativeConnectionInfo)server.ice_getConnection().getInfo();

                test(info.nativeCerts.length == 2);
                test(info.verified);

                test(caCert.equals(info.nativeCerts[1]));
                test(serverCert.equals(info.nativeCerts[0]));
            }
            catch(Exception ex)
            {
                ex.printStackTrace();
                test(false);
            }
            fact.destroyServer(server);

            //
            // Test IceSSL.VerifyPeer=2. Client has a certificate.
            //
            d = createServerProps(defaultProperties, defaultDir, defaultHost, "s_rsa_ca1", "cacert1");
            d.put("IceSSL.VerifyPeer", "2");
            server = fact.createServer(d);
            try
            {
                java.security.cert.X509Certificate clientCert = loadCertificate(defaultDir + "/c_rsa_ca1.jks", "cert");
                server.checkCert(clientCert.getSubjectDN().toString(), clientCert.getIssuerDN().toString());
            }
            catch(Exception ex)
            {
                test(false);
            }
            fact.destroyServer(server);

            comm.destroy();

            //
            // Test IceSSL.VerifyPeer=1. This should fail because the
            // client doesn't trust the server's CA.
            //
            initData = createClientProps(defaultProperties, defaultDir, defaultHost, "", "");
            initData.properties.setProperty("IceSSL.VerifyPeer", "1");
            comm = Ice.Util.initialize(args, initData);
            fact = ServerFactoryPrxHelper.checkedCast(comm.stringToProxy(factoryRef));
            test(fact != null);
            d = createServerProps(defaultProperties, defaultDir, defaultHost, "s_rsa_ca1", "cacert1");
            d.put("IceSSL.VerifyPeer", "0");
            server = fact.createServer(d);
            try
            {
                server.ice_ping();
                test(false);
            }
            catch(Ice.SecurityException ex)
            {
                // Expected.
            }
            catch(Ice.LocalException ex)
            {
                test(false);
            }
            fact.destroyServer(server);

            comm.destroy();

            //
            // Test IceSSL.VerifyPeer=1. This should fail because the
            // server doesn't trust the client's CA.
            //
            initData = createClientProps(defaultProperties, defaultDir, defaultHost, "c_rsa_ca2", "");
            initData.properties.setProperty("IceSSL.VerifyPeer", "0");
            comm = Ice.Util.initialize(args, initData);
            fact = ServerFactoryPrxHelper.checkedCast(comm.stringToProxy(factoryRef));
            test(fact != null);
            d = createServerProps(defaultProperties, defaultDir, defaultHost, "s_rsa_ca1", "");
            d.put("IceSSL.VerifyPeer", "1");
            server = fact.createServer(d);
            try
            {
                server.ice_ping();
                test(false);
            }
            catch(Ice.SecurityException ex)
            {
                // Expected.
            }
            catch(Ice.ConnectionLostException ex)
            {
                // Expected.
            }
            catch(Ice.LocalException ex)
            {
                test(false);
            }
            fact.destroyServer(server);
            comm.destroy();

            //
            // Test IceSSL.VerifyPeer=1. This should fail because the server
            // does not supply a certificate.
            //
            initData = createClientProps(defaultProperties, defaultDir, defaultHost);
            initData.properties.setProperty("IceSSL.Ciphers", "NONE (.*DH_anon.*AES.*)");
            initData.properties.setProperty("IceSSL.VerifyPeer", "1");
            comm = Ice.Util.initialize(args, initData);
            fact = ServerFactoryPrxHelper.checkedCast(comm.stringToProxy(factoryRef));
            test(fact != null);
            d = createServerProps(defaultProperties, defaultDir, defaultHost);
            d.put("IceSSL.Ciphers", "NONE (.*DH_anon.*AES.*)");
            d.put("IceSSL.VerifyPeer", "0");
            server = fact.createServer(d);
            try
            {
                server.ice_ping();
                test(false);
            }
            catch(Ice.SecurityException ex)
            {
                // Expected.
            }
            catch(Ice.LocalException ex)
            {
                test(false);
            }
            fact.destroyServer(server);
            comm.destroy();

            //
            // This should succeed because the self signed certificate used by the server is
            // trusted.
            //
            initData = createClientProps(defaultProperties, defaultDir, defaultHost, "", "cacert2");
            initData.properties.setProperty("IceSSL.VerifyPeer", "1");
            comm = Ice.Util.initialize(args, initData);
            fact = ServerFactoryPrxHelper.checkedCast(comm.stringToProxy(factoryRef));
            test(fact != null);
            d = createServerProps(defaultProperties, defaultDir, defaultHost, "s_cacert2", "");
            d.put("IceSSL.VerifyPeer", "0");
            server = fact.createServer(d);
            try
            {
                server.ice_ping();
            }
            catch(Ice.LocalException ex)
            {
                ex.printStackTrace();
                test(false);
            }
            fact.destroyServer(server);
            comm.destroy();

            //
            // This should fail because the self signed certificate used by the server is not
            // trusted.
            //
            initData = createClientProps(defaultProperties, defaultDir, defaultHost);
            initData.properties.setProperty("IceSSL.VerifyPeer", "1");
            comm = Ice.Util.initialize(args, initData);
            fact = ServerFactoryPrxHelper.checkedCast(comm.stringToProxy(factoryRef));
            test(fact != null);
            d = createServerProps(defaultProperties, defaultDir, defaultHost, "s_cacert2", "");
            d.put("IceSSL.VerifyPeer", "0");
            server = fact.createServer(d);
            try
            {
                server.ice_ping();
                test(false);
            }
            catch(Ice.SecurityException ex)
            {
                // Expected.
            }
            catch(Ice.LocalException ex)
            {
                test(false);
            }
            fact.destroyServer(server);
            comm.destroy();

            //
            // Verify that IceSSL.CheckCertName has no effect in a server.
            //
            initData = createClientProps(defaultProperties, defaultDir, defaultHost, "c_rsa_ca1", "cacert1");
            comm = Ice.Util.initialize(args, initData);

            fact = ServerFactoryPrxHelper.checkedCast(comm.stringToProxy(factoryRef));
            test(fact != null);
            d = createServerProps(defaultProperties, defaultDir, defaultHost, "s_rsa_ca1", "cacert1");
            d.put("IceSSL.CheckCertName", "1");
            server = fact.createServer(d);
            try
            {
                server.ice_ping();
            }
            catch(Ice.LocalException ex)
            {
                test(false);
            }
            fact.destroyServer(server);
            comm.destroy();

            //
            // Test IceSSL.CheckCertName. The test certificates for the server contain "127.0.0.1"
            // as the common name or as a subject alternative name, so we only perform this test when
            // the default host is "127.0.0.1".
            //
            if(defaultHost.equals("127.0.0.1"))
            {
                //
                // Test subject alternative name.
                //
                {
                    initData = createClientProps(defaultProperties, defaultDir, defaultHost, "c_rsa_ca1", "cacert1");
                    initData.properties.setProperty("IceSSL.CheckCertName", "1");
                    comm = Ice.Util.initialize(args, initData);

                    fact = ServerFactoryPrxHelper.checkedCast(comm.stringToProxy(factoryRef));
                    test(fact != null);
                    d = createServerProps(defaultProperties, defaultDir, defaultHost, "s_rsa_ca1", "cacert1");
                    server = fact.createServer(d);
                    try
                    {
                        server.ice_ping();
                    }
                    catch(Ice.LocalException ex)
                    {
                        test(false);
                    }
                    fact.destroyServer(server);
                    comm.destroy();
                }
                //
                // Test common name.
                //
                {
                    initData = createClientProps(defaultProperties, defaultDir, defaultHost, "c_rsa_ca1", "cacert1");
                    initData.properties.setProperty("IceSSL.CheckCertName", "1");
                    comm = Ice.Util.initialize(args, initData);

                    fact = ServerFactoryPrxHelper.checkedCast(comm.stringToProxy(factoryRef));
                    test(fact != null);
                    d = createServerProps(defaultProperties, defaultDir, defaultHost, "s_rsa_ca1_cn1", "cacert1");
                    server = fact.createServer(d);
                    try
                    {
                        server.ice_ping();
                    }
                    catch(Ice.LocalException ex)
                    {
                        test(false);
                    }
                    fact.destroyServer(server);
                    comm.destroy();
                }
                //
                // Test common name again. The certificate used in this test has "127.0.0.11" as its
                // common name, therefore the address "127.0.0.1" must NOT match.
                //
                {
                    initData = createClientProps(defaultProperties, defaultDir, defaultHost, "c_rsa_ca1", "cacert1");
                    initData.properties.setProperty("IceSSL.CheckCertName", "1");
                    comm = Ice.Util.initialize(args, initData);

                    fact = ServerFactoryPrxHelper.checkedCast(comm.stringToProxy(factoryRef));
                    test(fact != null);
                    d = createServerProps(defaultProperties, defaultDir, defaultHost, "s_rsa_ca1_cn2", "cacert1");
                    server = fact.createServer(d);
                    try
                    {
                        server.ice_ping();
                        test(false);
                    }
                    catch(Ice.LocalException ex)
                    {
                        // Expected.
                    }
                    fact.destroyServer(server);
                    comm.destroy();
                }
            }
        }
        out.println("ok");

        Ice.InitializationData initData;
        java.util.Map<String, String> d;

        out.print("testing certificate chains... ");
        out.flush();
        {
            IceSSL.NativeConnectionInfo info;

            initData = createClientProps(defaultProperties, defaultDir, defaultHost, "", "");
            initData.properties.setProperty("IceSSL.VerifyPeer", "0");
            Ice.Communicator comm = Ice.Util.initialize(initData);

            ServerFactoryPrx fact = ServerFactoryPrxHelper.checkedCast(comm.stringToProxy(factoryRef));
            test(fact != null);

            //
            // The client can't verify the server certificate but it should
            // still provide it. "s_rsa_ca1" doesn't include the root so the
            // cert size should be 1.
            //
            d = createServerProps(defaultProperties, defaultDir, defaultHost, "s_rsa_ca1", "");
            d.put("IceSSL.VerifyPeer", "0");
            ServerPrx server = fact.createServer(d);
            try
            {
                info = (IceSSL.NativeConnectionInfo)server.ice_getConnection().getInfo();
                test(info.nativeCerts.length == 1);
                test(!info.verified);
            }
            catch(Ice.LocalException ex)
            {
                test(false);
            }
            fact.destroyServer(server);

            //
            // Setting the CA for the server shouldn't change anything, it
            // shouldn't modify the cert chain sent to the client.
            //
            d = createServerProps(defaultProperties, defaultDir, defaultHost, "s_rsa_ca1", "cacert1");
            d.put("IceSSL.VerifyPeer", "0");
            server = fact.createServer(d);
            try
            {
                info = (IceSSL.NativeConnectionInfo)server.ice_getConnection().getInfo();
                test(info.nativeCerts.length == 1);
                test(!info.verified);
            }
            catch(Ice.LocalException ex)
            {
                test(false);
            }
            fact.destroyServer(server);

            //
            // The client can't verify the server certificate but should
            // still provide it. "s_rsa_wroot_ca1" includes the root so
            // the cert size should be 2.
            //
            d = createServerProps(defaultProperties, defaultDir, defaultHost, "s_rsa_wroot_ca1", "");
            d.put("IceSSL.VerifyPeer", "0");
            server = fact.createServer(d);
            try
            {
                info = (IceSSL.NativeConnectionInfo)server.ice_getConnection().getInfo();
                test(info.nativeCerts.length == 2);
            }
            catch(Ice.LocalException ex)
            {
                test(false);
            }
            fact.destroyServer(server);
            comm.destroy();

            //
            // Now the client verifies the server certificate
            //
            initData = createClientProps(defaultProperties, defaultDir, defaultHost, "", "cacert1");
            initData.properties.setProperty("IceSSL.VerifyPeer", "1");
            comm = Ice.Util.initialize(initData);

            fact = ServerFactoryPrxHelper.checkedCast(comm.stringToProxy(factoryRef));
            test(fact != null);

            {
                d = createServerProps(defaultProperties, defaultDir, defaultHost, "s_rsa_ca1", "");
                d.put("IceSSL.VerifyPeer", "0");
                server = fact.createServer(d);
                try
                {
                    info = (IceSSL.NativeConnectionInfo)server.ice_getConnection().getInfo();
                    test(info.nativeCerts.length == 2);
                    test(info.verified);
                }
                catch(Ice.LocalException ex)
                {
                    test(false);
                }
                fact.destroyServer(server);
            }

            comm.destroy();

            //
            // Try certificate with one intermediate and VerifyDepthMax=2
            //
            initData = createClientProps(defaultProperties, defaultDir, defaultHost, "", "cacert1");
            initData.properties.setProperty("IceSSL.VerifyPeer", "1");
            initData.properties.setProperty("IceSSL.VerifyDepthMax", "2");
            comm = Ice.Util.initialize(initData);

            fact = ServerFactoryPrxHelper.checkedCast(comm.stringToProxy(factoryRef));
            test(fact != null);

            {
                d = createServerProps(defaultProperties, defaultDir, defaultHost, "s_rsa_cai1", "");
                d.put("IceSSL.VerifyPeer", "0");
                server = fact.createServer(d);
                try
                {
                    server.ice_getConnection().getInfo();
                    test(false);
                }
                catch(Ice.SecurityException ex)
                {
                    // Chain length too long
                }
                catch(Ice.LocalException ex)
                {
                    test(false);
                }
                fact.destroyServer(server);
            }
            comm.destroy();

            //
            // Try with VerifyDepthMax to 3 (the default)
            //
            initData = createClientProps(defaultProperties, defaultDir, defaultHost, "", "cacert1");
            initData.properties.setProperty("IceSSL.VerifyPeer", "1");
            //initData.properties.setProperty("IceSSL.VerifyDepthMax", "3");
            comm = Ice.Util.initialize(initData);

            fact = ServerFactoryPrxHelper.checkedCast(comm.stringToProxy(factoryRef));
            test(fact != null);

            {
                d = createServerProps(defaultProperties, defaultDir, defaultHost, "s_rsa_cai1", "");
                d.put("IceSSL.VerifyPeer", "0");
                server = fact.createServer(d);
                try
                {
                    info = (IceSSL.NativeConnectionInfo)server.ice_getConnection().getInfo();
                    test(info.nativeCerts.length == 3);
                    test(info.verified);
                }
                catch(Ice.LocalException ex)
                {
                    test(false);
                }
                fact.destroyServer(server);
            }

            {
                d = createServerProps(defaultProperties, defaultDir, defaultHost, "s_rsa_cai2", "");
                d.put("IceSSL.VerifyPeer", "0");
                server = fact.createServer(d);
                try
                {
                    server.ice_getConnection().getInfo();
                    test(false);
                }
                catch(Ice.SecurityException ex)
                {
                    // Chain length too long
                }
                fact.destroyServer(server);
            }
            comm.destroy();

            //
            // Increase VerifyDepthMax to 4
            //
            initData = createClientProps(defaultProperties, defaultDir, defaultHost, "", "cacert1");
            initData.properties.setProperty("IceSSL.VerifyPeer", "1");
            initData.properties.setProperty("IceSSL.VerifyDepthMax", "4");
            comm = Ice.Util.initialize(initData);

            fact = ServerFactoryPrxHelper.checkedCast(comm.stringToProxy(factoryRef));
            test(fact != null);

            {
                d = createServerProps(defaultProperties, defaultDir, defaultHost, "s_rsa_cai2", "");
                d.put("IceSSL.VerifyPeer", "0");
                server = fact.createServer(d);
                try
                {
                    info = (IceSSL.NativeConnectionInfo)server.ice_getConnection().getInfo();
                    test(info.nativeCerts.length == 4);
                    test(info.verified);
                }
                catch(Ice.LocalException ex)
                {
                    test(false);
                }
                fact.destroyServer(server);
            }

            comm.destroy();

            //
            // Increase VerifyDepthMax to 4
            //
            initData = createClientProps(defaultProperties, defaultDir, defaultHost, "c_rsa_cai2", "cacert1");
            initData.properties.setProperty("IceSSL.VerifyPeer", "1");
            initData.properties.setProperty("IceSSL.VerifyDepthMax", "4");
            comm = Ice.Util.initialize(initData);

            fact = ServerFactoryPrxHelper.checkedCast(comm.stringToProxy(factoryRef));
            test(fact != null);

            {
                d = createServerProps(defaultProperties, defaultDir, defaultHost, "s_rsa_cai2", "cacert1");
                d.put("IceSSL.VerifyPeer", "2");
                server = fact.createServer(d);
                try
                {
                    server.ice_getConnection();
                    test(false);
                }
                catch(Ice.ProtocolException ex)
                {
                    // Expected
                }
                catch(Ice.ConnectionLostException ex)
                {
                    // Expected
                }
                catch(Ice.LocalException ex)
                {
                    test(false);
                }
                fact.destroyServer(server);
            }

            {
                d = createServerProps(defaultProperties, defaultDir, defaultHost, "s_rsa_cai2", "cacert1");
                d.put("IceSSL.VerifyPeer", "2");
                d.put("IceSSL.VerifyDepthMax", "4");
                server = fact.createServer(d);
                try
                {
                    server.ice_getConnection();
                }
                catch(Ice.LocalException ex)
                {
                    test(false);
                }
                fact.destroyServer(server);
            }

            comm.destroy();
        }
        out.println("ok");

        out.print("testing custom certificate verifier... ");
        out.flush();
        {
            //
            // ADH is allowed but will not have a certificate.
            //
            initData = createClientProps(defaultProperties, defaultDir, defaultHost);
            initData.properties.setProperty("IceSSL.Ciphers", "NONE (.*DH_anon.*AES.*)");
            initData.properties.setProperty("IceSSL.VerifyPeer", "0");
            Ice.Communicator comm = Ice.Util.initialize(args, initData);
            IceSSL.Plugin plugin = (IceSSL.Plugin)comm.getPluginManager().getPlugin("IceSSL");
            test(plugin != null);
            CertificateVerifierI verifier = new CertificateVerifierI();
            plugin.setCertificateVerifier(verifier);

            ServerFactoryPrx fact = ServerFactoryPrxHelper.checkedCast(comm.stringToProxy(factoryRef));
            test(fact != null);
            d = createServerProps(defaultProperties, defaultDir, defaultHost);
            d.put("IceSSL.Ciphers", "NONE (.*DH_anon.*AES.*)");
            d.put("IceSSL.VerifyPeer", "0");
            ServerPrx server = fact.createServer(d);
            try
            {
                String cipherSub = "DH_anon";
                server.checkCipher(cipherSub);
                IceSSL.NativeConnectionInfo info = (IceSSL.NativeConnectionInfo)server.ice_getConnection().getInfo();
                test(info.cipher.indexOf(cipherSub) >= 0);
            }
            catch(Ice.LocalException ex)
            {
                test(false);
            }
            test(verifier.invoked());
            test(!verifier.hadCert());

            //
            // Have the verifier return false. Close the connection explicitly
            // to force a new connection to be established.
            //
            verifier.reset();
            verifier.returnValue(false);
            server.ice_getConnection().close(false);
            try
            {
                server.ice_ping();
                test(false);
            }
            catch(Ice.SecurityException ex)
            {
                // Expected.
            }
            catch(Ice.LocalException ex)
            {
                test(false);
            }
            test(verifier.invoked());
            test(!verifier.hadCert());

            fact.destroyServer(server);
            comm.destroy();
        }

        {
            //
            // Verify that a server certificate is present.
            //
            initData = createClientProps(defaultProperties, defaultDir, defaultHost, "c_rsa_ca1", "cacert1");
            Ice.Communicator comm = Ice.Util.initialize(args, initData);
            IceSSL.Plugin plugin = (IceSSL.Plugin)comm.getPluginManager().getPlugin("IceSSL");
            test(plugin != null);
            CertificateVerifierI verifier = new CertificateVerifierI();
            plugin.setCertificateVerifier(verifier);

            ServerFactoryPrx fact = ServerFactoryPrxHelper.checkedCast(comm.stringToProxy(factoryRef));
            test(fact != null);
            d = createServerProps(defaultProperties, defaultDir, defaultHost, "s_rsa_ca1", "cacert1");
            d.put("IceSSL.VerifyPeer", "2");
            ServerPrx server = fact.createServer(d);
            try
            {
                server.ice_ping();
            }
            catch(Ice.LocalException ex)
            {
                test(false);
            }
            test(verifier.invoked());
            test(verifier.hadCert());
            fact.destroyServer(server);
            comm.destroy();
        }
        {
            //
            // Verify that verifier is installed via property.
            //
            initData = createClientProps(defaultProperties, defaultDir, defaultHost, "c_rsa_ca1", "cacert1");
            initData.properties.setProperty("IceSSL.CertVerifier", "test.IceSSL.configuration.CertificateVerifierI");
            Ice.Communicator comm = Ice.Util.initialize(args, initData);
            IceSSL.Plugin plugin = (IceSSL.Plugin)comm.getPluginManager().getPlugin("IceSSL");
            test(plugin != null);
            test(plugin.getCertificateVerifier() != null);
            comm.destroy();
        }
        out.println("ok");

        out.print("testing protocols... ");
        out.flush();
        {
            //
            // This should fail because the client and server have no protocol
            // in common.
            //
            initData = createClientProps(defaultProperties, defaultDir, defaultHost, "c_rsa_ca1", "cacert1");
            initData.properties.setProperty("IceSSL.Protocols", "ssl3");
            Ice.Communicator comm = Ice.Util.initialize(args, initData);
            ServerFactoryPrx fact = ServerFactoryPrxHelper.checkedCast(comm.stringToProxy(factoryRef));
            test(fact != null);
            d = createServerProps(defaultProperties, defaultDir, defaultHost, "s_rsa_ca1", "cacert1");
            d.put("IceSSL.VerifyPeer", "2");
            d.put("IceSSL.Protocols", "tls1");
            ServerPrx server = fact.createServer(d);
            try
            {
                server.ice_ping();
                test(false);
            }
            catch(Ice.SecurityException ex)
            {
                // Expected.
            }
            catch(Ice.ConnectionLostException ex)
            {
                // Expected for thread pool.
            }
            catch(Ice.LocalException ex)
            {
                test(false);
            }
            fact.destroyServer(server);
            comm.destroy();

            //
            // SSLv3 has been disabled by default in latests JDK updates.
            //
//             //
//             // SSLv3 is disabled by default in the IBM JDK.
//             //
//             if(System.getProperty("java.vendor").toLowerCase().indexOf("ibm") == -1)
//             {
//                 //
//                 // This should succeed.
//                 //
//                 comm = Ice.Util.initialize(args, initData);
//                 fact = ServerFactoryPrxHelper.checkedCast(comm.stringToProxy(factoryRef));
//                 test(fact != null);
//                 d = createServerProps(defaultProperties, defaultDir, defaultHost, "s_rsa_ca1", "cacert1");
//                 d.put("IceSSL.VerifyPeer", "2");
//                 d.put("IceSSL.Protocols", "tls1, ssl3");
//                 server = fact.createServer(d);
//                 try
//                 {
//                     server.ice_ping();
//                 }
//                 catch(Ice.LocalException ex)
//                 {
//                     ex.printStackTrace();
//                     test(false);
//                 }
//                 fact.destroyServer(server);
//                 comm.destroy();
//             }
        }

        {
            //
            // This should fail because the client ony enables SSLv3 and the server
            // uses the default protocol set that disables SSLv3
            //
            initData = createClientProps(defaultProperties, defaultDir, defaultHost, "c_rsa_ca1", "cacert1");
            initData.properties.setProperty("IceSSL.Protocols", "ssl3");
            Ice.Communicator comm = Ice.Util.initialize(args, initData);
            ServerFactoryPrx fact = ServerFactoryPrxHelper.checkedCast(comm.stringToProxy(factoryRef));
            test(fact != null);
            d = createServerProps(defaultProperties, defaultDir, defaultHost, "s_rsa_ca1", "cacert1");
            d.put("IceSSL.VerifyPeer", "2");
            ServerPrx server = fact.createServer(d);
            try
            {
                server.ice_ping();
                test(false);
            }
            catch(Ice.SecurityException ex)
            {
                // Expected.
            }
            catch(Ice.ConnectionLostException ex)
            {
                // Expected for thread pool.
            }
            catch(Ice.LocalException ex)
            {
                test(false);
            }
            fact.destroyServer(server);
            comm.destroy();
        }

        {
            //
            // This should success because the client and the server enables SSLv3
            //
            initData = createClientProps(defaultProperties, defaultDir, defaultHost, "c_rsa_ca1", "cacert1");
            initData.properties.setProperty("IceSSL.Protocols", "ssl3");
            Ice.Communicator comm = Ice.Util.initialize(args, initData);
            ServerFactoryPrx fact = ServerFactoryPrxHelper.checkedCast(comm.stringToProxy(factoryRef));
            test(fact != null);
            d = createServerProps(defaultProperties, defaultDir, defaultHost, "s_rsa_ca1", "cacert1");
            d.put("IceSSL.VerifyPeer", "2");
            d.put("IceSSL.Protocols", "ssl3, tls1_0, tls1_1, tls1_2");
            ServerPrx server = fact.createServer(d);
            try
            {
                server.ice_ping();
            }
            catch(Ice.SecurityException ex)
            {
                // Expected.
            }
            catch(Ice.ConnectionLostException ex)
            {
                // Expected for thread pool.
            }
            catch(Ice.LocalException ex)
            {
                test(false);
            }
            fact.destroyServer(server);
            comm.destroy();
        }

        out.println("ok");

        out.print("testing expired certificates... ");
        out.flush();
        {
            //
            // This should fail because the server's certificate is expired.
            //
            initData = createClientProps(defaultProperties, defaultDir, defaultHost, "c_rsa_ca1", "cacert1");
            Ice.Communicator comm = Ice.Util.initialize(args, initData);
            ServerFactoryPrx fact = ServerFactoryPrxHelper.checkedCast(comm.stringToProxy(factoryRef));
            test(fact != null);
            d = createServerProps(defaultProperties, defaultDir, defaultHost, "s_rsa_ca1_exp", "cacert1");
            d.put("IceSSL.VerifyPeer", "2");
            ServerPrx server = fact.createServer(d);
            try
            {
                server.ice_ping();
                test(false);
            }
            catch(Ice.SecurityException ex)
            {
                // Expected.
            }
            catch(Ice.LocalException ex)
            {
                test(false);
            }
            fact.destroyServer(server);
            comm.destroy();

            //
            // This should fail because the client's certificate is expired.
            //
            initData.properties.setProperty("IceSSL.Keystore", "c_rsa_ca1_exp.jks");
            initData.properties.setProperty("IceSSL.Truststore", "cacert1.jks");
            comm = Ice.Util.initialize(args, initData);
            fact = ServerFactoryPrxHelper.checkedCast(comm.stringToProxy(factoryRef));
            test(fact != null);
            d = createServerProps(defaultProperties, defaultDir, defaultHost, "s_rsa_ca1", "cacert1");
            d.put("IceSSL.VerifyPeer", "2");
            server = fact.createServer(d);
            try
            {
                server.ice_ping();
                test(false);
            }
            catch(Ice.ConnectionLostException ex)
            {
                // Expected.
            }
            catch(Ice.SecurityException ex)
            {
                // Expected.
            }
            catch(Ice.LocalException ex)
            {
                test(false);
            }
            fact.destroyServer(server);
            comm.destroy();
        }
        out.println("ok");

        out.print("testing multiple CA certificates... ");
        out.flush();
        {
            initData = createClientProps(defaultProperties, defaultDir, defaultHost, "c_rsa_ca1", "cacerts");
            Ice.Communicator comm = Ice.Util.initialize(args, initData);
            ServerFactoryPrx fact = ServerFactoryPrxHelper.checkedCast(comm.stringToProxy(factoryRef));
            test(fact != null);
            d = createServerProps(defaultProperties, defaultDir, defaultHost, "s_rsa_ca2", "cacerts");
            d.put("IceSSL.VerifyPeer", "2");
            ServerPrx server = fact.createServer(d);
            try
            {
                server.ice_ping();
            }
            catch(Ice.LocalException ex)
            {
                test(false);
            }
            fact.destroyServer(server);
            comm.destroy();
        }
        out.println("ok");

        out.print("testing passwords... ");
        out.flush();
        {
            //
            // Test password failure.
            //
            initData = createClientProps(defaultProperties, defaultDir, defaultHost);
            initData.properties.setProperty("IceSSL.Keystore", "c_rsa_ca1.jks");
            // Don't specify the password.
            try
            {
                Ice.Util.initialize(args, initData);
                test(false);
            }
            catch(Ice.PluginInitializationException ex)
            {
                // Expected.
            }
            catch(Ice.LocalException ex)
            {
                test(false);
            }
        }
        {
            //
            // Test password failure with callback.
            //
            initData = createClientProps(defaultProperties, defaultDir, defaultHost);
            initData.properties.setProperty("IceSSL.Keystore", "c_rsa_ca1.jks");
            initData.properties.setProperty("Ice.InitPlugins", "0");
            Ice.Communicator comm = Ice.Util.initialize(args, initData);
            Ice.PluginManager pm = comm.getPluginManager();
            IceSSL.Plugin plugin = (IceSSL.Plugin)pm.getPlugin("IceSSL");
            test(plugin != null);
            PasswordCallbackI cb = new PasswordCallbackI("bogus");
            plugin.setPasswordCallback(cb);
            try
            {
                pm.initializePlugins();
                test(false);
            }
            catch(Ice.PluginInitializationException ex)
            {
                // Expected.
            }
            catch(Ice.LocalException ex)
            {
                test(false);
            }
            comm.destroy();
        }
        {
            //
            // Test installation of password callback.
            //
            initData = createClientProps(defaultProperties, defaultDir, defaultHost);
            initData.properties.setProperty("IceSSL.Keystore", "c_rsa_ca1.jks");
            initData.properties.setProperty("Ice.InitPlugins", "0");
            Ice.Communicator comm = Ice.Util.initialize(args, initData);
            Ice.PluginManager pm = comm.getPluginManager();
            IceSSL.Plugin plugin = (IceSSL.Plugin)pm.getPlugin("IceSSL");
            test(plugin != null);
            PasswordCallbackI cb = new PasswordCallbackI();
            plugin.setPasswordCallback(cb);
            test(plugin.getPasswordCallback() == cb);
            try
            {
                pm.initializePlugins();
            }
            catch(Ice.LocalException ex)
            {
                test(false);
            }
            comm.destroy();
        }
        {
            //
            // Test password callback property.
            //
            initData = createClientProps(defaultProperties, defaultDir, defaultHost);
            initData.properties.setProperty("IceSSL.Keystore", "c_rsa_ca1.jks");
            initData.properties.setProperty("IceSSL.PasswordCallback", "test.IceSSL.configuration.PasswordCallbackI");
            Ice.Communicator comm = Ice.Util.initialize(args, initData);
            Ice.PluginManager pm = comm.getPluginManager();
            IceSSL.Plugin plugin = (IceSSL.Plugin)pm.getPlugin("IceSSL");
            test(plugin != null);
            test(plugin.getPasswordCallback() != null);
            comm.destroy();
        }
        out.println("ok");

        out.print("testing ciphers... ");
        out.flush();
        {
            //
            // The server has a certificate but the client doesn't. They should
            // negotiate to use ADH since we explicitly enable it.
            //
            initData = createClientProps(defaultProperties, defaultDir, defaultHost);
            initData.properties.setProperty("IceSSL.Ciphers", "NONE (.*DH_anon.*AES.*)");
            initData.properties.setProperty("IceSSL.VerifyPeer", "0");
            Ice.Communicator comm = Ice.Util.initialize(args, initData);
            ServerFactoryPrx fact = ServerFactoryPrxHelper.checkedCast(comm.stringToProxy(factoryRef));
            test(fact != null);
            d = createServerProps(defaultProperties, defaultDir, defaultHost, "s_rsa_ca1", "cacert1");
            d.put("IceSSL.Ciphers", "ALL");
            d.put("IceSSL.VerifyPeer", "1");
            ServerPrx server = fact.createServer(d);
            try
            {
                server.ice_ping();
            }
            catch(Ice.LocalException ex)
            {
                test(false);
            }
            fact.destroyServer(server);
            comm.destroy();
        }
        {
            //
            // Configure a server with RSA and DSA certificates.
            //
            // First try a client with a DSA certificate.
            //
            initData = createClientProps(defaultProperties, defaultDir, defaultHost, "c_dsa_ca1", "cacert1");
            Ice.Communicator comm = Ice.Util.initialize(args, initData);
            ServerFactoryPrx fact = ServerFactoryPrxHelper.checkedCast(comm.stringToProxy(factoryRef));
            test(fact != null);
            d = createServerProps(defaultProperties, defaultDir, defaultHost, "s_rsa_dsa_ca1", "cacert1");
            d.put("IceSSL.VerifyPeer", "1");
            ServerPrx server = fact.createServer(d);
            try
            {
                server.ice_ping();
            }
            catch(Ice.LocalException ex)
            {
                test(false);
            }
            fact.destroyServer(server);
            comm.destroy();

            //
            // Next try a client with an RSA certificate.
            //
            initData = createClientProps(defaultProperties, defaultDir, defaultHost, "c_rsa_ca1", "cacert1");
            comm = Ice.Util.initialize(args, initData);
            fact = ServerFactoryPrxHelper.checkedCast(comm.stringToProxy(factoryRef));
            test(fact != null);
            d = createServerProps(defaultProperties, defaultDir, defaultHost, "s_rsa_dsa_ca1", "cacert1");
            d.put("IceSSL.VerifyPeer", "1");
            server = fact.createServer(d);
            try
            {
                server.ice_ping();
            }
            catch(Ice.LocalException ex)
            {
                test(false);
            }
            fact.destroyServer(server);
            comm.destroy();

            //
            // Next try a client with ADH. This should fail.
            //
            initData = createClientProps(defaultProperties, defaultDir, defaultHost);
            initData.properties.setProperty("IceSSL.Ciphers", "NONE (.*DH_anon.*AES.*)");
            comm = Ice.Util.initialize(args, initData);
            fact = ServerFactoryPrxHelper.checkedCast(comm.stringToProxy(factoryRef));
            test(fact != null);
            d = createServerProps(defaultProperties, defaultDir, defaultHost, "s_rsa_dsa_ca1", "cacert1");
            d.put("IceSSL.VerifyPeer", "1");
            server = fact.createServer(d);
            try
            {
                server.ice_ping();
                test(false);
            }
            catch(Ice.SecurityException ex)
            {
                // Expected.
            }
            catch(Ice.ConnectionLostException ex)
            {
                // Expected for thread pool.
            }
            catch(Ice.LocalException ex)
            {
                test(false);
            }
            fact.destroyServer(server);
            comm.destroy();
        }
        {
            //
            // Configure a server with RSA and a client with DSA. This should fail.
            //
            initData = createClientProps(defaultProperties, defaultDir, defaultHost, "c_dsa_ca1", "cacert1");
            initData.properties.setProperty("IceSSL.Ciphers", "NONE (.*DSS.*)");
            Ice.Communicator comm = Ice.Util.initialize(args, initData);
            ServerFactoryPrx fact = ServerFactoryPrxHelper.checkedCast(comm.stringToProxy(factoryRef));
            test(fact != null);
            d = createServerProps(defaultProperties, defaultDir, defaultHost, "s_rsa_ca1", "cacert1");
            d.put("IceSSL.VerifyPeer", "2");
            ServerPrx server = fact.createServer(d);
            try
            {
                server.ice_ping();
                test(false);
            }
            catch(Ice.SecurityException ex)
            {
                // Expected.
            }
            catch(Ice.ConnectionLostException ex)
            {
                // Expected for thread pool.
            }
            catch(Ice.LocalException ex)
            {
                test(false);
            }
            fact.destroyServer(server);
            comm.destroy();
        }
        {
            //
            // Configure the server with both RSA and DSA certificates, but use the
            // Alias property to select the RSA certificate. This should fail.
            //
            initData = createClientProps(defaultProperties, defaultDir, defaultHost, "c_dsa_ca1", "cacert1");
            initData.properties.setProperty("IceSSL.Ciphers", "NONE (.*DSS.*)");
            Ice.Communicator comm = Ice.Util.initialize(args, initData);
            ServerFactoryPrx fact = ServerFactoryPrxHelper.checkedCast(comm.stringToProxy(factoryRef));
            test(fact != null);
            d = createServerProps(defaultProperties, defaultDir, defaultHost, "s_rsa_dsa_ca1", "cacert1");
            d.put("IceSSL.Alias", "rsacert");
            d.put("IceSSL.VerifyPeer", "2");
            ServerPrx server = fact.createServer(d);
            try
            {
                server.ice_ping();
                test(false);
            }
            catch(Ice.SecurityException ex)
            {
                // Expected.
            }
            catch(Ice.ConnectionLostException ex)
            {
                // Expected for thread pool.
            }
            catch(Ice.LocalException ex)
            {
                test(false);
            }
            fact.destroyServer(server);
            comm.destroy();
        }
        {
            //
            // Configure the server with both RSA and DSA certificates, but use the
            // Alias property to select the DSA certificate. This should succeed.
            //
            initData = createClientProps(defaultProperties, defaultDir, defaultHost, "cacert1", "");
            Ice.Communicator comm = Ice.Util.initialize(args, initData);
            ServerFactoryPrx fact = ServerFactoryPrxHelper.checkedCast(comm.stringToProxy(factoryRef));
            test(fact != null);
            d = createServerProps(defaultProperties, defaultDir, defaultHost, "s_rsa_dsa_ca1", "cacert1");
            d.put("IceSSL.Alias", "dsacert");
            d.put("IceSSL.VerifyPeer", "1");
            ServerPrx server = fact.createServer(d);
            try
            {
                server.ice_ping();
                //
                // RSA is used by default, so we examine the negotiated cipher to determine whether
                // DSA was actually used.
                //
                IceSSL.ConnectionInfo info = (IceSSL.ConnectionInfo)server.ice_getConnection().getInfo();
                test(info.cipher.toLowerCase().contains("dss"));
            }
            catch(Ice.LocalException ex)
            {
                test(false);
            }
            fact.destroyServer(server);
            comm.destroy();
        }
        out.println("ok");

        out.print("testing IceSSL.TrustOnly... ");
        out.flush();
        {
            initData = createClientProps(defaultProperties, defaultDir, defaultHost, "c_rsa_ca1", "cacert1");
            initData.properties.setProperty("IceSSL.TrustOnly",
                "C=US, ST=Florida, O=ZeroC\\, Inc., OU=Ice, emailAddress=info@zeroc.com, CN=Server");
            Ice.Communicator comm = Ice.Util.initialize(args, initData);

            ServerFactoryPrx fact = ServerFactoryPrxHelper.checkedCast(comm.stringToProxy(factoryRef));
            test(fact != null);
            d = createServerProps(defaultProperties, defaultDir, defaultHost, "s_rsa_dsa_ca1", "cacert1");
            ServerPrx server = fact.createServer(d);
            try
            {
                server.ice_ping();
            }
            catch(Ice.LocalException ex)
            {
                test(false);
            }
            fact.destroyServer(server);
            comm.destroy();
        }
        {
            initData = createClientProps(defaultProperties, defaultDir, defaultHost, "c_rsa_ca1", "cacert1");
            initData.properties.setProperty("IceSSL.TrustOnly",
                "!C=US, ST=Florida, O=ZeroC\\, Inc., OU=Ice, emailAddress=info@zeroc.com, CN=Server");
            Ice.Communicator comm = Ice.Util.initialize(args, initData);

            ServerFactoryPrx fact = ServerFactoryPrxHelper.checkedCast(comm.stringToProxy(factoryRef));
            test(fact != null);
            d = createServerProps(defaultProperties, defaultDir, defaultHost, "s_rsa_dsa_ca1", "cacert1");
            ServerPrx server = fact.createServer(d);
            try
            {
                server.ice_ping();
                test(false);
            }
            catch(Ice.LocalException ex)
            {
            }
            fact.destroyServer(server);
            comm.destroy();
        }
        {
            initData = createClientProps(defaultProperties, defaultDir, defaultHost, "c_rsa_ca1", "cacert1");
            initData.properties.setProperty("IceSSL.TrustOnly",
                "C=US, ST=Florida, O=\"ZeroC, Inc.\", OU=Ice, emailAddress=info@zeroc.com, CN=Server");
            Ice.Communicator comm = Ice.Util.initialize(args, initData);

            ServerFactoryPrx fact = ServerFactoryPrxHelper.checkedCast(comm.stringToProxy(factoryRef));
            test(fact != null);
            d = createServerProps(defaultProperties, defaultDir, defaultHost, "s_rsa_dsa_ca1", "cacert1");
            ServerPrx server = fact.createServer(d);
            try
            {
                server.ice_ping();
            }
            catch(Ice.LocalException ex)
            {
                test(false);
            }
            fact.destroyServer(server);
            comm.destroy();
        }
        {
            initData = createClientProps(defaultProperties, defaultDir, defaultHost, "c_rsa_ca1", "cacert1");
            Ice.Communicator comm = Ice.Util.initialize(args, initData);

            ServerFactoryPrx fact = ServerFactoryPrxHelper.checkedCast(comm.stringToProxy(factoryRef));
            test(fact != null);
            d = createServerProps(defaultProperties, defaultDir, defaultHost, "s_rsa_dsa_ca1", "cacert1");
            d.put("IceSSL.TrustOnly",
                  "C=US, ST=Florida, O=ZeroC\\, Inc., OU=Ice, emailAddress=info@zeroc.com, CN=Client");
            ServerPrx server = fact.createServer(d);
            try
            {
                server.ice_ping();
            }
            catch(Ice.LocalException ex)
            {
                test(false);
            }
            fact.destroyServer(server);
            comm.destroy();
        }
        {
            initData = createClientProps(defaultProperties, defaultDir, defaultHost, "c_rsa_ca1", "cacert1");
            Ice.Communicator comm = Ice.Util.initialize(args, initData);

            ServerFactoryPrx fact = ServerFactoryPrxHelper.checkedCast(comm.stringToProxy(factoryRef));
            test(fact != null);
            d = createServerProps(defaultProperties, defaultDir, defaultHost, "s_rsa_dsa_ca1", "cacert1");
            d.put("IceSSL.TrustOnly",
                  "!C=US, ST=Florida, O=ZeroC\\, Inc., OU=Ice, emailAddress=info@zeroc.com, CN=Client");
            ServerPrx server = fact.createServer(d);
            try
            {
                server.ice_ping();
                test(false);
            }
            catch(Ice.LocalException ex)
            {
            }
            fact.destroyServer(server);
            comm.destroy();
        }
        {
            initData = createClientProps(defaultProperties, defaultDir, defaultHost, "c_rsa_ca1", "cacert1");
            initData.properties.setProperty("IceSSL.TrustOnly", "CN=Server");
            Ice.Communicator comm = Ice.Util.initialize(args, initData);

            ServerFactoryPrx fact = ServerFactoryPrxHelper.checkedCast(comm.stringToProxy(factoryRef));
            test(fact != null);
            d = createServerProps(defaultProperties, defaultDir, defaultHost, "s_rsa_dsa_ca1", "cacert1");
            ServerPrx server = fact.createServer(d);
            try
            {
                server.ice_ping();
            }
            catch(Ice.LocalException ex)
            {
                test(false);
            }
            fact.destroyServer(server);
            comm.destroy();
        }
        {
            initData = createClientProps(defaultProperties, defaultDir, defaultHost, "c_rsa_ca1", "cacert1");
            initData.properties.setProperty("IceSSL.TrustOnly", "!CN=Server");
            Ice.Communicator comm = Ice.Util.initialize(args, initData);

            ServerFactoryPrx fact = ServerFactoryPrxHelper.checkedCast(comm.stringToProxy(factoryRef));
            test(fact != null);
            d = createServerProps(defaultProperties, defaultDir, defaultHost, "s_rsa_dsa_ca1", "cacert1");
            ServerPrx server = fact.createServer(d);
            try
            {
                server.ice_ping();
                test(false);
            }
            catch(Ice.LocalException ex)
            {
            }
            fact.destroyServer(server);
            comm.destroy();
        }
        {
            initData = createClientProps(defaultProperties, defaultDir, defaultHost, "c_rsa_ca1", "cacert1");
            Ice.Communicator comm = Ice.Util.initialize(args, initData);

            ServerFactoryPrx fact = ServerFactoryPrxHelper.checkedCast(comm.stringToProxy(factoryRef));
            test(fact != null);
            d = createServerProps(defaultProperties, defaultDir, defaultHost, "s_rsa_dsa_ca1", "cacert1");
            d.put("IceSSL.TrustOnly", "CN=Client");
            ServerPrx server = fact.createServer(d);
            try
            {
                server.ice_ping();
            }
            catch(Ice.LocalException ex)
            {
                test(false);
            }
            fact.destroyServer(server);
            comm.destroy();
        }
        {
            initData = createClientProps(defaultProperties, defaultDir, defaultHost, "c_rsa_ca1", "cacert1");
            Ice.Communicator comm = Ice.Util.initialize(args, initData);

            ServerFactoryPrx fact = ServerFactoryPrxHelper.checkedCast(comm.stringToProxy(factoryRef));
            test(fact != null);
            d = createServerProps(defaultProperties, defaultDir, defaultHost, "s_rsa_dsa_ca1", "cacert1");
            d.put("IceSSL.TrustOnly", "!CN=Client");
            ServerPrx server = fact.createServer(d);
            try
            {
                server.ice_ping();
                test(false);
            }
            catch(Ice.LocalException ex)
            {
            }
            fact.destroyServer(server);
            comm.destroy();
        }
        {
            initData = createClientProps(defaultProperties, defaultDir, defaultHost, "c_rsa_ca1", "cacert1");
            initData.properties.setProperty("IceSSL.TrustOnly", "CN=Client");
            Ice.Communicator comm = Ice.Util.initialize(args, initData);

            ServerFactoryPrx fact = ServerFactoryPrxHelper.checkedCast(comm.stringToProxy(factoryRef));
            test(fact != null);
            d = createServerProps(defaultProperties, defaultDir, defaultHost, "s_rsa_dsa_ca1", "cacert1");
            ServerPrx server = fact.createServer(d);
            try
            {
                server.ice_ping();
                test(false);
            }
            catch(Ice.LocalException ex)
            {
            }
            fact.destroyServer(server);
            comm.destroy();
        }
        {
            initData = createClientProps(defaultProperties, defaultDir, defaultHost, "c_rsa_ca1", "cacert1");
            Ice.Communicator comm = Ice.Util.initialize(args, initData);

            ServerFactoryPrx fact = ServerFactoryPrxHelper.checkedCast(comm.stringToProxy(factoryRef));
            test(fact != null);
            d = createServerProps(defaultProperties, defaultDir, defaultHost, "s_rsa_dsa_ca1", "cacert1");
            d.put("IceSSL.TrustOnly", "CN=Server");
            ServerPrx server = fact.createServer(d);
            try
            {
                server.ice_ping();
                test(false);
            }
            catch(Ice.LocalException ex)
            {
            }
            fact.destroyServer(server);
            comm.destroy();
        }
        {
            initData = createClientProps(defaultProperties, defaultDir, defaultHost, "c_rsa_ca1", "cacert1");
            initData.properties.setProperty("IceSSL.TrustOnly", "C=Canada,CN=Server");
            Ice.Communicator comm = Ice.Util.initialize(args, initData);

            ServerFactoryPrx fact = ServerFactoryPrxHelper.checkedCast(comm.stringToProxy(factoryRef));
            test(fact != null);
            d = createServerProps(defaultProperties, defaultDir, defaultHost, "s_rsa_dsa_ca1", "cacert1");
            ServerPrx server = fact.createServer(d);
            try
            {
                server.ice_ping();
                test(false);
            }
            catch(Ice.LocalException ex)
            {
            }
            fact.destroyServer(server);
            comm.destroy();
        }
        {
            initData = createClientProps(defaultProperties, defaultDir, defaultHost, "c_rsa_ca1", "cacert1");
            initData.properties.setProperty("IceSSL.TrustOnly", "!C=Canada,CN=Server");
            Ice.Communicator comm = Ice.Util.initialize(args, initData);

            ServerFactoryPrx fact = ServerFactoryPrxHelper.checkedCast(comm.stringToProxy(factoryRef));
            test(fact != null);
            d = createServerProps(defaultProperties, defaultDir, defaultHost, "s_rsa_dsa_ca1", "cacert1");
            ServerPrx server = fact.createServer(d);
            try
            {
                server.ice_ping();
            }
            catch(Ice.LocalException ex)
            {
                test(false);
            }
            fact.destroyServer(server);
            comm.destroy();
        }
        {
            initData = createClientProps(defaultProperties, defaultDir, defaultHost, "c_rsa_ca1", "cacert1");
            initData.properties.setProperty("IceSSL.TrustOnly", "C=Canada;CN=Server");
            Ice.Communicator comm = Ice.Util.initialize(args, initData);

            ServerFactoryPrx fact = ServerFactoryPrxHelper.checkedCast(comm.stringToProxy(factoryRef));
            test(fact != null);
            d = createServerProps(defaultProperties, defaultDir, defaultHost, "s_rsa_dsa_ca1", "cacert1");
            ServerPrx server = fact.createServer(d);
            try
            {
                server.ice_ping();
            }
            catch(Ice.LocalException ex)
            {
                test(false);
            }
            fact.destroyServer(server);
            comm.destroy();
        }
        {
            initData = createClientProps(defaultProperties, defaultDir, defaultHost, "c_rsa_ca1", "cacert1");
            initData.properties.setProperty("IceSSL.TrustOnly", "!C=Canada;!CN=Server");
            Ice.Communicator comm = Ice.Util.initialize(args, initData);

            ServerFactoryPrx fact = ServerFactoryPrxHelper.checkedCast(comm.stringToProxy(factoryRef));
            test(fact != null);
            d = createServerProps(defaultProperties, defaultDir, defaultHost, "s_rsa_dsa_ca1", "cacert1");
            ServerPrx server = fact.createServer(d);
            try
            {
                server.ice_ping();
                test(false);
            }
            catch(Ice.LocalException ex)
            {
            }
            fact.destroyServer(server);
            comm.destroy();
        }
        {
            initData = createClientProps(defaultProperties, defaultDir, defaultHost, "c_rsa_ca1", "cacert1");
            initData.properties.setProperty("IceSSL.TrustOnly", "!CN=Server1"); // Should not match "Server"
            Ice.Communicator comm = Ice.Util.initialize(args, initData);

            ServerFactoryPrx fact = ServerFactoryPrxHelper.checkedCast(comm.stringToProxy(factoryRef));
            test(fact != null);
            d = createServerProps(defaultProperties, defaultDir, defaultHost, "s_rsa_dsa_ca1", "cacert1");
            ServerPrx server = fact.createServer(d);
            try
            {
                server.ice_ping();
            }
            catch(Ice.LocalException ex)
            {
                test(false);
            }
            fact.destroyServer(server);
            comm.destroy();
        }
        {
            initData = createClientProps(defaultProperties, defaultDir, defaultHost, "c_rsa_ca1", "cacert1");
            Ice.Communicator comm = Ice.Util.initialize(args, initData);

            ServerFactoryPrx fact = ServerFactoryPrxHelper.checkedCast(comm.stringToProxy(factoryRef));
            test(fact != null);
            d = createServerProps(defaultProperties, defaultDir, defaultHost, "s_rsa_dsa_ca1", "cacert1");
            d.put("IceSSL.TrustOnly", "!CN=Client1"); // Should not match "Client"
            ServerPrx server = fact.createServer(d);
            try
            {
                server.ice_ping();
            }
            catch(Ice.LocalException ex)
            {
                test(false);
            }
            fact.destroyServer(server);
            comm.destroy();
        }
        {
            //
            // Test rejection when client does not supply a certificate.
            //
            initData = createClientProps(defaultProperties, defaultDir, defaultHost);
            initData.properties.setProperty("IceSSL.Ciphers", "NONE (.*DH_anon.*AES.*)");
            initData.properties.setProperty("IceSSL.VerifyPeer", "0");
            Ice.Communicator comm = Ice.Util.initialize(args, initData);

            ServerFactoryPrx fact = ServerFactoryPrxHelper.checkedCast(comm.stringToProxy(factoryRef));
            test(fact != null);
            d = createServerProps(defaultProperties, defaultDir, defaultHost);
            d.put("IceSSL.TrustOnly",
                  "C=US, ST=Florida, O=ZeroC\\, Inc., OU=Ice, emailAddress=info@zeroc.com, CN=Client");
            d.put("IceSSL.Ciphers", "NONE (.*DH_anon.*AES.*)");
            d.put("IceSSL.VerifyPeer", "0");
            ServerPrx server = fact.createServer(d);
            try
            {
                server.ice_ping();
                test(false);
            }
            catch(Ice.LocalException ex)
            {
            }
            fact.destroyServer(server);
            comm.destroy();
        }
        {
            //
            // Test rejection when client does not supply a certificate.
            //
            initData = createClientProps(defaultProperties, defaultDir, defaultHost);
            initData.properties.setProperty("IceSSL.Ciphers", "NONE (.*DH_anon.*AES.*)");
            initData.properties.setProperty("IceSSL.VerifyPeer", "0");
            Ice.Communicator comm = Ice.Util.initialize(args, initData);

            ServerFactoryPrx fact = ServerFactoryPrxHelper.checkedCast(comm.stringToProxy(factoryRef));
            test(fact != null);
            d = createServerProps(defaultProperties, defaultDir, defaultHost);
            d.put("IceSSL.TrustOnly",
                  "!C=US, ST=Florida, O=ZeroC\\, Inc., OU=Ice, emailAddress=info@zeroc.com, CN=Client");
            d.put("IceSSL.Ciphers", "NONE (.*DH_anon.*AES.*)");
            d.put("IceSSL.VerifyPeer", "0");
            ServerPrx server = fact.createServer(d);
            try
            {
                server.ice_ping();
                test(false);
            }
            catch(Ice.LocalException ex)
            {
            }
            fact.destroyServer(server);
            comm.destroy();
        }
        {
            //
            // Rejection takes precedence (client).
            //
            initData = createClientProps(defaultProperties, defaultDir, defaultHost, "c_rsa_ca1", "cacert1");
            initData.properties.setProperty("IceSSL.TrustOnly", "ST=Florida;!CN=Server;C=US");
            Ice.Communicator comm = Ice.Util.initialize(args, initData);

            ServerFactoryPrx fact = ServerFactoryPrxHelper.checkedCast(comm.stringToProxy(factoryRef));
            test(fact != null);
            d = createServerProps(defaultProperties, defaultDir, defaultHost, "s_rsa_dsa_ca1", "cacert1");
            ServerPrx server = fact.createServer(d);
            try
            {
                server.ice_ping();
                test(false);
            }
            catch(Ice.LocalException ex)
            {
            }
            fact.destroyServer(server);
            comm.destroy();
        }
        {
            //
            // Rejection takes precedence (server).
            //
            initData = createClientProps(defaultProperties, defaultDir, defaultHost, "c_rsa_ca1", "cacert1");
            Ice.Communicator comm = Ice.Util.initialize(args, initData);

            ServerFactoryPrx fact = ServerFactoryPrxHelper.checkedCast(comm.stringToProxy(factoryRef));
            test(fact != null);
            d = createServerProps(defaultProperties, defaultDir, defaultHost, "s_rsa_dsa_ca1", "cacert1");
            d.put("IceSSL.TrustOnly", "ST=Florida;!CN=Client;C=US");
            ServerPrx server = fact.createServer(d);
            try
            {
                server.ice_ping();
                test(false);
            }
            catch(Ice.LocalException ex)
            {
            }
            fact.destroyServer(server);
            comm.destroy();
        }
        out.println("ok");

        out.print("testing IceSSL.TrustOnly.Client... ");
        out.flush();
        {
            initData = createClientProps(defaultProperties, defaultDir, defaultHost, "c_rsa_ca1", "cacert1");
            initData.properties.setProperty("IceSSL.TrustOnly.Client",
                "C=US, ST=Florida, O=ZeroC\\, Inc., OU=Ice, emailAddress=info@zeroc.com, CN=Server");
            Ice.Communicator comm = Ice.Util.initialize(args, initData);

            ServerFactoryPrx fact = ServerFactoryPrxHelper.checkedCast(comm.stringToProxy(factoryRef));
            test(fact != null);
            d = createServerProps(defaultProperties, defaultDir, defaultHost, "s_rsa_dsa_ca1", "cacert1");
            // Should have no effect.
            d.put("IceSSL.TrustOnly.Client",
                "C=US, ST=Florida, O=ZeroC\\, Inc., OU=Ice, emailAddress=info@zeroc.com, CN=Server");
            ServerPrx server = fact.createServer(d);
            try
            {
                server.ice_ping();
            }
            catch(Ice.LocalException ex)
            {
                test(false);
            }
            fact.destroyServer(server);
            comm.destroy();
        }
        {
            initData = createClientProps(defaultProperties, defaultDir, defaultHost, "c_rsa_ca1", "cacert1");
            initData.properties.setProperty("IceSSL.TrustOnly.Client",
                "!C=US, ST=Florida, O=ZeroC\\, Inc., OU=Ice, emailAddress=info@zeroc.com, CN=Server");
            Ice.Communicator comm = Ice.Util.initialize(args, initData);

            ServerFactoryPrx fact = ServerFactoryPrxHelper.checkedCast(comm.stringToProxy(factoryRef));
            test(fact != null);
            d = createServerProps(defaultProperties, defaultDir, defaultHost, "s_rsa_dsa_ca1", "cacert1");
            ServerPrx server = fact.createServer(d);
            try
            {
                server.ice_ping();
                test(false);
            }
            catch(Ice.LocalException ex)
            {
            }
            fact.destroyServer(server);
            comm.destroy();
        }
        {
            initData = createClientProps(defaultProperties, defaultDir, defaultHost, "c_rsa_ca1", "cacert1");
            Ice.Communicator comm = Ice.Util.initialize(args, initData);

            ServerFactoryPrx fact = ServerFactoryPrxHelper.checkedCast(comm.stringToProxy(factoryRef));
            test(fact != null);
            d = createServerProps(defaultProperties, defaultDir, defaultHost, "s_rsa_dsa_ca1", "cacert1");
            // Should have no effect.
            d.put("IceSSL.TrustOnly.Client", "CN=Client");
            ServerPrx server = fact.createServer(d);
            try
            {
                server.ice_ping();
            }
            catch(Ice.LocalException ex)
            {
                test(false);
            }
            fact.destroyServer(server);
            comm.destroy();
        }
        {
            initData = createClientProps(defaultProperties, defaultDir, defaultHost, "c_rsa_ca1", "cacert1");
            initData.properties.setProperty("IceSSL.TrustOnly.Client", "CN=Client");
            Ice.Communicator comm = Ice.Util.initialize(args, initData);

            ServerFactoryPrx fact = ServerFactoryPrxHelper.checkedCast(comm.stringToProxy(factoryRef));
            test(fact != null);
            d = createServerProps(defaultProperties, defaultDir, defaultHost, "s_rsa_dsa_ca1", "cacert1");
            ServerPrx server = fact.createServer(d);
            try
            {
                server.ice_ping();
                test(false);
            }
            catch(Ice.LocalException ex)
            {
            }
            fact.destroyServer(server);
            comm.destroy();
        }
        {
            initData = createClientProps(defaultProperties, defaultDir, defaultHost, "c_rsa_ca1", "cacert1");
            initData.properties.setProperty("IceSSL.TrustOnly.Client", "!CN=Client");
            Ice.Communicator comm = Ice.Util.initialize(args, initData);

            ServerFactoryPrx fact = ServerFactoryPrxHelper.checkedCast(comm.stringToProxy(factoryRef));
            test(fact != null);
            d = createServerProps(defaultProperties, defaultDir, defaultHost, "s_rsa_dsa_ca1", "cacert1");
            ServerPrx server = fact.createServer(d);
            try
            {
                server.ice_ping();
            }
            catch(Ice.LocalException ex)
            {
                test(false);
            }
            fact.destroyServer(server);
            comm.destroy();
        }
        out.println("ok");

        out.print("testing IceSSL.TrustOnly.Server... ");
        out.flush();
        {
            initData = createClientProps(defaultProperties, defaultDir, defaultHost, "c_rsa_ca1", "cacert1");
            // Should have no effect.
            initData.properties.setProperty("IceSSL.TrustOnly.Server",
                "C=US, ST=Florida, O=ZeroC\\, Inc., OU=Ice, emailAddress=info@zeroc.com, CN=Client");
            Ice.Communicator comm = Ice.Util.initialize(args, initData);

            ServerFactoryPrx fact = ServerFactoryPrxHelper.checkedCast(comm.stringToProxy(factoryRef));
            test(fact != null);
            d = createServerProps(defaultProperties, defaultDir, defaultHost, "s_rsa_dsa_ca1", "cacert1");
            d.put("IceSSL.TrustOnly.Server",
                "C=US, ST=Florida, O=ZeroC\\, Inc., OU=Ice, emailAddress=info@zeroc.com, CN=Client");
            ServerPrx server = fact.createServer(d);
            try
            {
                server.ice_ping();
            }
            catch(Ice.LocalException ex)
            {
                test(false);
            }
            fact.destroyServer(server);
            comm.destroy();
        }
        {
            initData = createClientProps(defaultProperties, defaultDir, defaultHost, "c_rsa_ca1", "cacert1");
            Ice.Communicator comm = Ice.Util.initialize(args, initData);

            ServerFactoryPrx fact = ServerFactoryPrxHelper.checkedCast(comm.stringToProxy(factoryRef));
            test(fact != null);
            d = createServerProps(defaultProperties, defaultDir, defaultHost, "s_rsa_dsa_ca1", "cacert1");
            d.put("IceSSL.TrustOnly.Server",
                  "!C=US, ST=Florida, O=ZeroC\\, Inc., OU=Ice, emailAddress=info@zeroc.com, CN=Client");
            ServerPrx server = fact.createServer(d);
            try
            {
                server.ice_ping();
                test(false);
            }
            catch(Ice.LocalException ex)
            {
            }
            fact.destroyServer(server);
            comm.destroy();
        }
        {
            initData = createClientProps(defaultProperties, defaultDir, defaultHost, "c_rsa_ca1", "cacert1");
            // Should have no effect.
            initData.properties.setProperty("IceSSL.TrustOnly.Server", "!CN=Server");
            Ice.Communicator comm = Ice.Util.initialize(args, initData);

            ServerFactoryPrx fact = ServerFactoryPrxHelper.checkedCast(comm.stringToProxy(factoryRef));
            test(fact != null);
            d = createServerProps(defaultProperties, defaultDir, defaultHost, "s_rsa_dsa_ca1", "cacert1");
            ServerPrx server = fact.createServer(d);
            try
            {
                server.ice_ping();
            }
            catch(Ice.LocalException ex)
            {
                test(false);
            }
            fact.destroyServer(server);
            comm.destroy();
        }
        {
            initData = createClientProps(defaultProperties, defaultDir, defaultHost, "c_rsa_ca1", "cacert1");
            Ice.Communicator comm = Ice.Util.initialize(args, initData);

            ServerFactoryPrx fact = ServerFactoryPrxHelper.checkedCast(comm.stringToProxy(factoryRef));
            test(fact != null);
            d = createServerProps(defaultProperties, defaultDir, defaultHost, "s_rsa_dsa_ca1", "cacert1");
            d.put("IceSSL.TrustOnly.Server", "CN=Server");
            ServerPrx server = fact.createServer(d);
            try
            {
                server.ice_ping();
                test(false);
            }
            catch(Ice.LocalException ex)
            {
            }
            fact.destroyServer(server);
            comm.destroy();
        }
        {
            initData = createClientProps(defaultProperties, defaultDir, defaultHost, "c_rsa_ca1", "cacert1");
            Ice.Communicator comm = Ice.Util.initialize(args, initData);

            ServerFactoryPrx fact = ServerFactoryPrxHelper.checkedCast(comm.stringToProxy(factoryRef));
            test(fact != null);
            d = createServerProps(defaultProperties, defaultDir, defaultHost, "s_rsa_dsa_ca1", "cacert1");
            d.put("IceSSL.TrustOnly.Server", "!CN=Client");
            ServerPrx server = fact.createServer(d);
            try
            {
                server.ice_ping();
                test(false);
            }
            catch(Ice.LocalException ex)
            {
            }
            fact.destroyServer(server);
            comm.destroy();
        }
        out.println("ok");

        out.print("testing IceSSL.TrustOnly.Server.<AdapterName>... ");
        out.flush();
        {
            initData = createClientProps(defaultProperties, defaultDir, defaultHost, "c_rsa_ca1", "cacert1");
            Ice.Communicator comm = Ice.Util.initialize(args, initData);

            ServerFactoryPrx fact = ServerFactoryPrxHelper.checkedCast(comm.stringToProxy(factoryRef));
            test(fact != null);
            d = createServerProps(defaultProperties, defaultDir, defaultHost, "s_rsa_dsa_ca1", "cacert1");
            d.put("IceSSL.TrustOnly.Server", "CN=bogus");
            d.put("IceSSL.TrustOnly.Server.ServerAdapter",
                "C=US, ST=Florida, O=ZeroC\\, Inc., OU=Ice, emailAddress=info@zeroc.com, CN=Client");
            ServerPrx server = fact.createServer(d);
            try
            {
                server.ice_ping();
            }
            catch(Ice.LocalException ex)
            {
                test(false);
            }
            fact.destroyServer(server);
            comm.destroy();
        }
        {
            initData = createClientProps(defaultProperties, defaultDir, defaultHost, "c_rsa_ca1", "cacert1");
            Ice.Communicator comm = Ice.Util.initialize(args, initData);

            ServerFactoryPrx fact = ServerFactoryPrxHelper.checkedCast(comm.stringToProxy(factoryRef));
            test(fact != null);
            d = createServerProps(defaultProperties, defaultDir, defaultHost, "s_rsa_dsa_ca1", "cacert1");
            d.put("IceSSL.TrustOnly.Server.ServerAdapter",
                  "!C=US, ST=Florida, O=ZeroC\\, Inc., OU=Ice, emailAddress=info@zeroc.com, CN=Client");
            ServerPrx server = fact.createServer(d);
            try
            {
                server.ice_ping();
                test(false);
            }
            catch(Ice.LocalException ex)
            {
            }
            fact.destroyServer(server);
            comm.destroy();
        }
        {
            initData = createClientProps(defaultProperties, defaultDir, defaultHost, "c_rsa_ca1", "cacert1");
            Ice.Communicator comm = Ice.Util.initialize(args, initData);

            ServerFactoryPrx fact = ServerFactoryPrxHelper.checkedCast(comm.stringToProxy(factoryRef));
            test(fact != null);
            d = createServerProps(defaultProperties, defaultDir, defaultHost, "s_rsa_dsa_ca1", "cacert1");
            d.put("IceSSL.TrustOnly.Server.ServerAdapter", "CN=bogus");
            ServerPrx server = fact.createServer(d);
            try
            {
                server.ice_ping();
                test(false);
            }
            catch(Ice.LocalException ex)
            {
            }
            fact.destroyServer(server);
            comm.destroy();
        }
        {
            initData = createClientProps(defaultProperties, defaultDir, defaultHost, "c_rsa_ca1", "cacert1");
            Ice.Communicator comm = Ice.Util.initialize(args, initData);
            ServerFactoryPrx fact = ServerFactoryPrxHelper.checkedCast(comm.stringToProxy(factoryRef));
            test(fact != null);
            d = createServerProps(defaultProperties, defaultDir, defaultHost, "s_rsa_dsa_ca1", "cacert1");
            d.put("IceSSL.TrustOnly.Server.ServerAdapter", "!CN=bogus");
            ServerPrx server = fact.createServer(d);
            try
            {
                server.ice_ping();
            }
            catch(Ice.LocalException ex)
            {
                test(false);
            }
            fact.destroyServer(server);
            comm.destroy();
        }
        out.println("ok");

        out.print("testing system CAs... ");
        out.flush();
        {
            initData = createClientProps(defaultProperties, defaultDir, defaultHost);
            initData.properties.setProperty("IceSSL.VerifyDepthMax", "4");
            initData.properties.setProperty("Ice.Override.Timeout", "5000"); // 5s timeout
            Ice.Communicator comm = Ice.Util.initialize(initData);
            Ice.ObjectPrx p = comm.stringToProxy("dummy:wss -h demo.zeroc.com -p 5064");
            try
            {
                p.ice_ping();
                test(false);
            }
            catch(Ice.SecurityException ex)
            {
                // Expected, by default we don't check for system CAs.
            }
            catch(Ice.LocalException ex)
            {
                test(false);
            }

            initData = createClientProps(defaultProperties, defaultDir, defaultHost);
            initData.properties.setProperty("IceSSL.VerifyDepthMax", "4");
            initData.properties.setProperty("Ice.Override.Timeout", "5000"); // 5s timeout
            initData.properties.setProperty("IceSSL.UsePlatformCAs", "1");
            comm = Ice.Util.initialize(initData);
            p = comm.stringToProxy("dummy:wss -h demo.zeroc.com -p 5064");
            IceSSL.WSSConnectionInfo info;
            try
            {
                info = (IceSSL.WSSConnectionInfo)p.ice_getConnection().getInfo();
                test(info.verified);
            }
            catch(Ice.LocalException ex)
            {
                test(false);
            }
            comm.destroy();
        }
        out.println("ok");

        return factory;
    }
}
