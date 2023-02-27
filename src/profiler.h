#pragma once

#include <stdint.h>

#if PROFILE

#if defined(__cplusplus)
extern "C"{
#endif

typedef struct _ProfileWorkStack
{
    struct _ProfileWorkStack* upper;
    uint8_t     mesurement_index;
    uint32_t    start;
    uint32_t    accum;
} ProfileWorkStack;

void profile_initialize(uint8_t task_num, uint8_t measurement_num);
uint8_t profile_create_group(const char* name);
void profile_measure_begin(uint8_t group_index, uint8_t mesurement_index, const char* name, ProfileWorkStack* work);
void profile_measure_end(uint8_t group_index);
void profile_print();
void profile_reset();

#if defined(__cplusplus)
}
#endif

#if defined(__cplusplus)

class profile_measure_scoped
{
public:
    profile_measure_scoped(uint8_t mesurement_index, const char* name)
    {
        profile_measure_begin(get_core_num(), mesurement_index, name, &m_work);
    }
    ~profile_measure_scoped()
    {
        profile_measure_end(get_core_num());
    }

private:
    ProfileWorkStack    m_work;
};

#endif

#define PROFILE_INITIALIZE(task_num, measurement_num) \
    profile_initialize(task_num, measurement_num)
#define PROFILE_CREATE_GROUP(name) \
    profile_create_group(name)

#define PROFILE_MEASURE_BEGIN(label)    \
    ProfileWorkStack __profile_work_stack__##label##__LINE__; profile_measure_begin(get_core_num(), label, #label, &__profile_work_stack__##label##__LINE__)
#define PROFILE_MEASURE_END()       \
    profile_measure_end(get_core_num())

#define PROFILE_MEASURE_SCOPED(label)    \
    profile_measure_scoped __profile_measure_scoped##label##__LINE__(get_core_num(), label, #label);

#define PROFILE_PRINT()     \
    profile_print()

#define PROFILE_RESET()     \
    profile_reset()

#else

#define PROFILE_INITIALIZE(task_num, measurement_num)
#define PROFILE_CREATE_GROUP(name)
#define PROFILE_INSTALL()
#define PROFILE_MEASURE_BEGIN(label)    
#define PROFILE_MEASURE_END()       
#define PROFILE_MEASURE_SCOPED(label)
#define PROFILE_PRINT()
#define PROFILE_RESET()

#endif // defined(__cplusplus)

