#ifndef __DEADLOCK_DETETOR_H__
#define __DEADLOCK_DETETOR_H__

#include <stdio.h>
#include <stdint.h>

#include <unistd.h>
#include <pthread.h>

#include <deque>
#include <vector>
#include <map>
#include <memory>

#include "backward.hpp"
#include "spdlog/spdlog.h"
#include "spdlog/sinks/basic_file_sink.h"


using namespace backward;


struct thread_graphic_vertex_t
{
    int indegress;                  // 入度，表示该线程被多少个线程依赖
    std::vector<uint64_t> vertexs;  // 存储当前线程指向的其他线程 ID

    thread_graphic_vertex_t()
        : indegress(0)
        {}
};

class DeadLockGraphic{

public:
    static DeadLockGraphic &getInstance()
    {
        static DeadLockGraphic instance;
        return instance;
    }

    void lock_before(uint64_t thread_id, uint64_t lock_addr)
    {
        // 同时锁定 m_mutex_thread_apply_lock 和 m_mutex_thread_stacktrace
        std::lock(m_mutex_thread_apply_lock, m_mutex_thread_stacktrace);
        m_thread_apply_lock[thread_id] = lock_addr;

        // 展示保存堆栈信息，如果后面判断在此处发生死锁，把这里的堆栈打印出来
        StackTrace st;
        TraceResolver tr;
        st.load_here(64);
        tr.load_stacktrace(st);

        std::stringstream st_buffer;
        st_buffer << " thread_id " << thread_id
                  << " apply lock_addr " << lock_addr
                  << std::endl;

        for (size_t i = 0; i < st.size(); ++i) {

            ResolvedTrace trace = tr.resolve(st[i]);
            st_buffer << "#" << i
                      // << " " << trace.object_filename
                      // << " " << trace.object_function
                      // << " [" << trace.addr << "]"
                      << "  " << trace.source.filename
                      << "  " << trace.source.function
                      << "  " << trace.source.line
                      << std::endl;
        }    
    
        //std::cout<<st_buffer.str()<<std::endl;
        {
            std::lock_guard<std::mutex> m1(m_mutex_thread_stacktrace,std::adopt_lock);
            m_thread_stacktrace[thread_id] += st_buffer.str();
        }
    }

    /* 
        成功加锁后：
        1）从有向图中删除一条边
        2）从关系表中增加一条关系
    */
    void lock_after(uint64_t thread_id, uint64_t lock_addr)
    {
        std::lock(m_mutex_lock_belong_thread, m_mutex_thread_apply_lock, m_mutex_thread_stacktrace);
        {
            std::lock_guard<std::mutex> m0(m_mutex_thread_apply_lock, std::adopt_lock);
            m_thread_apply_lock.erase(thread_id);
        }

        {
            std::lock_guard<std::mutex> m1(m_mutex_thread_stacktrace, std::adopt_lock);
            m_thread_stacktrace.erase(thread_id);
        }

        {
            std::lock_guard<std::mutex> m2(m_mutex_lock_belong_thread, std::adopt_lock);
            m_lock_belong_thread[lock_addr] = thread_id;
        }
    }

    void unlock_after(uint64_t thread_id, uint64_t lock_addr)
    {
        std::lock_guard<std::mutex> m(m_mutex_lock_belong_thread);
        m_lock_belong_thread.erase(lock_addr);
    }

    void check_dead_lock()
    {
        std::map<uint64_t, uint64_t> lock_belong_thread;
        std::map<uint64_t, uint64_t> thread_apply_lock;
        std::map<uint64_t, std::string> thread_stacktrace;

        std::lock(m_mutex_lock_belong_thread, m_mutex_thread_apply_lock, m_mutex_thread_stacktrace);
        {
            std::lock_guard<std::mutex> m0(m_mutex_thread_apply_lock, std::adopt_lock);
            lock_belong_thread = m_lock_belong_thread;
        }

        {
            std::lock_guard<std::mutex> m1(m_mutex_thread_stacktrace, std::adopt_lock);
            thread_apply_lock = m_thread_apply_lock;
        }

        {
            std::lock_guard<std::mutex> m2(m_mutex_lock_belong_thread, std::adopt_lock);
            thread_stacktrace = m_thread_stacktrace;
        }

        // 构建有向图，邻接链表
        std::map<uint64_t, thread_graphic_vertex_t> graphics;
        for(auto it = thread_apply_lock.begin();
                    it != thread_apply_lock.end();  ++it)
        {
            uint64_t thd_id1 = it->first;
            uint64_t lock_id = it->second;
            if(lock_belong_thread.find(lock_id) == lock_belong_thread.end())
            {
                continue;   // 如果这个锁没有被其他线程持有，则跳过
            }

            // 锁的持有线程
            uint64_t thd_id2 = lock_belong_thread[lock_id];

            if(graphics.find(thd_id1) == graphics.end())
            {
                graphics[thd_id1] = thread_graphic_vertex_t();
            }

            if(graphics.find(thd_id2) == graphics.end())
            {
                graphics[thd_id2] = thread_graphic_vertex_t();
            }

            // 保存有向边 thd_id1 --> thd_id2
            graphics[thd_id1].vertexs.push_back(thd_id2);
            // 入度
            graphics[thd_id2].indegress++;
        }

        int graphicsSize = graphics.size();

        // 拓扑排序，入度为0 的节点先入队
        uint64_t counter = 0;
        std::deque<uint64_t> graphics_queue;
        for(auto it = graphics.begin(); it != graphics.end(); ++it)
        {
            uint64_t thd_id = it->first;
            const thread_graphic_vertex_t &gvert = it->second;
            if(gvert.indegress == 0)
            {
                graphics_queue.push_back(thd_id);
                ++counter;
            }
        }

        // 处理入度为 0 的节点
        while(!graphics_queue.empty())
        {
            // 取出入度为 0 的节点
            uint64_t thd_id = graphics_queue.front();
            graphics_queue.pop_front();

            // 删除边
            const thread_graphic_vertex_t &gvert = graphics[thd_id];
            // 遍历邻近有向边
            for(size_t i = 0; i < gvert.vertexs.size(); ++i)
            {
                uint64_t thd_id2 = gvert.vertexs[i];
                graphics[thd_id2].indegress--;
                if(graphics[thd_id2].indegress == 0)
                {
                    graphics_queue.push_back(thd_id2);
                    counter++;
                }
            }
            // 删除入度为 0 的点
            graphics.erase(thd_id);
        }

        if(counter != graphics.size())
        {
            printf("[ERROR!]: Found Dead Lock!!! \n");
            for(auto it = graphics.begin(); it != graphics.end(); ++it)
            {
                uint64_t thd_id = it->first;
                spdlog::info(thread_stacktrace[thd_id]);
                
                std::stringstream lock_belong_info;
                lock_belong_info << " The lock addr " << thread_apply_lock[thd_id]
                  << " is owned by " << lock_belong_thread[thread_apply_lock[thd_id]]
                  << std::endl;
                spdlog::info(lock_belong_info.str());

                m_file_logger->flush();
            }
        }
        else
        {
            printf("No Found Dead Lock! \n");
        }
    }

    void start_check()
    {
        pthread_t tid;
        pthread_create(&tid, NULL, thread_rountine, (void*)(this));
    }

    static void* thread_rountine(void *args)
    {
        DeadLockGraphic *ptr_graphics = static_cast<DeadLockGraphic *>(args);
        while(1)
        {
            sleep(10);
            ptr_graphics->check_dead_lock();
        }
    }

private:

    // 锁关系表: <threadid, 锁地址>
    std::mutex m_mutex_lock_belong_thread;
    std::map<uint64_t, uint64_t> m_lock_belong_thread;

    // 有向图: <threadid, 锁地址>
    std::mutex m_mutex_thread_apply_lock;
    std::map<uint64_t, uint64_t> m_thread_apply_lock;

    // 使用锁的调用堆栈: <>
    std::mutex m_mutex_thread_stacktrace;
    std::map<uint64_t, std::string> m_thread_stacktrace;

    std::shared_ptr<spdlog::logger> m_file_logger;

    DeadLockGraphic()
    {
        m_file_logger = spdlog::basic_logger_mt("basic_logger", "logs/basic.txt");
        spdlog::set_default_logger(m_file_logger);
    }
    ~DeadLockGraphic() = default;
    DeadLockGraphic(const DeadLockGraphic &) = default;
    DeadLockGraphic& operator=(const DeadLockGraphic &) = default;
};


// for C

#include <sys/syscall.h>

#ifdef __linux__
#define gettid() syscall(__NR_gettid)
#else
#define gettid() syscall(SYS_gettid)
#endif

// 用宏拦截 lock，添加 lock_before、lock_after 等操作，记录锁与线程的关系
#define pthread_mutex_lock(x)                                                                       \
    do {                                                                                            \
        DeadLockGraphic::getInstance().lock_before(gettid(), reinterpret_cast<uint64_t>(x));        \
        pthread_mutex_lock(x);                                                                      \
        DeadLockGraphic::getInstance().lock_after(gettid(), reinterpret_cast<uint64_t>(x));         \
    } while(false); 

// 拦截 unlock，添加 unlock_after，删除锁关系
#define pthread_mutex_unlock(x)                                                                     \
    do {                                                                                            \
        pthread_mutex_unlock(x);                                                                    \
        DeadLockGraphic::getInstance().unlock_after(gettid(), reinterpret_cast<uint64_t>(x));       \
    } while(false);

// for cpp
namespace std{

class DL_Mutex{
    public:
        DL_Mutex(){
            m_mutex = PTHREAD_MUTEX_INITIALIZER;
        }
        void Lock(){
            pthread_mutex_lock(&m_mutex);
        }
        void Unlock(){
            pthread_mutex_unlock(&m_mutex);
        }
    private:
        pthread_mutex_t m_mutex;


};

template <class T>
class DL_LockGuard{
    public:
        DL_LockGuard(T &mutex):m_lockguard_mutex(mutex){
            m_lockguard_mutex.Lock();
        }
        ~DL_LockGuard(){
            m_lockguard_mutex.Unlock();
        }
    private:
        T &m_lockguard_mutex;
};

#define mutex DL_Mutex
#define lock_guard DL_LockGuard
}

#endif