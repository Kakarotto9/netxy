#include <cassert>

#include "Channel.h"
#include "EventLoop.h"

using namespace netxy::net;

namespace netxy
{
    namespace net
    {
#ifdef PLATFORM_WINDOWS
        class WakeupChannel final : public Channel, public NonCopyable
        {
        public:
            WakeupChannel(HANDLE iocp) : mIOCP(iocp), mWakeupOvl(EventLoop::OLV_VALUE::OVL_RECV)
            {
            }

            void    wakeup()
            {
                PostQueuedCompletionStatus(mIOCP, 0, (ULONG_PTR)this, (OVERLAPPED*)&mWakeupOvl);
            }

        private:
            void    canRecv() override
            {
            }

            void    canSend() override
            {
            }

            void    onClose() override
            {
            }

        private:
            HANDLE                  mIOCP;
            EventLoop::ovl_ext_s    mWakeupOvl;
        };
#else
        class WakeupChannel final : public Channel, public NonCopyable
        {
        public:
            explicit WakeupChannel(sock fd) : mFd(fd)
            {
            }

            void    wakeup()
            {
                uint64_t one = 1;
                write(mFd, &one, sizeof one);
            }

            ~WakeupChannel()
            {
                close(mFd);
                mFd = SOCKET_ERROR;
            }

        private:
            void    canRecv() override
            {
                char temp[1024 * 10];
                while (true)
                {
                    auto n = read(mFd, temp, sizeof(temp));
                    if (n == -1)
                    {
                        break;
                    }
                }
            }

            void    canSend() override
            {
            }

            void    onClose() override
            {
            }

        private:
            sock    mFd;
        };
#endif
    }
}

EventLoop::EventLoop()
#ifdef PLATFORM_WINDOWS
    : mIOCP(CreateIoCompletionPort(INVALID_HANDLE_VALUE, 0, 0, 1)), mWakeupChannel(new WakeupChannel(mIOCP))
#else
    : mEpollFd(epoll_create(1))
#endif
{
#ifdef PLATFORM_WINDOWS
    mPGetQueuedCompletionStatusEx = NULL;
    auto kernel32_module = GetModuleHandleA("kernel32.dll");
    if (kernel32_module != NULL) {
        mPGetQueuedCompletionStatusEx = (sGetQueuedCompletionStatusEx)GetProcAddress(
            kernel32_module,
            "GetQueuedCompletionStatusEx");
        FreeLibrary(kernel32_module);
    }
#else
    auto eventfd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    mWakeupChannel.reset(new WakeupChannel(eventfd));
    linkChannel(eventfd, mWakeupChannel.get());
#endif

    mIsAlreadyPostWakeup = false;
    mIsInBlock = true;

    mEventEntries = NULL;
    mEventEntriesNum = 0;

    reallocEventSize(1024);
    mSelfThreadid = -1;
    mIsInitThreadID = false;
    mTimer = std::make_shared<TimerMgr>();
}

EventLoop::~EventLoop()
{
#ifdef PLATFORM_WINDOWS
    CloseHandle(mIOCP);
    mIOCP = INVALID_HANDLE_VALUE;
#else
    close(mEpollFd);
    mEpollFd = -1;
#endif
    delete[] mEventEntries;
    mEventEntries = nullptr;
}

netxy::TimerMgr::PTR EventLoop::getTimerMgr()
{
    tryInitThreadID();
    assert(isInLoopThread());
    return isInLoopThread() ? mTimer : nullptr;
}

inline void EventLoop::tryInitThreadID()
{
    if (!mIsInitThreadID && !mIsInitThreadID.exchange(true))
    {
        mSelfThreadid = CurrentThread::tid();
    }
}

void EventLoop::loop(int64_t timeout)
{
    tryInitThreadID();

#ifndef NDEBUG
    assert(isInLoopThread());
#endif
    if (!isInLoopThread())
    {
        return;
    }

    /*  warn::���mAfterLoopProcs��Ϊ�գ�Ŀǰ������һ��loop(ʱ��֮ǰ������˻ص�����ĳ�Ự�Ͽ����������������Ự������Ϣ���²���callback������timeout��Ϊ0����ʾ������iocp/epoll wait   */
    if (!mAfterLoopProcs.empty())
    {
        timeout = 0;
    }

#ifdef PLATFORM_WINDOWS
    ULONG numComplete = 0;
    if (mPGetQueuedCompletionStatusEx != nullptr)
    {
        if (!mPGetQueuedCompletionStatusEx(mIOCP, mEventEntries, static_cast<ULONG>(mEventEntriesNum), &numComplete, static_cast<DWORD>(timeout), false))
        {
            numComplete = 0;
        }
    }
    else
    {
        /*��GQCS����ʧ�ܲ��账������������ֻ�������ϲ�����closesocket�ŷ�����������������onRecv�еõ�����(�����ͷŻỰ��Դ)*/
        do
        {
            GetQueuedCompletionStatus(mIOCP,
                &mEventEntries[numComplete].dwNumberOfBytesTransferred,
                &mEventEntries[numComplete].lpCompletionKey,
                &mEventEntries[numComplete].lpOverlapped,
                (numComplete == 0) ? static_cast<DWORD>(timeout) : 0);
        } while (mEventEntries[numComplete].lpOverlapped != nullptr && ++numComplete < mEventEntriesNum);
    }

    mIsInBlock = false;

    for (ULONG i = 0; i < numComplete; ++i)
    {
        auto channel = (Channel*)mEventEntries[i].lpCompletionKey;
        auto ovl = (EventLoop::ovl_ext_s*)mEventEntries[i].lpOverlapped;
        if (ovl->OP == EventLoop::OLV_VALUE::OVL_RECV)
        {
            channel->canRecv();
        }
        else if (ovl->OP == EventLoop::OLV_VALUE::OVL_SEND)
        {
            channel->canSend();
        }
        else
        {
            assert(false);
        }
    }
#else
    int numComplete = epoll_wait(mEpollFd, mEventEntries, mEventEntriesNum, timeout);

    mIsInBlock = false;

    for (int i = 0; i < numComplete; ++i)
    {
        auto    channel = (Channel*)(mEventEntries[i].data.ptr);
        auto    event_data = mEventEntries[i].events;

        if (event_data & EPOLLRDHUP)
        {
            channel->canRecv();
            channel->onClose();   /*  ���������öϿ�����(��ȫ�ģ���������ظ�close)���Է�canRecv��û��recv �Ͽ�֪ͨ*/
        }
        else
        {
            if (event_data & EPOLLIN)
            {
                channel->canRecv();
            }

            if (event_data & EPOLLOUT)
            {
                channel->canSend();
            }
        }
    }
#endif

    mIsAlreadyPostWakeup = false;
    mIsInBlock = true;

    processAsyncProcs();
    processAfterLoopProcs();

    if (numComplete == mEventEntriesNum)
    {
        /*  ����¼���������ˣ��������¼�������д�С��������һ��epoll/iocp wait��þ����ܸ����֪ͨ */
        reallocEventSize(mEventEntriesNum + 128);
    }

    mTimer->schedule();
}

void EventLoop::processAfterLoopProcs()
{
    mCopyAfterLoopProcs.swap(mAfterLoopProcs);
    for (auto& x : mCopyAfterLoopProcs)
    {
        x();
    }
    mCopyAfterLoopProcs.clear();
}

void EventLoop::processAsyncProcs()
{
    mAsyncProcsMutex.lock();
    mCopyAsyncProcs.swap(mAsyncProcs);
    mAsyncProcsMutex.unlock();

    for (auto& x : mCopyAsyncProcs)
    {
        x();
    }
    mCopyAsyncProcs.clear();
}

bool EventLoop::wakeup()
{
    bool ret = false;
    if (!isInLoopThread() && mIsInBlock && !mIsAlreadyPostWakeup.exchange(true))
    {
        mWakeupChannel->wakeup();
        ret = true;
    }

    return ret;
}

bool EventLoop::linkChannel(sock fd, Channel* ptr)
{
#ifdef PLATFORM_WINDOWS
    return CreateIoCompletionPort((HANDLE)fd, mIOCP, (ULONG_PTR)ptr, 0) != nullptr;
#else
    struct epoll_event ev = { 0, { 0 } };
    ev.events = EPOLLET | EPOLLIN | EPOLLRDHUP;
    ev.data.ptr = ptr;
    return epoll_ctl(mEpollFd, EPOLL_CTL_ADD, fd, &ev) == 0;
#endif
}

void EventLoop::pushAsyncProc(const USER_PROC& f)
{
    if (!isInLoopThread())
    {
        mAsyncProcsMutex.lock();
        mAsyncProcs.push_back(f);
        mAsyncProcsMutex.unlock();

        wakeup();
    }
    else
    {
        f();
    }
}

void EventLoop::pushAsyncProc(USER_PROC&& f)
{
    if (!isInLoopThread())
    {
        mAsyncProcsMutex.lock();
        mAsyncProcs.push_back(std::move(f));
        mAsyncProcsMutex.unlock();

        wakeup();
    }
    else
    {
        f();
    }
}

void EventLoop::pushAfterLoopProc(const USER_PROC& f)
{
    assert(isInLoopThread());
    if (isInLoopThread())
    {
        mAfterLoopProcs.push_back(f);
    }
}

void EventLoop::pushAfterLoopProc(USER_PROC&& f)
{
    assert(isInLoopThread());
    if (isInLoopThread())
    {
        mAfterLoopProcs.push_back(std::move(f));
    }
}

#ifndef PLATFORM_WINDOWS
int EventLoop::getEpollHandle() const
{
    return mEpollFd;
}
#endif

void EventLoop::reallocEventSize(size_t size)
{
    if (mEventEntries != NULL)
    {
        delete[] mEventEntries;
    }

#ifdef PLATFORM_WINDOWS
    mEventEntries = new OVERLAPPED_ENTRY[size];
#else
    mEventEntries = new epoll_event[size];
#endif

    mEventEntriesNum = size;
}
