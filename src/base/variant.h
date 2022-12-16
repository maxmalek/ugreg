#pragma once

#include "types.h"

#include "tinyhashmap.h"
#include "upgrade_mutex.h"

class TreeMem;
class Var;
class _VarMap;
class _VarExtra;
class Accessor;
class Fetcher;
class TreeMemReadLocker;

struct _VarRange
{
    size_t first; // starts at 0
    size_t last; // inclusive
};

// For merge() operations.
// If not recursive, simply assign keys and replace the values of keys that are overwritten.
// If recursive, merge all maps recursively (all other values are replaced);
//  merging only applies if both values are maps. If one of them is not, one simply replaces the other.
// If MERGE_APPEND_ARRAYS is set, instead of replacing arrays, if both values are arrays, append one to the other.
enum MergeFlags // bitmask!
{
    MERGE_FLAT           = 0x00,
    MERGE_RECURSIVE      = 0x01,
    MERGE_APPEND_ARRAYS  = 0x02,
};
inline static MergeFlags operator|(MergeFlags a, MergeFlags b) {return MergeFlags(unsigned(a) | unsigned(b)); }


/* A small variant type that encapsulates all primitives available in JSON:
- Atom types:      null, number aka int/float, bool, string
- Compound types:   array, object aka map, string
- Maps have strings as keys, and and Var as value
With the following adjustment to the in-memory representation (vs. "normal" JSON):
- Distinction between float and int types (json only knows 'number')

Note that Var may or may not form a deeply nested tree structure.
In order to keep memory usage to a minimum and make things efficient,
all memory for compound types is stored externally.
The Var stores a pointer to the external memory,
but the actual management is done via a TreeMem structure that takes care of
string deduplication and also takes the role of a specialized allocator.

That also means that any time a Var possibly touches memory, the method takes a TreeMem.
Be careful not to mix up Vars created with different memory allocators, and to always pass the correct allocator.
There are neither checks nor protection against this and everything will go horribly wrong if you do mix things up.

For passing a Var around and do stuff more comfortably, use a VarRef -- that class has all the nice operators
and makes sure that the memory allocators are handled correctly.
*/
class Var
{
public:
    struct Policy
    {
        typedef TreeMem Allocator;
        inline static void OnDestroy(TreeMem& mem, Var& v)
        {
            v.clear(mem);
        }
    };

    Var();
    ~Var();
    Var(Var&& v) noexcept;

    // simply copying is forbidden. 2 reasons:
    // 1) when copying, we likely want a different allocator for the target. the copy ctor is unable to do that.
    // 2) enforcing move semantics unless an explicit copy is made is just so much better than accidentally
    //    copying a large tree
    Var(const Var& o) = delete;
    Var& operator=(const Var& o) = delete;

    enum Type
    {
        TYPE_NULL = 0, // Important that this is 0
        TYPE_BOOL,
        // numeric types
        TYPE_INT,
        TYPE_UINT,
        TYPE_FLOAT,
        // misc -- not part of "normal" json data
        TYPE_PTR, // Not actually json but we use this type to store a userdata/void*
        TYPE_RANGE, // Another custom extension
        // containers -- always last
        TYPE_STRING, // isContainer() returns false for this -- technically a string is an atom (scalar value)
        TYPE_ARRAY, // real container
        TYPE_MAP,   // real container
        // keep in sync with c_typeNames in the .cpp file!
    };

    // Also used as comparison operators in the VM
    enum CompareMode
    {
        CMP_EQ, // generic equality (no casting). recurses into arrays/maps. will never return CMP_RES_NA.
        CMP_LT, // numeric less than
        CMP_GT, // numeric greater than
        // string ops
        CMP_CONTAINS,
        CMP_STARTSWITH,
        CMP_ENDSWITH,
        // keep in sync with s_operatorNames[] in viewexec.cpp
        CMP_EXISTS,
    };

    enum CompareResult
    {
        CMP_RES_NA = -1,    // can't compare / not applicable
        CMP_RES_FALSE = 0,  // not equal
        CMP_RES_TRUE = 1,   // equal
    };

    enum Topbits
    {
        BITS_OTHER  = 0, // 00
        BITS_STRING = 1, // 01
        BITS_ARRAY  = 2, // 10
        BITS_MAP    = 3  // 11
    };

    /* Encoding:
    <- highest bit ---- lowest bit ->
    0?xxx... = object is atom
    1?xxx... = object is compound type aka has external data (array, map)
    10xxx... = object is array        (lower bits: size)
    11xxx... = object is map          (lower bits ignored)
    01xxx... = object is string       (lower bits: length)
    00xxx... = meta is one of the remaining enum Types
               (can just switch() over those since the high bits are 0)
    */
    size_t meta;

    typedef _VarMap Map;
    typedef _VarRange Range;
    typedef _VarExtra Extra;

    // either the value or pointer to value
    union
    {
        StrRef s;   // when string
        Var *a;     // when array; entries in a[0..N), N is the lower bits of meta
        Map *m;

        s64 i;      // when signed integer type
        u64 ui;     // ... or unsigned
        double f;   // when float type
        void *p;    // when ptr/userdata
        Range *ra;  // when range
    } u;

    enum Priv : size_t
    {
        SHIFT_TOPBIT = (sizeof(meta) * CHAR_BIT) - 1u,
        SHIFT_TOP2BITS = SHIFT_TOPBIT - 1u,
        SIZE_MASK = size_t(-1) >> 2u // upper 2 bits 0, rest 1
    };

    enum Limits : size_t
    {
        MAXSIZE  = SIZE_MASK,
        MAX_SIZE_BITS = SHIFT_TOP2BITS
    };

    void _settop(TreeMem& mem, Topbits top, size_t size);
    void _transmute(TreeMem& mem, size_t newmeta);
    void _adjustsize(size_t newsize);

    inline Topbits _topbits() const { return Topbits(meta >> SHIFT_TOP2BITS); }
    inline bool isContainer() const { return meta >> SHIFT_TOPBIT; } // highest bit set?
    inline bool isAtom()     const { return !isContainer(); }
    inline size_t _size()  const { return meta & SIZE_MASK; } // valid for string and array (but not map)
    size_t size() const;

    void clear(TreeMem& mem); // sets to null and clears memory. call this before it goes out of scope.

    Var clone(TreeMem& dstmem, const TreeMem& srcmem) const;

    Type type() const;
    const char *typestr() const;

    bool equals(const TreeMem& mymem, const Var& o, const TreeMem& othermem) const;
    CompareResult compare(CompareMode cmp, const TreeMem& mymem, const Var& o, const TreeMem& othermem) const;
    bool compareExact(const TreeMem& mymem, const Var& o, const TreeMem& othermem) const;
    bool compareExactSameMem(const Var& o) const;
    // transmute into type (does not lose data if the type is not changed)
    bool setBool(TreeMem& mem, bool x);
    s64 setInt(TreeMem& mem, s64 x);
    u64 setUint(TreeMem& mem, u64 x);
    double setFloat(TreeMem& mem, double x);
    StrRef setStr(TreeMem& mem, const char *x);
    StrRef setStr(TreeMem& mem, const char* x, size_t len);
    StrRef setStrRef(TreeMem& mem, StrRef r);
    void *setPtr(TreeMem& mem, void *p);
    Var *makeArray(TreeMem& mem, size_t n); // this may return NULL for n == 0
    Map *makeMap(TreeMem& mem, size_t prealloc = 0);
    Range *setRange(TreeMem& mem, const Range *ra, size_t n);

    // value extration (get pointer to value if valid or NULL if not possible/wrong type). no asserts.
    bool isNull() const { return meta == TYPE_NULL; }
    const s64 *asInt() const;
    const u64 *asUint() const;
    StrRef asStrRef() const;
    PoolStr asString(const TreeMem& mem) const; // (does not convert to string)
    const char *asCString(const TreeMem& mem) const;
    const double *asFloat() const;
    bool asBool() const; // only true if really bool type and true, false otherwise
    void *asPtr() const;
    const Range *asRange() const;

    Var& operator=(Var&& o) noexcept;

    // array ops
    inline       Var* array()       { return _topbits() == BITS_ARRAY ? u.a : 0; } // checked, returns Var[], NULL if not array
    inline const Var* array() const { return _topbits() == BITS_ARRAY ? u.a : 0; }
          Var *array_unsafe();   // unchecked, asserts that it's array but not in release mode
    const Var *array_unsafe() const;
          Var *at(size_t n);            // checked access, NULL if not array or invalid index
    const Var *at(size_t n) const;
          Var& operator[](size_t i); // unchecked, asserts that it's an array and that the index is valid
    const Var& operator[](size_t i) const;

    // map ops
    inline       Var::Map* map()       { return _topbits() == BITS_MAP ? u.m : 0; }
    inline const Var::Map* map() const { return _topbits() == BITS_MAP ? u.m : 0; }
          Var::Map* map_unsafe();    // asserts that map
    const Var::Map* map_unsafe() const;
          Var* lookupNoFetch(StrRef s);     // NULL if not map or no such key
    const Var* lookupNoFetch(StrRef s) const;
          Var* lookup(StrRef s);     // NULL if not map or no such key, and try to fetch
    const Var* lookup(StrRef s) const;
          Var* lookup(const TreeMem& mem, const char* key, size_t len);     // lookup first, fetch if it doesn't exist
    const Var* lookup(const TreeMem& mem, const char* key, size_t len) const;
    Var* fetchOne(const char *key, size_t len); // like lookup(), but try to fetch from remote if key doesn't exist
    bool fetchAll(); // fetch stuff and populate. returns true on success. Caller must check canFetch() first.
    const bool canFetch() const;
    Var::Extra *getExtra();
    const Var::Extra *getExtra() const;

    // tree traversal
    // bitmask
    enum SubtreeQueryFlags
    {
        SQ_DEFAULT = 0x00,
        SQ_CREATE = 0x01,
        SQ_NOFETCH = 0x02, // fetching is on by default; this turns it off
    };

    // /path/to/subnode
    Var *subtreeOrFetch(TreeMem& mem, const char *path, SubtreeQueryFlags qf = SQ_DEFAULT);

    // like subtree(..., SQ_NOFETCH)
    const Var *subtreeConst(const TreeMem& mem, const char *path) const;

    // instantiate from types
    inline Var(std::nullptr_t) : Var() {}
    Var(bool x);
    Var(s64 x);
    Var(u64 x);
    Var(double x);
    Var(TreeMem& mem, const char* s);
    Var(TreeMem& mem, const char* s, size_t len);

    static const Var Null;

    static void ClearArray(TreeMem& mem, Var *p, size_t n);
    static void ClearArrayRange(TreeMem& mem, Var *begin, Var *end); // clear [begin..end)

    // --- DANGER ZONE ----
    // caller MUST properly destroy existing elements before shrinking, and init ALL new elements after enlarging
    // (It is valid to call this, keep the end uninited, then call this again to clip off the uninited end)
    // -> failing to usethis properly will caused memory leaks, crashes, and generally UB.
    // read the code *carefully* before calling this!
    Var* makeArrayUninitialized_Dangerous(TreeMem& mem, size_t n);

private:
    int numericCompare(const Var& b) const; // -1 if less, 0 if eq, +1 if greater
    bool _compareExactDifferentMem(const TreeMem& mymem, const Var& o, const TreeMem& othermem) const;
};

// -------------------------------------------------

// TODO:
// also need a mutex to allow multiple readers to wait until an eventual re-fetch is done
// idea: lock shared, if need to fetch: lock unique, return, queue for re-query (will be blocked by unique lock),
// once fetch arrives: merge into tree, unlock unique lock.
// -- store fetch endpoints in a separate tree
// -- when we query something but it is an expired map, return empty-handed
// -- then go visit the fetch endpoints tree and check what must be fetched
// -- queue re-fetch and wait until that is done
// --> maybe add TYPE_USERDATA that stores a void* and use that for the fetch tree
// fetch tree entry: pointer to in-progress future, if any; script to call; params;


// Extra data attached to a Map that are uncommon enough to warrant externalizing to an extra struct.
//
class _VarExtra
{
public:
    u64 expiryTS; // timestamp when the map this is attached to expires
    _VarMap& mymap;
    acme::upgrade_mutex& writemutex;
    //_VarMap *fetchedBy;
    TreeMem& mem;
    Fetcher *fetcher;

    // FIXME: this should probably be atomic
    bool datavalid; // false if expired, not fetched, etc // TODO: reset this to false at some point

    bool check(const Accessor& a) const;
    _VarExtra *clone(TreeMem& mem, _VarMap& m);
    _VarExtra(_VarMap& m, TreeMem& mem, acme::upgrade_mutex& mutex);
    ~_VarExtra(); // TODO: kill dtor, replace with destroy() method? (then it can be refcounted)

private:
    // always attached to a Var::Vap and not movable afterwards
    _VarExtra(_VarExtra&&) = delete;
    _VarExtra(const _VarExtra&) = delete;
    _VarExtra operator=(_VarExtra&&) = delete;
    _VarExtra operator=(const _VarExtra&) = delete;
};


// Note: Methods that don't take TreeMem don't modify the string keys' refcount!
class _VarMap
{
    typedef TinyHashMap<Var, Var::Policy> _Map;
    typedef typename _Map::TVec Vec;
    ~_VarMap(); // call destroy() instead
public:
    // there's rarely any reason to change things while iterating,
    // so we'll make the const_iterator the default unless explicitly specified
    typedef _Map::const_iterator Iterator;
    typedef _Map::iterator MutIterator;
    typedef _VarExtra Extra;

    void destroy(TreeMem& mem); // deletes self
    _VarMap(TreeMem& mem, size_t prealloc = 0);
    _VarMap(_VarMap&&) noexcept;

    bool merge(TreeMem& dstmem, const _VarMap& o, const TreeMem& srcmem, MergeFlags mergeflags);
    void clear(TreeMem& mem);
    inline bool empty() const { return _storage.empty(); }
    _VarMap* clone(TreeMem& dstmem, const TreeMem& srcmem) const;
    inline size_t size() const { return _storage.size(); }

    Var* getOrCreate(TreeMem& mem, StrRef key); // return existing or insert new
          Var* getNoFetch(StrRef key);
    const Var* getNoFetch(StrRef key) const;
          Var* get(const TreeMem& mem, const char* key, size_t len);
    const Var* get(const TreeMem& mem, const char* key, size_t len) const;
          Var* get(StrRef key);
    const Var* get(StrRef key) const;
    const Vec& values() const { return _storage.values(); }
    Var* fetchOne(const char *key, size_t len); // always fetch
    bool fetchAll();                       // always fetch and replace own data

    // always returns ptr to valid object unless OOM
    Var* putKey(TreeMem& mem, const char* key, size_t len);

    // always returns ptr to valid object unless OOM
    Var* put(TreeMem& mem, StrRef k, Var&& x); // increases refcount if new key stored

    Iterator begin() const;
    inline Iterator end() const { return _storage.end(); }
    MutIterator begin();
    inline MutIterator end() { return _storage.end(); }

    bool equals(const TreeMem& mymem, const _VarMap& o, const TreeMem& othermem) const;

    Extra* ensureExtra(TreeMem& mem, acme::upgrade_mutex& mutex);
    inline Extra* getExtra() const { return _extra; }

    // intended for setting the extra data after creating the map
    void setExtra(Extra *extra);

    // return true when object is expired and should be deleted
    bool isExpired(u64 now) const;
    // return timestamp when object expires or 0 for no expiry
    u64 getExpiryTime() const { return _extra ? _extra->expiryTS : 0; }

    bool check(const Accessor& a) const;
    //void ensureData(u64 now) const;

private:
    _VarMap(const _VarMap&) = delete;
    _VarMap& operator=(const _VarMap& o) = delete;

    static Var* _InsertAndRefcount(TreeMem& dstmem, _Map& storage, StrRef k);

    _Map _storage;
    Extra *_extra; // only allocated when necessary

    void _checkmem(const TreeMem& m) const;

    // For debugging, store a pointer to our originating allocator.
    // If we errorneously get passed a pointer to a different allocator, fail an assert().
#ifdef _DEBUG
    TreeMem* const _mymem;
#endif
};

// -------------------------------------------------


class VarRef;
class VarCRef;

// Note: v can be NULL!
// This class is intended to be passed by value and constructed on the fly as needed.
// Don't store it in data structures! If the underlying memory goes away, the VarRef will contain a dangling pointer.
// Check for validity via:
// VarRef r = ...;
// if(r) { all good; }
class VarRef
{
public:
    Var * const v;
    TreeMem * const mem;

    VarRef() : v(0), mem(0) {}
    VarRef(TreeMem& mem, Var *x) : v(x), mem(&mem) {}
    VarRef(TreeMem *mem, Var* x) : v(x), mem(mem) {}

    inline operator Var*()              { return v; }
    inline operator const Var* () const { return v; }

    Var::Type type() const { return v->type(); }
    const char* typestr() const { return v->typestr(); }
    size_t size() const { return v->size(); }
    bool isNull() const { return v->isNull(); }
    bool isContainer() const { return v->isContainer(); }
    const s64* asInt() const { return v->asInt(); }
    const u64* asUint() const { return v->asUint(); }
    PoolStr asString() const { return v->asString(*mem); } // (does not convert to string)
    const char* asCString() const { return v->asCString(*mem); }
    const double* asFloat() const { return v->asFloat(); };
    bool asBool() const { return v->asBool(); }
    const Var::Range* asRange() const { return v->asRange(); }
    void* asPtr() const { return v->asPtr(); }

    // Returns this, transmuted to a different type. If the type is changed, old values are lost.
    VarRef& makeMap(size_t prealloc = 0);
    VarRef& makeArray(size_t n);

    VarRef at(size_t idx) const;          // does not convert to array
    VarRef lookup(const char* key) const; // does not convert to map
    VarRef lookup(const char* key, size_t len) const; // does not convert to map
    VarRef lookup(StrRef k); // does not convert to map

    // converts into a map, creates key if not present, so that a construction like this:
    // VarRef ref = ...;
    // ref.clear();
    // ref["hello"]["world"] = 42; creates the following JSON: { "hello":{"world":42}}
    // The VarRef returned from this is always valid & safe, but if a new key was created it will be of TYPE_NULL.
    VarRef operator[](const char *key);
    VarRef operator[](PoolStr ps);

    // Merge o into this according to MergeFlags.
    // Returns false if o is not a map.
    // Turns own node into map if it is not already, then merges in o, then returns true.
    bool merge(const VarCRef& o, MergeFlags mergeflags);

    // Replaces own contents with o's contents.
    void replace(const VarCRef& o);

    Var clone(TreeMem& dst) const;

    // TODO: op=(copy) and op=(move) ?
    // TODO: also optimize related code paths for this->mem == o.mem

    // value assignment
    void clear() const { v->clear(*mem); }
    inline VarRef& operator=(std::nullptr_t){ v->clear(*mem);       return *this; }
    inline VarRef& operator=(bool x)        { v->setBool(*mem, x);  return *this; }
    inline VarRef& operator=(s64 x)         { v->setInt(*mem, x);   return *this; }
    inline VarRef& operator=(u64 x)         { v->setUint(*mem, x);  return *this; }
    inline VarRef& operator=(double x)      { v->setFloat(*mem, x); return *this; }
    inline VarRef& operator=(const char *s) { v->setStr(*mem, s);   return *this; }
    inline VarRef& operator=(void *p)       { v->setPtr(*mem, p);   return *this; }

    inline void setStr(const char *s, size_t len) { v->setStr(*mem, s, len); }

private:
    // Template resolution comes before (implicit) cast, so we don't accidentally convert
    // 'const void*' to bool when assigning.
    // This is to prevent hard to track down errorneous implicit casts.
    // If this ever pops an error, explicitly cast upon assigning.
    template<typename T> VarRef& operator=(const T&); // not defined anywhere
};

// Like VarRef, except immutable and can be constructed from VarRef
class VarCRef
{
public:
    const Var* v;
    const TreeMem *mem;

    VarCRef() : v(0), mem(0) {}
    VarCRef(const TreeMem& mem, const Var* x) : v(x), mem(&mem) {}
    VarCRef(const TreeMem *mem, const Var* x) : v(x), mem(mem) {}
    VarCRef(VarRef r) : v(r.v), mem(r.mem) {}

    static inline VarCRef Null(const TreeMem& m) { return VarCRef(m, &Var::Null); }
    static inline VarCRef Null(const TreeMem *m) { return VarCRef(m, &Var::Null); }
    inline void makenull() { v = &Var::Null; }


    inline operator const Var* () const { return v; }

    Var::Type type() const { return v->type(); }
    const char* typestr() const { return v->typestr(); }
    size_t size() const { return v->size(); }
    bool isNull() const { return v->isNull(); }
    bool isContainer() const { return v->isContainer(); }
    const s64* asInt() const { return v->asInt(); }
    const u64* asUint() const { return v->asUint(); }
    PoolStr asString() const { return v->asString(*mem); } // (does not convert to string)
    const char* asCString() const { return v->asCString(*mem); }
    const double* asFloat() const { return v->asFloat(); };
    bool asBool() const { return v->asBool(); }
    const Var::Range *asRange() const { return v->asRange(); }
    void *asPtr() const { return v->asPtr(); }

    VarCRef at(size_t idx) const;          // does not convert to array
    VarCRef lookup(const char *key) const; // does not convert to map
    VarCRef lookup(const char *key, size_t len) const; // does not convert to map
    VarCRef lookup(StrRef k); // does not convert to map
    Var clone(TreeMem& dst) const;

    Var::CompareResult compare(Var::CompareMode cmp, const VarCRef& o);
};

// this is specialized
template<>
Var* mem_construct_default<Var>(Var* begin, Var* end);
