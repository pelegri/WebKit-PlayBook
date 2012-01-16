/*
 * Copyright 2008, The Android Open Source Project
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE COMPUTER, INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef TouchEvent_h
#define TouchEvent_h

#if ENABLE(TOUCH_EVENTS)

#include "MouseRelatedEvent.h"
#include "TouchList.h"

namespace WebCore {

class TouchEvent : public MouseRelatedEvent {
public:
    virtual ~TouchEvent();

    static PassRefPtr<TouchEvent> create()
    {
        return adoptRef(new TouchEvent);
    }
    static PassRefPtr<TouchEvent> create(TouchList* touches, 
            TouchList* targetTouches, TouchList* changedTouches, 
            const AtomicString& type, PassRefPtr<AbstractView> view,
            int screenX, int screenY, int pageX, int pageY,
            bool ctrlKey, bool altKey, bool shiftKey, bool metaKey,
            float scale = 1.0, float rotation = 0.0)
    {
        return adoptRef(new TouchEvent(touches, targetTouches, changedTouches,
                type, view, screenX, screenY, pageX, pageY,
                ctrlKey, altKey, shiftKey, metaKey, scale, rotation));
    }

    void initTouchEvent(TouchList* touches, TouchList* targetTouches,
            TouchList* changedTouches, const AtomicString& type, 
            PassRefPtr<AbstractView> view, int screenX, int screenY, 
            int clientX, int clientY,
            bool ctrlKey, bool altKey, bool shiftKey, bool metaKey,
            float scale = 1.0, float rotation = 0.0);

    TouchList* touches() const { return m_touches.get(); }
    TouchList* targetTouches() const { return m_targetTouches.get(); }
    TouchList* changedTouches() const { return m_changedTouches.get(); }
    float rotation() const { return m_rotation; }
    float scale() const { return m_scale; }
#if PLATFORM(BLACKBERRY) && OS(QNX)
    void setDoubleTap(bool doubleTap) { m_doubleTap = doubleTap; }
    bool isDoubleTap() const { return m_doubleTap; }
    void setTouchHold(bool touchHold) { m_touchHold = touchHold; }
    bool isTouchHold() const { return m_touchHold; }
#endif

private:
    TouchEvent();
    TouchEvent(TouchList* touches, TouchList* targetTouches,
            TouchList* changedTouches, const AtomicString& type,
            PassRefPtr<AbstractView>, int screenX, int screenY, int pageX,
            int pageY,
            bool ctrlKey, bool altKey, bool shiftKey, bool metaKey,
            float scale = 1.0, float rotation = 0.0);

    virtual bool isTouchEvent() const { return true; }

    RefPtr<TouchList> m_touches;
    RefPtr<TouchList> m_targetTouches;
    RefPtr<TouchList> m_changedTouches;
    float m_scale;
    float m_rotation;
#if PLATFORM(BLACKBERRY) && OS(QNX)
    bool m_touchHold;
    bool m_doubleTap;
#endif
};

} // namespace WebCore

#endif // ENABLE(TOUCH_EVENTS)

#endif // TouchEvent_h