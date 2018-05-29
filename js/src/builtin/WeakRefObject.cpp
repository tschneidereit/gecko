/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=8 sts=4 et sw=4 tw=99:
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "builtin/WeakRefObject.h"

template<class T>
MOZ_ALWAYS_INLINE bool
Is(const HandleValue v)
{
    return v.isObject() && v.toObject().is<T>();
}

template <>
inline bool
JSObject::is<js::WeakCellObject>() const
{
    const js::Class* cls = getClass();
    return cls == &WeakCellObject::class_ || cls == &WeakRefObject::class_;
}

static ObjectValueMap*
GetCellsToTargetsMap(WeakFactoryObject* factory) {
    return static_cast<ObjectValueMap*>(factory->getPrivate());
}

static MOZ_MUST_USE ObjectValueMap*
GetOrCreateCellsToTargetsMap(JSContext* cx, Handle<WeakFactoryObject*> factory) {
    auto map = static_cast<ObjectValueMap*>(factory->getPrivate());
    if (map)
        return map;

    auto newMap = cx->make_unique<ObjectValueMap>(cx, factory.get());
    if (!newMap)
        return nullptr;
    if (!newMap->init()) {
        JS_ReportOutOfMemory(cx);
        return nullptr;
    }

    map = newMap.release();
    factory->setPrivate(map);

    return map;
}

/* static*/
MOZ_MUST_USE WeakFactoryObject*
WeakFactoryObject::create(JSContext* cx, HandleObject proto /* = nullptr */)
{
    Rooted<WeakFactoryObject*> weakFactory(cx);
    weakFactory = NewObjectWithClassProto<WeakFactoryObject>(cx, proto);
    if (!weakFactory)
        return nullptr;
    return weakFactory;
}

/* static */ JSObject*
WeakFactoryObject::cellTarget(WeakCellObject* cell)
{
    auto map = GetCellsToTargetsMap(this);
    if (!map)
        return nullptr;

    if (ObjectValueMap::Ptr ptr = map->lookup(cell))
        return &ptr->value().toObject();

    return nullptr;
}

static MOZ_MUST_USE bool
WeakFactoryObject_construct(JSContext* cx, unsigned argc, Value* vp)
{
    CallArgs args = CallArgsFromVp(argc, vp);

    Rooted<WeakFactoryObject*> weakFactory(cx, WeakFactoryObject::create(cx));
    if (!weakFactory)
        return false;

    args.rval().setObject(*weakFactory);
    return true;
}

static void
WeakFactoryObject_trace(JSTracer* trc, JSObject* obj)
{
    if (ObjectValueMap* map = GetCellsToTargetsMap(&obj->as<WeakFactoryObject>()))
        map->trace(trc);
}

static void
WeakFactoryObject_finalize(FreeOp* fop, JSObject* obj)
{
    MOZ_ASSERT(fop->maybeOnHelperThread());
    if (ObjectValueMap* map = GetCellsToTargetsMap(obj->as<WeakFactoryObject>()))
        fop->delete_(map);
}

template<typename T>
MOZ_MUST_USE bool
MakeWeakCellOrRef(JSContext* cx, const CallArgs& args)
{
    if (!args.get(0).isObject()) {
        JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr, JSMSG_NOT_NONNULL_OBJECT, "target");
        return false;
    }

    RootedObject target(cx, &args[0].toObject());
    Rooted<WeakFactoryObject*> factory(cx, &args.thisv().toObject().as<WeakFactoryObject>());
    Rooted<T*> cell(cx, T::create(cx, args.get(1), factory));
    if (!cell)
        return false;

    auto map = GetOrCreateCellsToTargetsMap(cx, factory);
    if (!map)
        return false;

    if (!map->put(cell, ObjectValue(*target))) {
        JS_ReportOutOfMemory(cx);
        return false;
    }

    args.rval().setObject(*cell);
    return true;
}

MOZ_ALWAYS_INLINE bool
WeakFactory_makeCell_impl(JSContext* cx, const CallArgs& args)
{
    return MakeWeakCellOrRef<WeakCellObject>(cx, args);
}

static bool
WeakFactory_makeCell(JSContext* cx, unsigned argc, Value* vp)
{
    CallArgs args = CallArgsFromVp(argc, vp);
    return CallNonGenericMethod<Is<WeakFactoryObject>, WeakFactory_makeCell_impl>(cx, args);
}

MOZ_ALWAYS_INLINE bool
WeakFactory_makeRef_impl(JSContext* cx, const CallArgs& args)
{
    return MakeWeakCellOrRef<WeakRefObject>(cx, args);
}

static bool
WeakFactory_makeRef(JSContext* cx, unsigned argc, Value* vp)
{
    CallArgs args = CallArgsFromVp(argc, vp);
    return CallNonGenericMethod<Is<WeakFactoryObject>, WeakFactory_makeRef_impl>(cx, args);
}

MOZ_ALWAYS_INLINE bool
WeakFactory_cleanupSome_impl(JSContext* cx, const CallArgs& args)
{
    MOZ_ASSERT(Is<WeakFactoryObject>(args.thisv()));

    args.rval().setUndefined();
    return true;
}

static bool
WeakFactory_cleanupSome(JSContext* cx, unsigned argc, Value* vp)
{
    CallArgs args = CallArgsFromVp(argc, vp);
    return CallNonGenericMethod<Is<WeakFactoryObject>, WeakFactory_cleanupSome_impl>(cx, args);
}

MOZ_ALWAYS_INLINE bool
WeakFactory_shutdown_impl(JSContext* cx, const CallArgs& args)
{
    MOZ_ASSERT(Is<WeakFactoryObject>(args.thisv()));

    args.rval().setUndefined();
    return true;
}

static bool
WeakFactory_shutdown(JSContext* cx, unsigned argc, Value* vp)
{
    CallArgs args = CallArgsFromVp(argc, vp);
    return CallNonGenericMethod<Is<WeakFactoryObject>, WeakFactory_shutdown_impl>(cx, args);
}

const JSPropertySpec WeakFactoryObject::properties[] = {
    JS_PS_END
};

const JSFunctionSpec WeakFactoryObject::methods[] = {
    JS_FN("makeCell",       WeakFactory_makeCell,       1, 0),
    JS_FN("makeRef",        WeakFactory_makeRef,        1, 0),
    JS_FN("cleanupSome",    WeakFactory_cleanupSome,    1, 0),
    JS_FN("shutdown",       WeakFactory_shutdown,       0, 0),
    JS_FS_END
};

static const ClassOps WeakFactoryObjectClassOps = {
    nullptr,        /* addProperty */
    nullptr,        /* delProperty */
    nullptr,        /* enumerate */
    nullptr,        /* newEnumerate */
    nullptr,        /* resolve */
    nullptr,        /* mayResolve */
    WeakFactoryObject_finalize,
    nullptr,        /* call        */
    nullptr,        /* hasInstance */
    nullptr,        /* construct   */
    WeakFactoryObject_trace,
};

static const ClassSpec WeakFactoryObjectClassSpec = {
    GenericCreateConstructor<WeakFactoryObject_construct, 0, gc::AllocKind::FUNCTION>,
    GenericCreatePrototype,
    nullptr,
    nullptr,
    WeakFactoryObject::methods,
    WeakFactoryObject::properties
};

const Class WeakFactoryObject::class_ = {
    "WeakFactory",
    JSCLASS_HAS_PRIVATE |
    JSCLASS_HAS_RESERVED_SLOTS(WeakFactorySlots) |
    JSCLASS_HAS_CACHED_PROTO(JSProto_WeakFactory) |
    JSCLASS_BACKGROUND_FINALIZE,
    &WeakFactoryObjectClassOps,
    &WeakFactoryObjectClassSpec
};

const Class WeakFactoryObject::protoClass_ = {
    "object",
    JSCLASS_HAS_CACHED_PROTO(JSProto_WeakFactory),
    JS_NULL_CLASS_OPS,
    &WeakFactoryObjectClassSpec
};

static WeakFactoryObject*
GetFactoryFromCell(WeakCellObject* cell)
{
    return &cell->getFixedSlot(WeakCellSlot_factory).toObject().as<WeakFactoryObject>();
}

static JSObject*
GetCellTarget(WeakCellObject* cell)
{
    WeakFactoryObject* factory = GetFactoryFromCell(cell);
    return factory->cellTarget(cell);
}

template<typename T>
static MOZ_MUST_USE T*
CreateWeakCellOrRef(JSContext* cx, HandleValue holdings, Handle<WeakFactoryObject*> factory,
                    HandleObject proto)
{
    T* cell = NewObjectWithClassProto<T>(cx);
    if (!cell)
        return nullptr;

    cell->setFixedSlot(WeakCellSlot_factory, ObjectValue(*factory));
    cell->setFixedSlot(WeakCellSlot_holdings, holdings);
    return cell;
}

/* static*/
MOZ_MUST_USE WeakCellObject*
WeakCellObject::create(JSContext* cx, HandleValue holdings, Handle<WeakFactoryObject*> factory,
                       HandleObject proto /* = nullptr */)
{
    return CreateWeakCellOrRef<WeakCellObject>(cx, holdings, factory, proto);
}

static MOZ_MUST_USE bool
WeakCellObject_construct(JSContext* cx, unsigned argc, Value* vp)
{
    JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr, JSMSG_NO_CONSTRUCTOR, "WeakCell");
    return false;
}

MOZ_ALWAYS_INLINE bool
WeakCell_holdings_impl(JSContext* cx, const CallArgs& args)
{
    WeakCellObject* cell = &args.thisv().toObject().as<WeakCellObject>();
    args.rval().set(cell->getFixedSlot(WeakCellSlot_holdings));
    return true;
}

static bool
WeakCell_holdings(JSContext* cx, unsigned argc, Value* vp)
{
    CallArgs args = CallArgsFromVp(argc, vp);
    return CallNonGenericMethod<Is<WeakCellObject>, WeakCell_holdings_impl>(cx, args);
}

MOZ_ALWAYS_INLINE bool
WeakCell_drop_impl(JSContext* cx, const CallArgs& args)
{
    MOZ_ASSERT(Is<WeakCellObject>(args.thisv()));

    args.rval().setUndefined();
    return true;
}

static bool
WeakCell_drop(JSContext* cx, unsigned argc, Value* vp)
{
    CallArgs args = CallArgsFromVp(argc, vp);
    return CallNonGenericMethod<Is<WeakCellObject>, WeakCell_drop_impl>(cx, args);
}

const JSPropertySpec WeakCellObject::properties[] = {
    JS_PSG("holdings", WeakCell_holdings, 0),
    JS_PS_END
};

const JSFunctionSpec WeakCellObject::methods[] = {
    JS_FN("drop",   WeakCell_drop,  0, 0),
    JS_FS_END
};

static const ClassSpec WeakCellObjectClassSpec = {
    GenericCreateConstructor<WeakCellObject_construct, 1, gc::AllocKind::FUNCTION>,
    GenericCreatePrototype,
    nullptr,
    nullptr,
    WeakCellObject::methods,
    WeakCellObject::properties,
    nullptr,
    ClassSpec::DontDefineConstructor
};

const Class WeakCellObject::class_ = {
    "WeakCell",
    JSCLASS_HAS_RESERVED_SLOTS(WeakCellSlots) |
    JSCLASS_HAS_CACHED_PROTO(JSProto_WeakCell),
    JS_NULL_CLASS_OPS,
    &WeakCellObjectClassSpec
};

const Class WeakCellObject::protoClass_ = {
    "object",
    JSCLASS_HAS_CACHED_PROTO(JSProto_WeakCell),
    JS_NULL_CLASS_OPS,
    &WeakCellObjectClassSpec
};

static JSObject*
CreateWeakRefPrototype(JSContext* cx, JSProtoKey key)
{
    RootedObject cellProto(cx, GlobalObject::getOrCreatePrototype(cx, JSProto_WeakCell));
    if (!cellProto)
        return nullptr;

    return GlobalObject::createBlankPrototypeInheriting(cx, cx->global(),
                                                        &WeakRefObject::protoClass_, cellProto);
}

/* static*/
MOZ_MUST_USE WeakRefObject*
WeakRefObject::create(JSContext* cx, HandleValue holdings, Handle<WeakFactoryObject*> factory,
                      HandleObject proto /* = nullptr */)
{
    return CreateWeakCellOrRef<WeakRefObject>(cx, holdings, factory, proto);
}

static MOZ_MUST_USE bool
WeakRefObject_construct(JSContext* cx, unsigned argc, Value* vp)
{
    JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr, JSMSG_NO_CONSTRUCTOR, "WeakRef");
    return false;
}

MOZ_ALWAYS_INLINE bool
WeakRef_deref_impl(JSContext* cx, const CallArgs& args)
{
    MOZ_ASSERT(Is<WeakRefObject>(args.thisv()));

    WeakRefObject* ref = &args.thisv().toObject().as<WeakRefObject>();
    args.rval().setObjectOrNull(GetCellTarget(ref));

    return true;
}

static bool
WeakRef_deref(JSContext* cx, unsigned argc, Value* vp)
{
    CallArgs args = CallArgsFromVp(argc, vp);
    return CallNonGenericMethod<Is<WeakRefObject>, WeakRef_deref_impl>(cx, args);
}

const JSPropertySpec WeakRefObject::properties[] = {
    JS_PS_END
};

const JSFunctionSpec WeakRefObject::methods[] = {
    JS_FN("deref",  WeakRef_deref,  0, 0),
    JS_FS_END
};

static const ClassSpec WeakRefObjectClassSpec = {
    GenericCreateConstructor<WeakRefObject_construct, 1, gc::AllocKind::FUNCTION>,
    CreateWeakRefPrototype,
    nullptr,
    nullptr,
    WeakRefObject::methods,
    WeakRefObject::properties,
    nullptr,
    ClassSpec::DontDefineConstructor
};

const Class WeakRefObject::class_ = {
    "WeakRef",
    JSCLASS_HAS_RESERVED_SLOTS(WeakCellSlots) |
    JSCLASS_HAS_CACHED_PROTO(JSProto_WeakRef),
    JS_NULL_CLASS_OPS,
    &WeakRefObjectClassSpec
};

const Class WeakRefObject::protoClass_ = {
    "object",
    JSCLASS_HAS_CACHED_PROTO(JSProto_WeakRef),
    JS_NULL_CLASS_OPS,
    &WeakRefObjectClassSpec
};
