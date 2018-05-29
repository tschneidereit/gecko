/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=8 sts=4 et sw=4 tw=99:
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef builtin_WeakRefObject_h
#define builtin_WeakRefObject_h

#include "vm/NativeObject.h"

enum WeakFactorySlots {
    WeakFactorySlot_cleanupCallback,
    WeakFactorySlot_cells,
    WeakFactorySlots
};

enum WeakCellSlots {
    WeakCellSlot_factory,
    WeakCellSlot_holdings,
    WeakCellSlots
};

namespace js {

class WeakCellObject;

class WeakFactoryObject : public NativeObject
{
  public:
    static const unsigned RESERVED_SLOTS = WeakFactorySlots;
    static const Class class_;
    static const Class protoClass_;

    static const JSPropertySpec properties[];
    static const JSFunctionSpec methods[];

    static MOZ_MUST_USE WeakFactoryObject* create(JSContext* cx, HandleObject proto = nullptr);

    JSObject* cellTarget(WeakCellObject* cell);
};

class WeakCellObject : public NativeObject
{
  public:
    static const unsigned RESERVED_SLOTS = WeakCellSlots;
    static const Class class_;
    static const Class protoClass_;

    static const JSPropertySpec properties[];
    static const JSFunctionSpec methods[];

    static MOZ_MUST_USE WeakCellObject* create(JSContext* cx, HandleValue holdings,
                                               Handle<WeakFactoryObject*> factory,
                                               HandleObject proto = nullptr);
};

class WeakRefObject : public WeakCellObject
{
  public:
    static const unsigned RESERVED_SLOTS = WeakCellSlots;
    static const Class class_;
    static const Class protoClass_;

    static const JSPropertySpec properties[];
    static const JSFunctionSpec methods[];

    static MOZ_MUST_USE WeakRefObject* create(JSContext* cx, HandleValue holdings,
                                              Handle<WeakFactoryObject*> factory,
                                              HandleObject proto = nullptr);
};

} // namespace js

// template<>
// inline bool
// JSObject::is<js::WeakCellObject>() const
// {
//     return is<js::WeakMapObject>() || is<js::WeakRefObject>();
// }

#endif /* builtin_WeakRefObject_h */
