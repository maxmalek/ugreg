#pragma once

#include "types.h"

#include "tinyhashmap.h"

class TreeMem;
class Var;
class _VarMap;

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
        TYPE_NULL, // Important that this is 0
        TYPE_BOOL,
        // numeric types
        TYPE_INT,
        TYPE_UINT,
        TYPE_FLOAT,
        // misc -- not part of "normal" json data
        TYPE_PTR, // Not actually json but we use this type to store a userdata/void*
        TYPE_RANGE, // Another custom extension
        // containers -- always last
        TYPE_STRING,
        TYPE_ARRAY,
        TYPE_MAP,
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

    // transmute into type (does not lose data if the type is not changed)
    bool setBool(TreeMem& mem, bool x);
    s64 setInt(TreeMem& mem, s64 x);
    u64 setUint(TreeMem& mem, u64 x);
    double setFloat(TreeMem& mem, double x);
    StrRef setStr(TreeMem& mem, const char *x);
    StrRef setStr(TreeMem& mem, const char* x, size_t len);
    void *setPtr(TreeMem& mem, void *p);
    Var *makeArray(TreeMem& mem, size_t n);
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
          Var* lookup(StrRef s);     // NULL if not map or no such key
    const Var* lookup(StrRef s) const;

    // instantiate from types
    inline Var(std::nullptr_t) : Var() {}
    Var(bool x);
    Var(s64 x);
    Var(u64 x);
    Var(double x);
    Var(TreeMem& mem, const char* s);
    Var(TreeMem& mem, const char* s, size_t len);

    static const Var Null;

private:
    int numericCompare(const Var& b) const; // -1 if less, 0 if eq, +1 if greater
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
class VarExpiry
{
public:
    u64 ts;
    // TODO: manual reset event


    VarExpiry *clone(TreeMem& mem);
    VarExpiry();
    ~VarExpiry();

private:
    VarExpiry(const VarExpiry&) = delete;
    VarExpiry& operator=(const VarExpiry&) = delete;
};


// Note: Methods that don't take TreeMem don't modify the refcount!
class _VarMap
{
    //typedef std::unordered_map<StrRef, Var> _Map;
    typedef TinyHashMap<Var> _Map;
    ~_VarMap(); // call destroy() instead
public:
    typedef _Map::const_iterator Iterator;

    void destroy(TreeMem& mem); // deletes self
    _VarMap(TreeMem& mem);
    _VarMap(_VarMap&&) noexcept;

    void merge(TreeMem& dstmem, const _VarMap& o, const TreeMem& srcmem, MergeFlags mergeflags);
    void clear(TreeMem& mem);
    inline bool empty() const { return _storage.empty(); }
    _VarMap* clone(TreeMem& dstmem, const TreeMem& srcmem) const;
    inline size_t size() const { return _storage.size(); }

    Var& getOrCreate(TreeMem& mem, StrRef key); // return existing or insert new
    Var* get(StrRef key);
    const Var* get(StrRef key) const;

    Var& putKey(TreeMem& mem, const char* key, size_t len);

    Var& put(TreeMem& mem, StrRef k, Var&& x); // increases refcount if new key stored

    inline Iterator begin() const { return _storage.begin(); }
    inline Iterator end() const { return _storage.end(); }

    bool equals(const TreeMem& mymem, const _VarMap& o, const TreeMem& othermem) const;

    // return true when object is expired and should be deleted
    bool isExpired(u64 now) const;
    u64 getExpiryTime() const { return _expiry ? _expiry->ts : 0; } // FIXME

private: // disabled ops until we actually need them
    _VarMap(const _VarMap&) = delete;
    _VarMap& operator=(const _VarMap& o) = delete;


    static Var& _InsertAndRefcount(TreeMem& dstmem, _Map& storage, StrRef k);

    _Map _storage;
    VarExpiry *_expiry; // only allocated when necessary

    void _checkmem(const TreeMem& m) const;
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
    TreeMem *mem;

    VarRef() : v(0), mem(0) {}
    VarRef(TreeMem& mem, Var *x) : v(x), mem(&mem) {}
    VarRef(TreeMem *mem, Var* x) : v(x), mem(mem) {}

    inline operator Var*()              { return v; }
    inline operator const Var* () const { return v; }

    Var::Type type() const { return v->type(); }
    bool isNull() const { return v->isNull(); }
    const s64* asInt() const { return v->asInt(); }
    const u64* asUint() const { return v->asUint(); }
    PoolStr asString() const { return v->asString(*mem); } // (does not convert to string)
    const char* asCString() const { return v->asCString(*mem); }
    const double* asFloat() const { return v->asFloat(); };
    bool asBool() const { return v->asBool(); }
    const Var::Range* asRange() const { return v->asRange(); }

    // Returns this, transmuted to a different type. If the type is changed, old values are lost.
    VarRef& makeMap();
    VarRef& makeArray(size_t n);

    VarRef at(size_t idx) const;          // does not convert to array
    VarRef lookup(const char* key) const; // does not convert to map

    // converts into a map, creates key if not present, so that a construction like this:
    // VarRef ref = ...;
    // ref.clear();
    // ref["hello"]["world"] = 42; creates the following JSON: { "hello":{"world":42}}
    // The VarRef returned from this is always valid & safe, but if a new key was created it will be of TYPE_NULL.
    VarRef operator[](const char *key);

    // Merge o into this according to MergeFlags.
    // Returns false if o is not a map.
    // Turns own node into map if it is not already, then merges in o, then returns true.
    bool merge(const VarCRef& o, MergeFlags mergeflags);

    // Replaces own contents with o's contents.
    void replace(const VarCRef& o);

    // TODO: op=(copy) and op=(move) ?
    // TODO: also optimize related code paths for this->mem == o.mem

    // value assignment
    void clear() { v->clear(*mem); }
    inline VarRef& operator=(std::nullptr_t)     { v->clear(*mem);       return *this; }
    inline VarRef& operator=(bool x)        { v->setBool(*mem, x);  return *this; }
    inline VarRef& operator=(s64 x)         { v->setInt(*mem, x);   return *this; }
    inline VarRef& operator=(u64 x)         { v->setUint(*mem, x);  return *this; }
    inline VarRef& operator=(double x)      { v->setFloat(*mem, x); return *this; }
    inline VarRef& operator=(const char *s) { v->setStr(*mem, s);   return *this; }
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
    size_t size() const { return v->size(); }
    bool isNull() const { return v->isNull(); }
    const s64* asInt() const { return v->asInt(); }
    const u64* asUint() const { return v->asUint(); }
    PoolStr asString() const { return v->asString(*mem); } // (does not convert to string)
    const char* asCString() const { return v->asCString(*mem); }
    const double* asFloat() const { return v->asFloat(); };
    bool asBool() const { return v->asBool(); }
    const Var::Range *asRange() const { return v->asRange(); }

    VarCRef at(size_t idx) const;          // does not convert to array
    VarCRef lookup(const char *key) const; // does not convert to map

    Var::CompareResult compare(Var::CompareMode cmp, const VarCRef& o);
};

