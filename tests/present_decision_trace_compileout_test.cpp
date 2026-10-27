#include "present_decision_trace.h"

int TraceCompileOutFunction(int value MINIMAP_PD_TRACE_PARAM(int*, trace))
{
    MINIMAP_PD_TRACE_IF(trace, ++value);
    return value;
}

int RunCompileOutProbe()
{
    int sideEffects = 0;
    const int result = TraceCompileOutFunction(10 MINIMAP_PD_TRACE_ARG(unresolved_trace));
    MINIMAP_PD_TRACE_IF(unresolved_trace, ++sideEffects);
    int gateEvaluations = 0;
    const bool gatePassed = MINIMAP_PD_GATE(unresolved_trace, Rejection::QueueNull, (++gateEvaluations, true));
    return result == 10 && sideEffects == 0 && gatePassed && gateEvaluations == 1 ? 0 : 1;
}
