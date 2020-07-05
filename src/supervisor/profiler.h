#pragma once

#include <Remotery.h>
#include "config_sv.h"
#include "global_context.h"

namespace vNerve::bilibili::profiler
{
void setup_profiling(config::config_supervisor* config, supervisor_global_context* ctxt);
void teardown_profiling();

#define VN_PROFILE_SCOPED(text) /*rmt_ScopedCPUSample(text, RMTSF_Aggregate);*/
#define VN_PROFILE_BEGIN(text) /*rmt_BeginCPUSample(text, RMTSF_Aggregate);*/
#define VN_PROFILE_END() /*rmt_EndCPUSample();*/

}
