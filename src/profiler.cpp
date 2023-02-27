#include <array>
#include <cstring>
#include <stdio.h>
#include <pico/time.h>
#include <pico/sync.h>
#include "profiler.h"
#include "debug.h"

#if PROFILE

#if defined(__cplusplus)
extern "C"{
#endif

constexpr uint32_t period_count_num = 127;

struct Measurement
{
    const char* name;
    uint32_t    total_count;
    uint64_t    total_spent;
    uint32_t    max_period;
    uint32_t    min_period;
    uint64_t    spent_period;
    uint32_t    max_period_inter;
    uint32_t    min_period_inter;
    uint64_t    spent_period_inter;
};
struct TaskProfileInfo
{
    const char*  name;
    uint32_t     start;
    uint64_t     running;
    uint64_t     idle;
    ProfileWorkStack *work_tail;
};

static std::array<uint8_t, 10240> g_task_profile_info_buf = {0};
static uint8_t g_task_profile_info_buf_tail;
static TaskProfileInfo* g_profile_info_top;
static size_t g_profile_info_max_num;
static size_t g_profile_info_num;
static Measurement* g_measurement_top;
static size_t g_measurement_max_num;
static uint64_t g_start_time;
static critical_section g_critical_section;


static inline void lock()
{
    critical_section_enter_blocking(&g_critical_section);
}

static inline void unlock()
{
    critical_section_exit(&g_critical_section);
}


void profile_initialize(uint8_t task_num, uint8_t measurement_num)
{
    g_profile_info_top = reinterpret_cast<TaskProfileInfo*>(g_task_profile_info_buf.begin());
    g_profile_info_max_num = task_num;
    g_profile_info_num = 0;
    const size_t measurement_offset = (sizeof(TaskProfileInfo)*task_num + 3)&(~3);
    g_measurement_top = reinterpret_cast<Measurement*>(g_task_profile_info_buf.begin() + measurement_offset);
    g_measurement_max_num = (g_task_profile_info_buf.size() - measurement_offset)/sizeof(Measurement);
    g_start_time = time_us_64();
    critical_section_init(&g_critical_section);

    dbg_assert(measurement_num < g_measurement_max_num);
}

uint8_t profile_create_group(const char* name)
{    
    dbg_assert(g_profile_info_num < g_profile_info_max_num);

    lock();

    auto info = g_profile_info_top + g_profile_info_num;
    info->name = name;
    info->start = time_us_32();

    g_profile_info_num++;

    unlock();

    return g_profile_info_num - 1;
}

void profile_measure_begin(uint8_t group_index, uint8_t mesurement_index, const char* name, ProfileWorkStack* work)
{
    dbg_assert(group_index < g_profile_info_num);

    auto info = g_profile_info_top + group_index;

    dbg_assert(mesurement_index < g_measurement_max_num);

    lock();

    auto mesurement = g_measurement_top + mesurement_index;
    mesurement->name = name;
    work->upper = info->work_tail;
    work->mesurement_index = mesurement_index;
    work->accum = 0;
    work->start = time_us_32();
    info->work_tail = work;

    unlock();
}

void profile_measure_end(uint8_t group_index)
{
    dbg_assert(group_index < g_profile_info_num);

    auto info = g_profile_info_top + group_index;

    dbg_assert(info->work_tail);

    lock();

    auto work = info->work_tail;
    auto mesurement = g_measurement_top + work->mesurement_index;

    const auto accum = work->accum + (time_us_32() - work->start);
    
    mesurement->total_spent += accum;
    ++mesurement->total_count;

    if(mesurement->max_period_inter < accum)
        mesurement->max_period_inter = accum;
    if(mesurement->min_period_inter > accum)
        mesurement->min_period_inter = accum;
    mesurement->spent_period_inter += accum;

    if((mesurement->total_count&period_count_num) == 0)
    {
        mesurement->max_period = mesurement->max_period_inter;
        mesurement->min_period = mesurement->min_period_inter;
        mesurement->spent_period = mesurement->spent_period_inter;
        mesurement->max_period_inter = 0;
        mesurement->min_period_inter = ~0;
        mesurement->spent_period_inter = 0;
    }

    info->work_tail = info->work_tail->upper;

    unlock();
}

void profile_print()
{
    const auto elapsed = time_us_64() - g_start_time;
    const auto elapsed_f = (double)elapsed;

    lock();

    dbg_printf("profiling:\n");
    dbg_printf("   elapsed: %llu\n", elapsed);

    dbg_printf("measurements:\n");
    dbg_printf("   label                     spent(s)  spent(%%)   count      min   max   ave \n");
    for(int i = 0; i < g_measurement_max_num; ++i)
    {
        auto& m = g_measurement_top[i]; 
        if(m.name)
        {
            char spent_sec[32];
            char spent_per[32];
            sprintf(spent_sec, "%5.4f", m.total_spent/1000000.0);
            sprintf(spent_per, "%3.2f", m.total_spent*100/elapsed_f);
            dbg_printf("   %-24s %10s %6s %12u %5u %5u %5u\n", m.name, spent_sec, spent_per, m.total_count, m.min_period, m.max_period, (uint32_t)(m.spent_period/period_count_num));
        }
    }

    unlock();
}

void profile_reset()
{
    lock();

    std::fill(g_measurement_top, g_measurement_top + g_measurement_max_num, Measurement{});

    for(int i = 0; i < g_profile_info_num; ++i)
    {
        auto& info = g_profile_info_top[i];
        info.idle = 0;
        info.running = 0;
        info.start = time_us_32();
    }

    g_start_time = time_us_64();

    unlock();
}

#if defined(__cplusplus)
}
#endif

#endif