//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

SETUP_LOG (RPCDoor)

RPCDoor::RPCDoor (boost::asio::io_service& io_service, RPCServer::Handler& handler)
    : m_rpcServerHandler (handler)
    , mAcceptor (io_service,
                 boost::asio::ip::tcp::endpoint (boost::asio::ip::address::from_string (getConfig ().getRpcIP ()), getConfig ().getRpcPort ()))
    , mDelayTimer (io_service)
    , mSSLContext (boost::asio::ssl::context::sslv23)
{
    WriteLog (lsINFO, RPCDoor) << "RPC port: " << getConfig ().getRpcAddress().toRawUTF8() << " allow remote: " << getConfig ().RPC_ALLOW_REMOTE;

    if (getConfig ().RPC_SECURE != 0)
    {
        // VFALCO TODO This could be a method of theConfig
        //
        basio::SslContext::initializeFromFile (
            mSSLContext,
            getConfig ().RPC_SSL_KEY,
            getConfig ().RPC_SSL_CERT,
            getConfig ().RPC_SSL_CHAIN);
    }

    startListening ();
}

RPCDoor::~RPCDoor ()
{
    WriteLog (lsINFO, RPCDoor) << "RPC port: " << getConfig ().getRpcAddress().toRawUTF8() << " allow remote: " << getConfig ().RPC_ALLOW_REMOTE;
}

void RPCDoor::startListening ()
{
    RPCServer::pointer new_connection = RPCServer::New (mAcceptor.get_io_service (), mSSLContext, m_rpcServerHandler);
    mAcceptor.set_option (boost::asio::ip::tcp::acceptor::reuse_address (true));

    mAcceptor.async_accept (new_connection->getRawSocket (),
                            boost::bind (&RPCDoor::handleConnect, this, new_connection,
                                         boost::asio::placeholders::error));
}

bool RPCDoor::isClientAllowed (const std::string& ip)
{
    if (getConfig ().RPC_ALLOW_REMOTE)
        return true;

    // VFALCO TODO Represent ip addresses as a structure. Use isLoopback() member here
    //
    if (ip == "127.0.0.1")
        return true;

    return false;
}

void RPCDoor::handleConnect (RPCServer::pointer new_connection, const boost::system::error_code& error)
{
    bool delay = false;

    if (!error)
    {
        // Restrict callers by IP
        try
        {
            if (! isClientAllowed (new_connection->getRemoteAddressText ()))
            {
                startListening ();
                return;
            }
        }
        catch (...)
        {
            // client may have disconnected
            startListening ();
            return;
        }

        new_connection->getSocket ().async_handshake (AutoSocket::ssl_socket::server,
                boost::bind (&RPCServer::connected, new_connection));
    }
    else
    {
        if (error == boost::system::errc::too_many_files_open)
            delay = true;

        WriteLog (lsINFO, RPCDoor) << "RPCDoor::handleConnect Error: " << error;
    }

    if (delay)
    {
        mDelayTimer.expires_from_now (boost::posix_time::milliseconds (1000));
        mDelayTimer.async_wait (boost::bind (&RPCDoor::startListening, this));
    }
    else
        startListening ();
}
// vim:ts=4