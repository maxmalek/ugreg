#pragma once

class MxStore;
struct MxSearchConfig;

struct EvTreeRebuilt
{
    virtual void onTreeRebuilt(const MxStore& mxs) = 0;
};
