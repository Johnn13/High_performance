#include <set>
#include <functional>
#include <chrono>
#include <sys/epoll.h>
#include <iostream>

using namespace std;

struct TimerNodeBase
{
    time_t expire_;
};

struct TimerNode : public TimerNodeBase
{
    using cb = std::function<void()>;

    cb func_;

    TimerNode(time_t expire, cb func)
        : func_(func)
        {
            this->expire_ = expire;
        }
};

bool operator < (const TimerNodeBase &LTimer, const TimerNodeBase &RTimer)
{
    return LTimer.expire_ < RTimer.expire_;
}

class Timer 
{
public:
    void addTimer(time_t msc, TimerNode::cb func)
    {
        time_t expire = GetTick() + msc;
        timer_set.emplace(expire, func);
    }

    bool delTimer(const TimerNode &node)
    {
        auto it = timer_set.find(node);
        if(it != timer_set.end())
        {
            timer_set.erase(node);
            return true;
        }
        return false;
    }

    void HandleTimer(time_t now)
    {
        auto iter = timer_set.begin();
        while(iter != timer_set.end() && iter->expire_ <= now)
        {
            iter->func_();
            iter = timer_set.erase(iter);
        }
    }

    time_t TimeToSleep()
    {
        if(0 == timer_set.size())
        {
            return -1;
        }
        time_t time_gap = timer_set.begin()->expire_ - GetTick();
        return time_gap > 0 ? time_gap : 0;
    }

     // 返回从 steady_clock（通常是程序启动时间）到当前时间的毫秒数  
    static time_t GetTick()
    {
        auto sc = chrono::time_point_cast<chrono::microseconds>(chrono::steady_clock::now());
        auto tmp = chrono::duration_cast<chrono::milliseconds>(sc.time_since_epoch());
        return tmp.count();
    }

private:
    multiset<TimerNode> timer_set;
};

int main()
{
    int epfd = epoll_create(1);

    Timer timer;
    
    timer.addTimer(1000, [](){
        cout << "第一个任务" << endl;
    });

    timer.addTimer(2000, [](){
        cout << "第二个任务" << endl;
    });

    timer.addTimer(3000, [](){
        cout << "第三个任务" << endl;
    });

    epoll_event ev[64] = {};
    while(1)
    {
        int n = epoll_wait(epfd, ev, 64, timer.TimeToSleep());
        time_t cur_time = Timer::GetTick();
        timer.HandleTimer(cur_time);
    }
    return 0;
}