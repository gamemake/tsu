#include "TsuInspectorCallback.h"

static ITsuInspectorCallback* InspectorCallback = nullptr;

void ITsuInspectorCallback::SetCallback(ITsuInspectorCallback* Callback)
{
    InspectorCallback = Callback;
}

ITsuInspectorCallback* ITsuInspectorCallback::GetCallback()
{
    return InspectorCallback;
}
