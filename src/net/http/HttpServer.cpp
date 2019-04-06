#include <string>
#include <cstring>

#include "SHA1.h"
#include "base64.h"
#include "http_parser.h"
#include "WebSocketFormat.h"

#include "HttpServer.h"

using namespace netxy::net;

HttpSession::HttpSession(TCPSession::PTR session)
{
    mUserData = -1;
    mSession = session;
}

TCPSession::PTR& HttpSession::getSession()
{
    return mSession;
}

int64_t HttpSession::getUD() const
{
    return mUserData;
}

void HttpSession::setUD(int64_t userData)
{
    mUserData = userData;
}

void HttpSession::setHttpCallback(HTTPPARSER_CALLBACK&& callback)
{
    mHttpRequestCallback = std::move(callback);
}

void HttpSession::setCloseCallback(CLOSE_CALLBACK&& callback)
{
    mCloseCallback = std::move(callback);
}

void HttpSession::setWSCallback(WS_CALLBACK&& callback)
{
    mWSCallback = std::move(callback);
}

void HttpSession::setHttpCallback(const HTTPPARSER_CALLBACK& callback)
{
    mHttpRequestCallback = callback;
}

void HttpSession::setCloseCallback(const CLOSE_CALLBACK& callback)
{
    mCloseCallback = callback;
}

void HttpSession::setWSCallback(const WS_CALLBACK& callback)
{
    mWSCallback = callback;
}

void HttpSession::setWSConnected(const WS_CONNECTED_CALLBACK& callback)
{
    mWSConnectedCallback = callback;
}

HttpSession::HTTPPARSER_CALLBACK& HttpSession::getHttpCallback()
{
    return mHttpRequestCallback;
}

HttpSession::CLOSE_CALLBACK& HttpSession::getCloseCallback()
{
    return mCloseCallback;
}

HttpSession::WS_CALLBACK& HttpSession::getWSCallback()
{
    return mWSCallback;
}

HttpSession::WS_CONNECTED_CALLBACK& HttpSession::getWSConnectedCallback()
{
    return mWSConnectedCallback;
}

void HttpSession::send(const DataSocket::PACKET_PTR& packet, const DataSocket::PACKED_SENDED_CALLBACK& callback /* = nullptr */)
{
    mSession->send(packet, callback);
}

void HttpSession::send(const char* packet, size_t len, const DataSocket::PACKED_SENDED_CALLBACK& callback)
{
    mSession->send(packet, len, callback);
}

void HttpSession::postShutdown() const
{
    mSession->postShutdown();
}

void HttpSession::postClose() const
{
    mSession->postClose();
}

HttpServer::HttpServer()
{
    mServer = std::make_shared<WrapServer>();
}

HttpServer::~HttpServer()
{
    if (mListenThread != nullptr)
    {
        mListenThread->closeListenThread();
    }
    if (mServer != nullptr)
    {
        mServer->getService()->closeService();
    }
}

WrapServer::PTR HttpServer::getServer()
{
    return mServer;
}

void HttpServer::setEnterCallback(const ENTER_CALLBACK& callback)
{
    mOnEnter = callback;
}

void HttpServer::addConnection(sock fd, 
    const ENTER_CALLBACK& enterCallback, 
    const HttpSession::HTTPPARSER_CALLBACK& responseCallback, 
    const HttpSession::WS_CALLBACK& wsCallback, 
    const HttpSession::CLOSE_CALLBACK& closeCallback,
    const HttpSession::WS_CONNECTED_CALLBACK& wsConnectedCallback)
{
    mServer->addSession(fd, [this, enterCallback, responseCallback, wsCallback, closeCallback, wsConnectedCallback](TCPSession::PTR& session){
        auto httpSession = HttpSession::Create(session);
        httpSession->setCloseCallback(closeCallback);
        httpSession->setWSCallback(wsCallback);
        httpSession->setHttpCallback(responseCallback);
        httpSession->setWSConnected(wsConnectedCallback);

        enterCallback(httpSession);
        handleHttp(httpSession);
    }, false, 32*1024 * 1024);
}

void HttpServer::startWorkThread(size_t workthreadnum, TcpService::FRAME_CALLBACK callback)
{
    mServer->startWorkThread(workthreadnum, callback);
}

void HttpServer::startListen(bool isIPV6, const std::string& ip, int port, const char *certificate /* = nullptr */, const char *privatekey /* = nullptr */)
{
    if (mListenThread == nullptr)
    {
        mListenThread = std::make_shared<ListenThread>();
        mListenThread->startListen(isIPV6, ip, port, certificate, privatekey, [this](sock fd){
            mServer->addSession(fd, [this](TCPSession::PTR& session){
                auto httpSession = HttpSession::Create(session);
                if (mOnEnter != nullptr)
                {
                    mOnEnter(httpSession);
                }
                handleHttp(httpSession);
            }, false, 32 * 1024 * 1024);
        });
    }
}

void HttpServer::handleHttp(HttpSession::PTR& httpSession)
{
    /*TODO::keep alive and timeout close */
    auto& session = httpSession->getSession();
    auto httpParser = new HTTPParser(HTTP_BOTH);
    session->setUD((int64_t)httpParser);

    session->setCloseCallback([httpSession](TCPSession::PTR& session){
        auto httpParser = (HTTPParser*)session->getUD();
        delete httpParser;

        auto& tmp = httpSession->getCloseCallback();
        if (tmp != nullptr)
        {
            tmp(httpSession);
        }
    });

    session->setDataCallback([this, httpSession](TCPSession::PTR& session, const char* buffer, size_t len){
        size_t retlen = 0;
        auto httpParser = (HTTPParser*)session->getUD();
        if (httpParser->isWebSocket())
        {
            const char* parse_str = buffer;
            size_t leftLen = len;

            auto& cacheFrame = httpParser->getWSCacheFrame();
            auto& parseString = httpParser->getWSParseString();

            while (leftLen > 0)
            {
                parseString.clear();

                auto opcode = WebSocketFormat::WebSocketFrameType::ERROR_FRAME; /*TODO::opcode�Ƿ�ش����ص�����*/
                size_t frameSize = 0;
                bool isFin = false;
                if (WebSocketFormat::wsFrameExtractBuffer(parse_str, leftLen, parseString, opcode, frameSize, isFin))
                {
                    if (isFin && (opcode == WebSocketFormat::WebSocketFrameType::TEXT_FRAME || opcode == WebSocketFormat::WebSocketFrameType::BINARY_FRAME))
                    {
                        if (!cacheFrame.empty())
                        {
                            cacheFrame += parseString;
                            parseString = std::move(cacheFrame);
                            cacheFrame.clear();
                        }

                        auto& wsCallback = httpSession->getWSCallback();
                        if (wsCallback != nullptr)
                        {
                            wsCallback(httpSession, opcode, parseString);
                        }
                    }
                    else if (opcode == WebSocketFormat::WebSocketFrameType::CONTINUATION_FRAME)
                    {
                        cacheFrame += parseString;
                        parseString.clear();
                    }
                    else if (opcode == WebSocketFormat::WebSocketFrameType::PING_FRAME ||
                            opcode == WebSocketFormat::WebSocketFrameType::PONG_FRAME ||
                            opcode == WebSocketFormat::WebSocketFrameType::CLOSE_FRAME)
                    {
                        auto& wsCallback = httpSession->getWSCallback();
                        if (wsCallback != nullptr)
                        {
                            wsCallback(httpSession, opcode, parseString);
                        }
                    }
                    else
                    {
                        assert(false);
                    }

                    leftLen -= frameSize;
                    retlen += frameSize;
                    parse_str += frameSize;
                }
                else
                {
                    break;
                }
            }
        }
        else
        {
            retlen = httpParser->tryParse(buffer, len);
            if (httpParser->isCompleted())
            {
                if (httpParser->isWebSocket())
                {
                    if (httpParser->hasKey("Sec-WebSocket-Key"))
                    {
                        auto response = WebSocketFormat::wsHandshake(httpParser->getValue("Sec-WebSocket-Key"));
                        session->send(response.c_str(), response.size());
                    }
                    
                    auto& wsConnectedCallback = httpSession->getWSConnectedCallback();
                    if (wsConnectedCallback != nullptr)
                    {
                        wsConnectedCallback(httpSession, *httpParser);
                    }
                }
                else
                {
                    auto& httpCallback = httpSession->getHttpCallback();
                    if (httpCallback != nullptr)
                    {
                        httpCallback(*httpParser, httpSession);
                    }
                    if (httpParser->isKeepAlive())
                    {
                        /*�������http�������ݣ�Ϊ��һ��http����׼��*/
                        httpParser->clearParse();
                    }
                }
            }
        }

        return retlen;
    });
}
