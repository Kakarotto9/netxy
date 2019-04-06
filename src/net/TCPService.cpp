#include "SocketLibFunction.h"
#include "EventLoop.h"
#include "DataSocket.h"

#include "TCPService.h"

static unsigned int sDefaultLoopTimeOutMS = 100;

using namespace netxy::net;

ListenThread::ListenThread()
{
    mIsIPV6 = false;
    mAcceptCallback = nullptr;
    mPort = 0;
    mRunListen = false;
    mListenThread = nullptr;
#ifdef USE_OPENSSL
    mOpenSSLCTX = nullptr;
#endif
}

ListenThread::~ListenThread()
{
    closeListenThread();
}

void ListenThread::startListen(bool isIPV6, const std::string& ip, int port, const char *certificate, const char *privatekey, ACCEPT_CALLBACK callback)
{
    if (mListenThread == nullptr)
    {
        mIsIPV6 = isIPV6;
        mRunListen = true;
        mIP = ip;
        mPort = port;
        mAcceptCallback = callback;
        if (certificate != nullptr)
        {
            mCertificate = certificate;
        }
        if (privatekey != nullptr)
        {
            mPrivatekey = privatekey;
        }

        mListenThread = new std::thread([=](){
            runListen();
        });
    }
}

void ListenThread::closeListenThread()
{
    if (mListenThread != nullptr)
    {
        mRunListen = false;

        sock tmp = ox_socket_connect(mIsIPV6, mIP.c_str(), mPort);
        ox_socket_close(tmp);
        tmp = SOCKET_ERROR;

        if (mListenThread->joinable())
        {
            mListenThread->join();
        }

        delete mListenThread;
        mListenThread = NULL;
    }
}

#ifdef USE_OPENSSL
SSL_CTX* ListenThread::getOpenSSLCTX()
{
    return mOpenSSLCTX;
}
#endif

void ListenThread::initSSL()
{
#ifdef USE_OPENSSL
    mOpenSSLCTX = nullptr;

    if (!mCertificate.empty() && !mPrivatekey.empty())
    {
        mOpenSSLCTX = SSL_CTX_new(SSLv23_server_method());
        if (SSL_CTX_use_certificate_file(mOpenSSLCTX, mCertificate.c_str(), SSL_FILETYPE_PEM) <= 0) {
            SSL_CTX_free(mOpenSSLCTX);
            mOpenSSLCTX = nullptr;
        }
        /* �����û�˽Կ */
        if (SSL_CTX_use_PrivateKey_file(mOpenSSLCTX, mPrivatekey.c_str(), SSL_FILETYPE_PEM) <= 0) {
            SSL_CTX_free(mOpenSSLCTX);
            mOpenSSLCTX = nullptr;
        }
        /* ����û�˽Կ�Ƿ���ȷ */
        if (!SSL_CTX_check_private_key(mOpenSSLCTX)) {
            SSL_CTX_free(mOpenSSLCTX);
            mOpenSSLCTX = nullptr;
        }
    }
#endif
}

void ListenThread::destroySSL()
{
#ifdef USE_OPENSSL
    if(mOpenSSLCTX != nullptr)
    {
        SSL_CTX_free(mOpenSSLCTX);
        mOpenSSLCTX = nullptr;
    }
#endif
}

void ListenThread::runListen()
{
    sock client_fd = SOCKET_ERROR;
    struct sockaddr_in socketaddress;
    struct sockaddr_in6 ip6Addr;
    socklen_t addrLen = sizeof(struct sockaddr);
    sockaddr_in* pAddr = &socketaddress;

    if (mIsIPV6)
    {
        addrLen = sizeof(ip6Addr);
        pAddr = (sockaddr_in*)&ip6Addr;
    }

    sock listen_fd = ox_socket_listen(mIsIPV6, mIP.c_str(), mPort, 512);
    initSSL();

    if (SOCKET_ERROR != listen_fd)
    {
        printf("listen : %d \n", mPort);
        for (; mRunListen;)
        {
            while ((client_fd = ox_socket_accept(listen_fd, (struct sockaddr*)pAddr, &addrLen)) == SOCKET_ERROR)
            {
                if (EINTR == sErrno)
                {
                    continue;
                }
            }

            if (SOCKET_ERROR != client_fd && mRunListen)
            {
                ox_socket_nodelay(client_fd);
                ox_socket_setsdsize(client_fd, 32 * 1024);
                mAcceptCallback(client_fd);
            }
        }

        ox_socket_close(listen_fd);
        listen_fd = SOCKET_ERROR;
    }
    else
    {
        printf("listen failed, error:%d \n", sErrno);
    }
}

TcpService::TcpService()
{
    static_assert(sizeof(SessionId) == sizeof(((SessionId*)nullptr)->id), "sizeof SessionId must equal int64_t");
    mLoops = nullptr;
    mLoopNum = 0;
    mIOThreads = nullptr;
    mDataSockets = nullptr;
    mIncIds = nullptr;

    mEnterCallback = nullptr;
    mDisConnectCallback = nullptr;
    mDataCallback = nullptr;

    mRunIOLoop = false;
    mCachePacketList = nullptr;
}

TcpService::~TcpService()
{
    closeService();
}

void TcpService::setEnterCallback(TcpService::ENTER_CALLBACK&& callback)
{
    mEnterCallback = std::move(callback);
}

void TcpService::setEnterCallback(const TcpService::ENTER_CALLBACK& callback)
{
    mEnterCallback = callback;
}

void TcpService::setDisconnectCallback(TcpService::DISCONNECT_CALLBACK&& callback)
{
    mDisConnectCallback = std::move(callback);
}

void TcpService::setDisconnectCallback(const TcpService::DISCONNECT_CALLBACK& callback)
{
    mDisConnectCallback = callback;
}

void TcpService::setDataCallback(TcpService::DATA_CALLBACK&& callback)
{
    mDataCallback = std::move(callback);
}

void TcpService::setDataCallback(const TcpService::DATA_CALLBACK& callback)
{
    mDataCallback = callback;
}

const TcpService::ENTER_CALLBACK& TcpService::getEnterCallback() const
{
    return mEnterCallback;
}

const TcpService::DISCONNECT_CALLBACK& TcpService::getDisconnectCallback() const
{
    return mDisConnectCallback;
}

const TcpService::DATA_CALLBACK& TcpService::getDataCallback() const
{
    return mDataCallback;
}

void TcpService::send(SESSION_TYPE id, const DataSocket::PACKET_PTR& packet, const DataSocket::PACKED_SENDED_CALLBACK& callback) const
{
    union  SessionId sid;
    sid.id = id;
    assert(sid.data.loopIndex < mLoopNum);
    if (sid.data.loopIndex < mLoopNum)
    {
        /*  �����ǰ���������߳���ֱ��send,����ʹ��pushAsyncProc����lambda(���ٶ���Ӱ��),�����ʹ��postSessionAsyncProc */
        if (mLoops[sid.data.loopIndex]->isInLoopThread())
        {
            DataSocket::PTR tmp = nullptr;
            if (mDataSockets[sid.data.loopIndex].get(sid.data.index, tmp) &&
                tmp != nullptr &&
                tmp->getUserData() == sid.id)
            {
                tmp->sendPacketInLoop(packet, callback);
            }
        }
        else
        {
            mLoops[sid.data.loopIndex]->pushAsyncProc([=](){
                DataSocket::PTR tmp = nullptr;
                if (mDataSockets[sid.data.loopIndex].get(sid.data.index, tmp) &&
                    tmp != nullptr &&
                    tmp->getUserData() == sid.id)
                {
                    tmp->sendPacketInLoop(packet, callback);
                }
            });
        }
    }
}

void TcpService::cacheSend(SESSION_TYPE id, const DataSocket::PACKET_PTR& packet, const DataSocket::PACKED_SENDED_CALLBACK& callback)
{
    union  SessionId sid;
    sid.id = id;
    assert(sid.data.loopIndex < mLoopNum);
    if (sid.data.loopIndex < mLoopNum)
    {
        mCachePacketList[sid.data.loopIndex]->push_back(std::make_tuple(id, packet, callback));
    }
}

void TcpService::flushCachePackectList()
{
    for (size_t i = 0; i < mLoopNum; ++i)
    {
        if (!mCachePacketList[i]->empty())
        {
            auto& msgList = mCachePacketList[i];
            mLoops[i]->pushAsyncProc([=](){
                for (auto& v : *msgList)
                {
                    union  SessionId sid;
                    sid.id = std::get<0>(v);
                    DataSocket::PTR tmp = nullptr;
                    if (mDataSockets[sid.data.loopIndex].get(sid.data.index, tmp))
                    {
                        if (tmp != nullptr && tmp->getUserData() == sid.id)
                        {
                            tmp->sendPacket(std::get<1>(v), std::get<2>(v));
                        }
                    }
                }
                
            });
            mCachePacketList[i] = std::make_shared<MSG_LIST>();
        }
    }
}

void TcpService::shutdown(SESSION_TYPE id) const
{
    postSessionAsyncProc(id, [](DataSocket::PTR ds){
        ds->postShutdown();
    });
}

void TcpService::disConnect(SESSION_TYPE id) const
{
    postSessionAsyncProc(id, [](DataSocket::PTR ds){
        ds->postDisConnect();
    });
}

void TcpService::setPingCheckTime(SESSION_TYPE id, int checktime)
{
    postSessionAsyncProc(id, [=](DataSocket::PTR ds){
        ds->setCheckTime(checktime);
    });
}

void TcpService::postSessionAsyncProc(SESSION_TYPE id, const std::function<void(DataSocket::PTR)>& callback) const
{
    union  SessionId sid;
    sid.id = id;
    assert(sid.data.loopIndex < mLoopNum);
    if (sid.data.loopIndex < mLoopNum)
    {
        mLoops[sid.data.loopIndex]->pushAsyncProc([=](){
            DataSocket::PTR tmp = nullptr;
            if (callback != nullptr &&
                mDataSockets[sid.data.loopIndex].get(sid.data.index, tmp) &&
                tmp != nullptr &&
                tmp->getUserData() == sid.id)
            {
                callback(tmp);
            }
        });
    }
}

void TcpService::closeService()
{
    closeListenThread();
    closeWorkerThread();
}

void TcpService::closeListenThread()
{
    mListenThread.closeListenThread();
}

void TcpService::closeWorkerThread()
{
    stopWorkerThread();

    delete[] mLoops;
    mLoops = nullptr;
    delete[] mIncIds;
    mIncIds = nullptr;
    delete[] mDataSockets;
    mDataSockets = nullptr;
    delete[] mCachePacketList;
    mCachePacketList = nullptr;
}

void TcpService::stopWorkerThread()
{
    if (mLoops != nullptr)
    {
        mRunIOLoop = false;

        for (size_t i = 0; i < mLoopNum; ++i)
        {
            mLoops[i]->wakeup();
        }
    }

    if (mIOThreads != nullptr)
    {
        for (size_t i = 0; i < mLoopNum; ++i)
        {
            if (mIOThreads[i]->joinable())
            {
                mIOThreads[i]->join();
            }
            delete mIOThreads[i];
        }

        delete[] mIOThreads;
        mIOThreads = nullptr;
    }
}

void TcpService::startListen(bool isIPV6, const std::string& ip, int port, int maxSessionRecvBufferSize, const char *certificate, const char *privatekey)
{
    mListenThread.startListen(isIPV6, ip, port, certificate, privatekey, [=](sock fd){
        std::string ip = ox_socket_getipoffd(fd);
        auto channel = new DataSocket(fd, maxSessionRecvBufferSize);
        bool ret = true;
#ifdef USE_OPENSSL
        if (mListenThread.getOpenSSLCTX() != nullptr)
        {
            ret = channel->initAcceptSSL(mListenThread.getOpenSSLCTX());
        }
#endif
        if (ret)
        {
            ret = helpAddChannel(channel, ip, mEnterCallback, mDisConnectCallback, mDataCallback);
        }
        
        if (!ret)
        {
            delete channel;
            channel = nullptr;
        }
    });
}

void TcpService::startWorkerThread(size_t threadNum, FRAME_CALLBACK callback)
{
    if (mLoops == nullptr)
    {
        mRunIOLoop = true;
        mCachePacketList = new std::shared_ptr<MSG_LIST>[threadNum];
        for (size_t i = 0; i < threadNum; ++i)
        {
            mCachePacketList[i] = std::make_shared<MSG_LIST>();
        }

        mLoops = new EventLoop::PTR[threadNum];
        for (size_t i = 0; i < threadNum; ++i)
        {
            mLoops[i] = std::make_shared<EventLoop>();
        }

        mIOThreads = new std::thread*[threadNum];
        mLoopNum = threadNum;
        mDataSockets = new TypeIDS<DataSocket::PTR>[threadNum];
        mIncIds = new int[threadNum];
        for (size_t i = 0; i < threadNum; ++i)
        {
            mIncIds[i] = 0;
        }

        for (size_t i = 0; i < mLoopNum; ++i)
        {
            EventLoop::PTR eventLoop = mLoops[i];
            mIOThreads[i] = new std::thread([=](){
                while (mRunIOLoop)
                {
                    eventLoop->loop(eventLoop->getTimerMgr()->isEmpty() ? sDefaultLoopTimeOutMS : eventLoop->getTimerMgr()->nearEndMs());
                    if (callback != nullptr)
                    {
                        callback(eventLoop);
                    }
                }
            });
        }
    }
}

void TcpService::wakeup(SESSION_TYPE id) const
{
    union  SessionId sid;
    sid.id = id;
    assert(sid.data.loopIndex < mLoopNum);
    if (sid.data.loopIndex < mLoopNum)
    {
        mLoops[sid.data.loopIndex]->wakeup();
    }
}

void TcpService::wakeupAll() const
{
    for (size_t i = 0; i < mLoopNum; ++i)
    {
        mLoops[i]->wakeup();
    }
}

EventLoop::PTR TcpService::getRandomEventLoop()
{
    EventLoop::PTR ret;
    if (mLoopNum > 0)
    {
        ret = mLoops[rand() % mLoopNum];
    }

    return ret;
}

EventLoop::PTR TcpService::getEventLoopBySocketID(SESSION_TYPE id)
{
    union  SessionId sid;
    sid.id = id;
    assert(sid.data.loopIndex < mLoopNum);
    if (sid.data.loopIndex < mLoopNum)
    {
        return mLoops[sid.data.loopIndex];
    }
    else
    {
        return nullptr;
    }
}

TcpService::SESSION_TYPE TcpService::MakeID(size_t loopIndex)
{
    union SessionId sid;
    sid.data.loopIndex = loopIndex;
    sid.data.index = mDataSockets[loopIndex].claimID();
    sid.data.iid = mIncIds[loopIndex]++;

    return sid.id;
}

void TcpService::procDataSocketClose(DataSocket::PTR ds)
{
    union SessionId sid;
    sid.id = ds->getUserData();

    mDataSockets[sid.data.loopIndex].set(nullptr, sid.data.index);
    mDataSockets[sid.data.loopIndex].reclaimID(sid.data.index);
}

bool TcpService::helpAddChannel(DataSocket::PTR channel, const std::string& ip, 
    const TcpService::ENTER_CALLBACK& enterCallback, const TcpService::DISCONNECT_CALLBACK& disConnectCallback, const TcpService::DATA_CALLBACK& dataCallback,
    bool forceSameThreadLoop)
{
    if (mLoopNum == 0)
    {
        return false;
    }

    size_t loopIndex = 0;
    if (forceSameThreadLoop)
    {
        bool find = false;
        for (size_t i = 0; i < mLoopNum; i++)
        {
            if (mLoops[i]->isInLoopThread())
            {
                loopIndex = i;
                find = true;
                break;
            }
        }
        if (!find)
        {
            return false;
        }
    }
    else
    {
        /*  ���Ϊ�����ӷ���һ��eventloop */
        loopIndex = rand() % mLoopNum;
    }

    auto loop = mLoops[loopIndex];

    channel->setEnterCallback([=](DataSocket::PTR dataSocket){
        auto id = MakeID(loopIndex);
        union SessionId sid;
        sid.id = id;
        mDataSockets[loopIndex].set(dataSocket, sid.data.index);
        dataSocket->setUserData(id);
        dataSocket->setDataCallback([=](DataSocket::PTR ds, const char* buffer, size_t len){
            return dataCallback(id, buffer, len);
        });

        dataSocket->setDisConnectCallback([=](DataSocket::PTR arg){
            procDataSocketClose(arg);
            disConnectCallback(id);
            delete arg;
        });

        if (enterCallback != nullptr)
        {
            enterCallback(id, ip);
        }
    });

    loop->pushAsyncProc([=](){
        if (!channel->onEnterEventLoop(loop.get()))
        {
            delete channel;
        }
    });

    return true;
}

bool TcpService::addDataSocket( sock fd,
                                const TcpService::ENTER_CALLBACK& enterCallback,
                                const TcpService::DISCONNECT_CALLBACK& disConnectCallback,
                                const TcpService::DATA_CALLBACK& dataCallback,
                                bool isUseSSL,
                                size_t maxRecvBufferSize,
                                bool forceSameThreadLoop)
{
    std::string ip = ox_socket_getipoffd(fd);
    DataSocket::PTR channel = nullptr;
#ifdef USE_OPENSSL
    bool ret = true;
    channel = new DataSocket(fd, maxRecvBufferSize);
    if (isUseSSL)
    {
        ret = channel->initConnectSSL();
    }
#else
    bool ret = false;
    if (!isUseSSL)
    {
        channel = new DataSocket(fd, maxRecvBufferSize);
        ret = true;
    }
#endif
    if (ret)
    {
        ret = helpAddChannel(channel, ip, enterCallback, disConnectCallback, dataCallback, forceSameThreadLoop);
    }

    if (!ret)
    {
        if (channel != nullptr)
        {
            delete channel;
            channel = nullptr;
        }
        else
        {
            ox_socket_close(fd);
        }
    }

    return ret;
}