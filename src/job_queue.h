#pragma once

#include <stdint.h>
#include "node.h"

#if !defined(JOB_TRACE_ENABLE)
#define JOB_TRACE_ENABLE    0
#endif

namespace job_queue
{
    class system
    {
    public:
        static void init();
        static void destroy();
        static void execute();
    };

    class work : private data_structure::node
    {
        friend class system;
    public:
        virtual ~work() = default;

        void activate();
        void deactivate();

        void wait_done();
        
        void set_affinity_mask(uint8_t affinity_mask);
        void set_pending();
        void set_pending_delay_us(uint32_t delay);
        void set_pending_at(uint64_t time);

        bool is_idle() const;

    protected:
        virtual void operator()() = 0;
    private:
        uint64_t m_at;
        uint8_t m_affinity_mask;
        uint8_t m_running;
        uint8_t m_pending;
        uint8_t m_removed;
    };

    class work_fn : public work
    {
    public:
        using callback_t = void(*)(work*);
        
        void set_callback(callback_t fn)
        {
            m_fn = fn;
        }
    protected:
        virtual void operator()() override
        {
            if(m_fn)
                m_fn(this);
        }
    private:
        callback_t m_fn = nullptr;
    };

#if JOB_TRACE_ENABLE
    #define JOB_TRACE_LOG(...) dbg_printf("[JOB] " __VA_ARGS__)
#else
    #define JOB_TRACE_LOG(...) 
#endif
}