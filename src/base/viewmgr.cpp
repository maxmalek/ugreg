#include "viewmgr.h"
#include "mem.h"
#include <assert.h>
#include <string.h>

namespace view {

Mgr::Mgr()
    : TreeMem(StringPool::TINY)
{
    _store.makeMap(*this);
}

Mgr::~Mgr()
{
    _clear(); // don't need lock here
}

void Mgr::clear() // public clear is locked
{
    std::unique_lock lock(_mtx);
    _clear();
}

void Mgr::_clear()
{
    Var::Map* m = _store.map();
    for (Var::Map::Iterator it = m->begin(); it != m->end(); ++it)
    {
        View* vv = static_cast<View*>(it.value().asPtr());
        deleteView(vv);
    }
    _store.clear(*this);
}

bool Mgr::addViewDef(const char *key, VarCRef v)
{
    // --- LOCK WRITE ---
    std::unique_lock lock(_mtx);

    Var *dst = _store.map_unsafe()->putKey(*this, key, strlen(key));
    if(!dst)
        return false;

    // All went well earlier, move it over
    void* p = this->Alloc(sizeof(View));
    View* vp = _X_PLACEMENT_NEW(p) View(*this);

    // Make entry & clear if already existing
    if (void* old = dst->asPtr())
        deleteView(static_cast<View*>(old));

    if(!vp->load(v, true))
    {
        deleteView(vp);
        return false;
    }

    dst->setPtr(*this, vp);

    return true;
}


bool Mgr::initVM(VM& vm, const char* key, size_t keylen) const
{
    // --- LOCK READ ---
    std::shared_lock lock(_mtx);
    StrRef k = this->lookup(key, keylen);
    if(!k)
        return false;

    const Var *v = _store.lookupNoFetch(k);
    if(!v)
        return false;

    assert(v->type() == Var::TYPE_PTR);
    const View *vv = static_cast<View*>(v->asPtr());
    vm.init(vv->exe, vv->ep.data(), vv->ep.size());
    return true;
}

void Mgr::deleteView(View* v)
{
    v->~View();
    this->Free(v, sizeof(View));
}

} // end namespace view
