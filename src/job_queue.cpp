#include <pico/sync.h>
#include "debug.h"
#include "job_queue.h"

namespace job_queue
{
    using node = data_structure::node;

    inline namespace internal
    {
        node    g_node_root = { &g_node_root, &g_node_root };
        critical_section g_critical_section;

        inline void lock()
        {
            critical_section_enter_blocking(&g_critical_section);
        }

        inline void unlock()
        {
            critical_section_exit(&g_critical_section);
        }

        inline uint8_t get_core_bit()
        {
            return 1 << get_core_num();
        }
    }

    
    void system::init()
    {
        critical_section_init(&g_critical_section);
    }

    void system::destroy()
    {
        critical_section_deinit(&g_critical_section);
    }
    
    void system::execute()
    {
        auto core_affinity = 1<<get_core_num();
        auto time = time_us_64();

        work *exec_job = nullptr;

        lock();
        {
            node* node = g_node_root.next();
            while(node && node != &g_node_root)
            {
                auto job = static_cast<work*>(node);
                if(job->m_pending 
                    && (job->m_running == 0)
                    && (job->m_affinity_mask&core_affinity) 
                    && (job->m_at == 0 || job->m_at < time))
                {
                    job->m_running = core_affinity;
                    job->m_pending = false;
                    job->remove();
                    exec_job = job;
                    break;
                }
                node = node->next();
            }
        }
        unlock();

        if(exec_job)
        {
            (*exec_job)();

            lock();
            {
                if(!exec_job->m_removed)
                    exec_job->insert(g_node_root.prev());
                exec_job->m_running = 0;
            }
            unlock();
        }
    }

    void work::activate()
    {
        m_at = 0;
        m_running = 0;
        m_pending = false;
        m_removed = false;
        
        lock();
        insert(g_node_root.prev());
        unlock();
    }
    
    void work::deactivate()
    {
        lock();
        {
            if(m_running)
                m_removed = true;
            else
                remove();
        }
        unlock();
    }

    void work::wait_done()
    {
        while(m_running);
    }

    void work::set_affinity_mask(uint8_t affinity_mask)
    {
        m_affinity_mask = affinity_mask;
    }

    void work::set_pending()
    {
        m_at = 0;
        m_pending = true;
    }

    void work::set_pending_delay_us(uint32_t delay)
    {
        m_at = time_us_64() + delay;
        m_pending = true;
    }

    void work::set_pending_at(uint64_t time)
    {
        m_at = time;
        m_pending = true;
    }

    bool work::is_idle() const
    {
        return !m_pending && (m_running == 0);
    }

}
