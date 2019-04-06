#include "WrapTCPService.h"

using namespace netxy::net;

TCPSession::TCPSession()
{
    mSocketID = -1;
    mUserData = -1;
    mCloseCallback = nullptr;
    mDataCallback = nullptr;
    mService = nullptr;
}

TCPSession::~TCPSession()
{
    mSocketID = -1;
    mUserData = -1;
}

TCPSession::USER_TYPE TCPSession::getUD() const
{
    return mUserData;
}

void TCPSession::setUD(USER_TYPE ud)
{
    mUserData = ud;
}

const std::string& TCPSession::getIP() const
{
    return mIP;
}

TcpService::SESSION_TYPE TCPSession::getSocketID() const
{
    return mSocketID;
}

void TCPSession::send(const char* buffer, size_t len, const DataSocket::PACKED_SENDED_CALLBACK& callback) const
{
    mService->send(mSocketID, DataSocket::makePacket(buffer, len), callback);
}

void TCPSession::send(const DataSocket::PACKET_PTR& packet, const DataSocket::PACKED_SENDED_CALLBACK& callback) const
{
    mService->send(mSocketID, packet, callback);
}

void TCPSession::postShutdown() const
{
    mService->shutdown(mSocketID);
}

void TCPSession::postClose() const
{
    mService->disConnect(mSocketID);
}

void TCPSession::setCloseCallback(CLOSE_CALLBACK&& callback)
{
    mCloseCallback = std::move(callback);
}

void TCPSession::setCloseCallback(const CLOSE_CALLBACK& callback)
{
    mCloseCallback = callback;
}

void TCPSession::setDataCallback(DATA_CALLBACK&& callback)
{
    mDataCallback = std::move(callback);
}

void TCPSession::setDataCallback(const DATA_CALLBACK& callback)
{
    mDataCallback = callback;
}

void TCPSession::setSocketID(TcpService::SESSION_TYPE id)
{
    mSocketID = id;
}

void TCPSession::setIP(const std::string& ip)
{
    mIP = ip;
}

void TCPSession::setService(TcpService::PTR& service)
{
    mService = service;
}

TCPSession::CLOSE_CALLBACK& TCPSession::getCloseCallback()
{
    return mCloseCallback;
}

TCPSession::DATA_CALLBACK& TCPSession::getDataCallback()
{
    return mDataCallback;
}

WrapServer::WrapServer()
{
    mTCPService = std::make_shared<TcpService>();
}

WrapServer::~WrapServer()
{
}

TcpService::PTR& WrapServer::getService()
{
    return mTCPService;
}

void WrapServer::startWorkThread(size_t threadNum, TcpService::FRAME_CALLBACK callback)
{
    mTCPService->startWorkerThread(threadNum, callback);
}

void WrapServer::addSession(sock fd, const SESSION_ENTER_CALLBACK& userEnterCallback, bool isUseSSL, size_t maxRecvBufferSize, bool forceSameThreadLoop)
{
    auto tmpSession = TCPSession::Create();
    tmpSession->setService(mTCPService);

    auto enterCallback = [=](TcpService::SESSION_TYPE id, std::string ip) mutable {
        tmpSession->setSocketID(id);
        tmpSession->setIP(ip);
        if (userEnterCallback != nullptr)
        {
            userEnterCallback(tmpSession);
        }
    };

    auto closeCallback = [=](TcpService::SESSION_TYPE id) mutable {
        auto& callback = tmpSession->getCloseCallback();
        if (callback != nullptr)
        {
            callback(tmpSession);
        }
    };

    auto msgCallback = [=](TcpService::SESSION_TYPE id, const char* buffer, size_t len) mutable {
        auto& callback = tmpSession->getDataCallback();
        if (callback != nullptr)
        {
            return callback(tmpSession, buffer, len);
        }
        else
        {
            return static_cast<size_t>(0);
        }
    };

    mTCPService->addDataSocket(fd, enterCallback, closeCallback, msgCallback, isUseSSL, maxRecvBufferSize, forceSameThreadLoop);
}