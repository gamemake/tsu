#include "TsuInspectorCallback.h"

static ITsuInspectorCallback* InspectorCallback = nullptr;

void ITsuInspectorCallback::Set(ITsuInspectorCallback* Callback)
{
    InspectorCallback = Callback;
}

ITsuInspectorCallback* ITsuInspectorCallback::Get()
{
    return InspectorCallback;
}
