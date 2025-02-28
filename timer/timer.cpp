#include <sys/epoll.h>
#include <functional>
#include <chrono>
#include <set>
#include <memory>
#include <iostream>

using namespace std;

struct TimerNodeBase
{
    time_t expire;  // 触发事件, Now + 定时时间
    uint64_t id;    // 由于时间可能重复，所以用 id 唯一表示定时任务q

    bool operator < (const TimerNodeBase &other) const
    {
        if(expire < other.expire)
            return true;
        else if(expire > other.expire)
            return false;
        else
            return id < other.id;       // 如果触发时间相等，则id的大放在后面触发
    }
};

struct TimerNode : public TimerNodeBase
{
    using Cb = std::function<void(const TimerNode &node)>;

    Cb func;
    TimerNode(uint64_t id, time_t expire, Cb func) 
        : func(func)
        {
            this->expire = expire;
            this->func = func;
        }
};


class Timer
{
public:
    // 返回从 steady_clock（通常是程序启动时间）到当前时间的毫秒数  
    static time_t GetTick()
    {
        auto sc = chrono::time_point_cast<chrono::microseconds>(chrono::steady_clock::now());
        auto tmp = chrono::duration_cast<chrono::milliseconds>(sc.time_since_epoch());
        return tmp.count();
    }

    // 给定时器添加定时任务
    TimerNodeBase AddTimer(time_t msec, TimerNode::Cb func)
    {
        time_t expire = GetTick() + msec;
        // 如果这个新添加的任务不是最晚触发的，则正常插入
        if(timeouts.empty() || expire <= timeouts.crbegin()->expire)
        {
            auto pairs = timeouts.emplace(GenID(), expire, std::move(func));
            return static_cast<TimerNodeBase>(*pairs.first);
        }
        // 如果新任务的触发时间 expire 比当前所有任务都晚，则 使用 emplace_hint() 进行优化插入
        auto ele = timeouts.emplace_hint(timeouts.crbegin().base(), GenID(), expire, std::move(func));
        return static_cast<TimerNodeBase>(*ele);
    }

    bool DelTimer(TimerNodeBase &node)
    {
        auto iter = timeouts.find(node);
        if(iter != timeouts.end())
        {
            timeouts.erase(iter);
            return true;
        }
        return false;
    }

    void HandleTimer(time_t now)
    {
        auto iter = timeouts.begin();
        while(iter != timeouts.end() && iter->expire <= now)
        {
            iter->func(*iter);
            iter = timeouts.erase(iter);
        }
    }

    time_t TimeToSleep()
    {
        if(0 == timeouts.size())
        {
            return -1;
        }
        time_t time_gap = timeouts.begin()->expire - GetTick();
        return time_gap > 0 ? time_gap : 0; 
    }

private:
    // 返回唯一的任务id
    static uint64_t GenID()
    {
        return gid++;
    }
    static uint64_t gid;
    set<TimerNode> timeouts;
};

uint64_t Timer::gid = 0;

int main()
{
    int epfd = epoll_create(1);

    unique_ptr<Timer> timer = make_unique<Timer>();

    int i = 0;
    timer->AddTimer(1000, [&](const TimerNode &node) {
        cout << Timer::GetTick() << " node id:" << node.id << " revoked times:" << ++i << endl;
    });

    timer->AddTimer(1000, [&](const TimerNode &node) {
        cout << Timer::GetTick() << " node id:" << node.id << " revoked times:" << ++i << endl;
    });

    timer->AddTimer(3000, [&](const TimerNode &node) {
        cout << Timer::GetTick() << " node id:" << node.id << " revoked times:" << ++i << endl;
    });

    auto node = timer->AddTimer(2100, [&](const TimerNode &node) {
        cout << Timer::GetTick() << " node id:" << node.id << " revoked times:" << ++i << endl;
    });

    timer->DelTimer(node);

    cout << "now time:" << Timer::GetTick() << endl;
    epoll_event ev[64] = {0};

    while(1)
    {
        int n = epoll_wait(epfd, ev, 64, timer->TimeToSleep());
        time_t now = Timer::GetTick();
        timer->HandleTimer(now);
    }
    return 0;
}