#ifndef DODO_NET_DATASOCKET_H_
#define DODO_NET_DATASOCKET_H_

#include <memory>
#include <functional>
#include <deque>

#include "Channel.h"
#include "Timer.h"
#include "EventLoop.h"
#include "NonCopyable.h"

#ifdef USE_OPENSSL

#ifdef  __cplusplus
extern "C" {
#endif
#include "openssl/ssl.h"
#include "openssl/err.h"
#ifdef  __cplusplus
}
#endif

#endif

struct buffer_s;

/*  ʹ����ָ��,��һ��Ͷ�ݵ�eventloop,ֻ����onEnterEventLoopʧ�ܻ��߶Ͽ��ص��в���delete��(һ��DataSocket�ĶϿ��ص�ֻ�ᱻ����һ��)  */

namespace netxy
{
    namespace net
    {
        class DataSocket final : public Channel, public NonCopyable
        {
        public:
            typedef DataSocket*                                                         PTR;

            typedef std::function<void(PTR)>                                            ENTER_CALLBACK;
            typedef std::function<size_t(PTR, const char* buffer, size_t len)>          DATA_CALLBACK;
            typedef std::function<void(PTR)>                                            DISCONNECT_CALLBACK;
            typedef std::shared_ptr<std::function<void(void)>>                          PACKED_SENDED_CALLBACK;

            typedef std::shared_ptr<std::string>                                        PACKET_PTR;

        public:
            explicit DataSocket(sock fd, size_t maxRecvBufferSize);
            ~DataSocket();

            /*  ���������߳��е��òſ��ܷ��سɹ� */
            bool                            onEnterEventLoop(EventLoop* eventLoop);

            void                            send(const char* buffer, size_t len, const PACKED_SENDED_CALLBACK& callback = nullptr);

            void                            sendPacket(const PACKET_PTR&, const PACKED_SENDED_CALLBACK& callback = nullptr);

            void                            sendPacketInLoop(const PACKET_PTR&, const PACKED_SENDED_CALLBACK& callback = nullptr);

            void                            setEnterCallback(ENTER_CALLBACK&& cb);
            void                            setEnterCallback(const ENTER_CALLBACK& cb);

            void                            setDataCallback(DATA_CALLBACK&& cb);
            void                            setDataCallback(const DATA_CALLBACK& cb);

            void                            setDisConnectCallback(DISCONNECT_CALLBACK&& cb);
            void                            setDisConnectCallback(const DISCONNECT_CALLBACK& cb);

            /*  �����������ʱ��,overtimeΪ-1��ʾ�����   */
            void                            setCheckTime(int overtime);
            /*  ����(Ͷ��)�Ͽ�����,����ɹ������Ͽ�(�����ײ�û���ȴ��������Ͽ�)��ᴥ���Ͽ��ص�  */
            void                            postDisConnect();
            void                            postShutdown();

            void                            setUserData(int64_t value);
            int64_t                         getUserData() const;

#ifdef USE_OPENSSL
            bool                            initAcceptSSL(SSL_CTX*);
            bool                            initConnectSSL();
#endif

            static  PACKET_PTR              makePacket(const char* buffer, size_t len);

        private:
            void                            growRecvBuffer();

            void                            PingCheck();
            void                            startPingCheckTimer();

            void                            canRecv() override;
            void                            canSend() override;

            bool                            checkRead();
            bool                            checkWrite();

            void                            recv();
            void                            flush();
            void                            normalFlush();
            void                            quickFlush();

            void                            onClose() override;
            void                            closeSocket();
            void                            procCloseInLoop();
            void                            procShutdownInLoop();

            void                            runAfterFlush();
#ifdef PLATFORM_LINUX
            void                            removeCheckWrite();
#endif
#ifdef USE_OPENSSL
            void                            processSSLHandshake();
#endif

        private:

#ifdef PLATFORM_WINDOWS
            struct EventLoop::ovl_ext_s     mOvlRecv;
            struct EventLoop::ovl_ext_s     mOvlSend;

            bool                            mPostRecvCheck;     /*  �Ƿ�Ͷ���˿ɶ����   */
            bool                            mPostWriteCheck;    /*  �Ƿ�Ͷ���˿�д���   */
#endif

            sock                            mFD;
            bool                            mIsPostFinalClose;  /*  �Ƿ�Ͷ�������յ�close����    */

            bool                            mCanWrite;          /*  socket�Ƿ��д  */

            EventLoop*                      mEventLoop;
            buffer_s*                       mRecvBuffer;
            size_t                          mMaxRecvBufferSize;

            struct pending_packet
            {
                PACKET_PTR  data;
                size_t      left;
                PACKED_SENDED_CALLBACK  mCompleteCallback;
            };

            typedef std::deque<pending_packet>   PACKET_LIST_TYPE;
            PACKET_LIST_TYPE                mSendList;          /*  ������Ϣ�б�  */

            ENTER_CALLBACK                  mEnterCallback;
            DATA_CALLBACK                   mDataCallback;
            DISCONNECT_CALLBACK             mDisConnectCallback;

            bool                            mIsPostFlush;       /*  �Ƿ��Ѿ�����flush��Ϣ�Ļص�    */

            int64_t                         mUserData;          /*  ���ӵ��û��Զ�������  */

#ifdef USE_OPENSSL
            SSL_CTX*                        mSSLCtx;            /*  mSSL��Ϊnullʱ�����mSSLCtx��Ϊnull�����ʾssl�Ŀͻ������ӣ�����Ϊaccept����  */
            SSL*                            mSSL;               /*  mSSL��Ϊnull�����ʾΪssl��ȫ����   */
            bool                            mIsHandsharked;
#endif

            bool                            mRecvData;
            int                             mCheckTime;
            Timer::WeakPtr                  mTimer;
        };
    }
}

#endif