#pragma once

#include <map>
#include <shared_mutex>
#include <string>
#include "variant.h"
#include "view.h"
#include "treemem.h"

namespace view {

class View;

// Manages multiple views and can init a VM based on a named view.
// This class is thread-safe.
class Mgr : protected TreeMem
{
public:
    Mgr();
    ~Mgr();

    void clear();

    // v is a map with a single view (as supplied via the config)
    bool addViewDef(const char *key, VarCRef v);

    // Keep the views internal, makes ownership management & threading
    // much easier. This just inits a VM using a previously registered view.
    bool initVM(VM& vm, const char *key, size_t keylen) const;

private:
    void _clear();
    Var _store;
    mutable std::shared_mutex _mtx;

    void deleteView(View *v);
};

} // end namespace view
