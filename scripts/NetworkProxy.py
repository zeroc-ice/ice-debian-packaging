# **********************************************************************
#
# Copyright (c) 2003-2017 ZeroC, Inc. All rights reserved.
#
# This copy of Ice is licensed to you under the terms described in the
# ICE_LICENSE file included in this distribution.
#
# **********************************************************************

import sys, os, threading, socket, select, atexit

class InvalidRequest(Exception): pass

class BaseConnection(threading.Thread):
    def __init__(self, socket, remote):
        threading.Thread.__init__(self)
        self.setDaemon(True)
        self.socket = socket
        self.remote = remote
        self.remoteSocket = None
        self.closed = False

    def response(self, code):
        pass

    def request(self, data):
        pass

    def close(self):
        self.closed = True
        try:
            if self.socket:
                self.socket.close()
                self.socket = None
            if self.remoteSocket:
                self.remoteSocket.close()
                self.remoteSocket = None
        except:
            pass

    def run(self):
        try:
            remoteAddr = self.request(self.socket)
            self.remoteSocket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            try:
                self.remoteSocket.connect(remoteAddr)
                self.socket.send(self.response(True))
            except:
                self.socket.send(self.response(False))
                return

            try:
                while(not self.closed):
                    readables, writeables, exceptions = select.select([self.socket, self.remoteSocket], [], [])
                    for r in readables:
                        w =  self.remoteSocket if r == self.socket else self.socket
                        data = r.recv(4096)
                        if(len(data) == 0):
                            self.closed = True
                            break
                        w.send(data)
            except InvalidRequest as ex:
                print("invalid request")
            except Exception as ex:
                #print(ex)
                pass

        except Exception as ex:
            #print(ex)
            pass

        finally:
            self.close()

class BaseProxy(threading.Thread):
    def __init__(self, port):
        threading.Thread.__init__(self)
        self.port = port
        self.closed = False
        self.socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.connections = []
        atexit.register(self.terminate)
        self.setDaemon(True)
        self.start()

    def createConnection(self):
        return None

    def run(self):
        self.socket.bind(("127.0.0.1", self.port))
        self.socket.listen(1)
        try:
            while not self.closed:
                incoming, peer = self.socket.accept()
                connection = self.createConnection(incoming, peer)
                connection.start()
                self.connections.append(connection)
        except:
            pass
        finally:
            self.socket.close()

    def terminate(self):
        if self.closed:
            return
        self.closed = True
        for c in self.connections:
            try:
                c.close()
            except Exception as ex:
                print(ex)
        connectToSelf = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        try:
            connectToSelf.connect(("127.0.0.1", self.port))
        except Exception as ex:
            print(ex)
        finally:
            connectToSelf.close()

class SocksConnection(BaseConnection):

    def request(self, s):
        def decode(c):
            return ord(c) if sys.version_info[0] == 2 else c

        data = s.recv(9) # Read the 9 bytes request

        if not data or len(data) == 0:
            raise InvalidRequest
        if decode(data[0]) != 4:
            raise InvalidRequest
        if decode(data[1]) != 1:
            raise InvalidRequest

        port = (decode(data[2]) << 8) + decode(data[3])
        addr = socket.inet_ntoa(data[4:8])
        return (addr, port)

    def response(self, success):
        def encode(c):
            return chr(c)

        packet = encode(0)
        packet += encode(90 if success else 91)
        packet += encode(0)
        packet += encode(0)
        packet += encode(0)
        packet += encode(0)
        packet += encode(0)
        packet += encode(0)
        return packet if sys.version_info[0] == 2 else bytes(packet,"ascii")

class SocksProxy(BaseProxy):
    
    def createConnection(self, socket, peer):
        return SocksConnection(socket, peer)

class HttpConnection(BaseConnection):

    def request(self, s):
        def decode(c):
            return c[0] if sys.version_info[0] == 2 else chr(c[0])

        data = ""
        while(len(data) < 4 or data[len(data) - 4:] != "\r\n\r\n"):
            data += decode(s.recv(1))

        if data.find("CONNECT ") != 0:
            raise InvalidRequest

        sep = data.find(":")
        if sep < len("CONNECT ") + 1:
            raise InvalidRequest
            
        host = data[len("CONNECT "):sep]
        space = data.find(" ", sep)
        if space < sep + 1:
            raise InvalidRequest
            
        port = int(data[sep + 1:space])
        return (host, port)

    def response(self, success):
        if(success):
            s = "HTTP/1.1 200 OK\r\nServer: CERN/3.0 libwww/2.17\r\n\r\n";
        else:
            s = "HTTP/1.1 404\r\n\r\n";
        return s if sys.version_info[0] == 2 else bytes(s,"ascii")

class HttpProxy(BaseProxy):
    
    def createConnection(self, socket, peer):
        return HttpConnection(socket, peer)
