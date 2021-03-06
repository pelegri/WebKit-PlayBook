/*
 * Copyright (C) Research In Motion Limited 2009-2011. All rights reserved.
 */

#include "config.h"
#include "InputHandler.h"

#include "CSSStyleDeclaration.h"
#include "CString.h"
#include "Chrome.h"
#include "Document.h"
#include "DocumentLoader.h"
#include "FocusController.h"
#include "Frame.h"
#include "FrameView.h"
#include "HTMLInputElement.h"
#include "HTMLNames.h"
#include "HTMLOptGroupElement.h"
#include "HTMLOptionElement.h"
#include "HTMLSelectElement.h"
#include "HTMLTextAreaElement.h"
#include "NotImplemented.h"
#include "BlackBerryPlatformKeyboardCodes.h"
#include "BlackBerryPlatformKeyboardEvent.h"
#include "BlackBerryPlatformMisc.h"
#include "BlackBerryPlatformSettings.h"
#include "Page.h"
#include "PlatformKeyboardEvent.h"
#include "PluginView.h"
#include "Range.h"
#include "RenderLayer.h"
#include "RenderMenuList.h"
#include "RenderPart.h"
#include "RenderText.h"
#include "RenderTextControl.h"
#include "RenderWidget.h"
#include "ScopePointer.h"
#include "SelectionHandler.h"
#include "TextIterator.h"
#include "WebPageClient.h"
#include "WebPage_p.h"
#include "WebSettings.h"

#define SHOWDEBUG_INPUTHANDLER 0
#define ENABLE_DEBUG_UNDOREDO 0
#define SHOWDEBUG_SYNCHANDLING 0

using namespace Olympia::Platform;
using namespace WebCore;

namespace Olympia {
namespace WebKit {

class ProcessingChangeGuard
{
public:
    ProcessingChangeGuard(InputHandler* inputHandler)
        : m_inputHandler(inputHandler)
        , m_savedProcessingChange(false)
    {
        ASSERT(m_inputHandler);

        m_savedProcessingChange = m_inputHandler->processingChange();
        m_inputHandler->setProcessingChange(true);
    }

    ~ProcessingChangeGuard()
    {
        m_inputHandler->setProcessingChange(m_savedProcessingChange);
    }

private:
    InputHandler* m_inputHandler;
    bool m_savedProcessingChange;
};

static Element* elementFromId(unsigned int id)
{
    return reinterpret_cast<Element*>(id);
}

static unsigned int idFromElement(const Element* element)
{
    return reinterpret_cast<unsigned int>(element);
}

static Frame* frameFromId(unsigned int id)
{
    return reinterpret_cast<Frame*>(id);
}

static unsigned int idFromFrame(Frame* frame)
{
    return reinterpret_cast<unsigned int>(frame);
}

InputHandler::InputHandler(WebPagePrivate* page)
    : m_webPage(page)
    , m_currentFocusElement(0)
    , m_processingChange(false)
    , m_navigationMode(false)
    , m_currentFocusElementType(TextEdit)
#if OS(BLACKBERRY)
    , m_numberOfOutstandingSyncMessages(0)
    , m_syncWatchdogTimer(new Timer<InputHandler>(this, &InputHandler::syncWatchdogFired))
#endif
{
}

InputHandler::~InputHandler()
{
#if OS(BLACKBERRY)
    delete m_syncWatchdogTimer;
    m_syncWatchdogTimer = 0;
#endif
}

#if OS(BLACKBERRY)
void InputHandler::syncWatchdogFired(WebCore::Timer<InputHandler>*)
{
    // Reset sync tracking.
    if (m_numberOfOutstandingSyncMessages && isActiveTextEdit()) {
#if SHOWDEBUG_SYNCHANDLING
    Olympia::Platform::log(Olympia::Platform::LogLevelCritical, "InputHandler::syncWatchdogFired Resetting");
#endif
        m_numberOfOutstandingSyncMessages = 0;
        // notifyTextChange is a sync message and will re-increment the value continuing
        // to prevent input until client responds.
        notifyTextChanged(m_currentFocusElement.get());
    }
}

void InputHandler::syncAckReceived()
{
    if (!m_numberOfOutstandingSyncMessages) {
#if SHOWDEBUG_SYNCHANDLING
        Olympia::Platform::log(Olympia::Platform::LogLevelCritical, "InputHandler::syncAckReceived unexpected");

        // An unexpected ack was received.  Sender should be investigated.
        ASSERT_NOT_REACHED();
#endif
        return;
    }
    m_numberOfOutstandingSyncMessages--;
#if SHOWDEBUG_SYNCHANDLING
    Olympia::Platform::log(Olympia::Platform::LogLevelCritical, "InputHandler::syncAckReceived current m_numberOfOutstandingSyncMessages = %d", m_numberOfOutstandingSyncMessages);
#endif
    if (!m_numberOfOutstandingSyncMessages)
        m_syncWatchdogTimer->stop();
}

void InputHandler::syncMessageSent()
{
    m_numberOfOutstandingSyncMessages++;
    m_syncWatchdogTimer->startOneShot(1.0); // Timeout after 1 second if no response is received.
}
#endif

bool InputHandler::allowSyncInput()
{
#if OS(BLACKBERRY)
    return !m_numberOfOutstandingSyncMessages;
#else
    return true;
#endif
}

static OlympiaInputType convertInputType(HTMLInputElement::DeprecatedInputType type)
{
    switch (type) {
    case HTMLInputElement::PASSWORD:
        return InputTypePassword;
    case HTMLInputElement::ISINDEX:
        return InputTypeIsIndex;
    case HTMLInputElement::SEARCH:
        return InputTypeSearch;
    case HTMLInputElement::EMAIL:
        return InputTypeEmail;
    case HTMLInputElement::NUMBER:
        return InputTypeNumber;
    case HTMLInputElement::TELEPHONE:
        return InputTypeTelephone;
    case HTMLInputElement::URL:
        return InputTypeURL;
    case HTMLInputElement::COLOR:
        return InputTypeColor;
    case HTMLInputElement::DATE:
        return InputTypeDate;
    case HTMLInputElement::DATETIME:
        return InputTypeDateTime;
    case HTMLInputElement::DATETIMELOCAL:
        return InputTypeDateTimeLocal;
    case HTMLInputElement::MONTH:
        return InputTypeMonth;
    case HTMLInputElement::TIME:
        return InputTypeTime;
    case HTMLInputElement::WEEK:
        // FIXME: missing WEEK popup selector
    case HTMLInputElement::TEXT:
    default:
        return InputTypeText;
    }
}

WTF::String InputHandler::elementText(Element* element)
{
    // Note:  requesting the value from some input fields can result in an update
    // when it updates it's internal value from the renderer.
    ProcessingChangeGuard guard(this);
    WTF::String elementText;
    if (element->hasTagName(HTMLNames::inputTag)) {
        const HTMLInputElement* inputElement = static_cast<const HTMLInputElement*>(element);
        elementText = inputElement->value();
    } else if (element->hasTagName(HTMLNames::textareaTag)) {
        const HTMLTextAreaElement* inputElement = static_cast<const HTMLTextAreaElement*>(element);
        elementText = inputElement->value();
    } else {
        RefPtr<Range> rangeForNode = rangeOfContents(element);
        elementText = rangeForNode.get()->text();
    }
    return elementText;
}

OlympiaInputType InputHandler::elementType(const Element* element)
{
    if (element->hasTagName(HTMLNames::inputTag)) {
        const HTMLInputElement* inputElement = static_cast<const HTMLInputElement*>(element);
        return convertInputType(inputElement->deprecatedInputType());
    }
    if (element->hasTagName(HTMLNames::textareaTag))
        return InputTypeTextArea;

    // Default to InputTypeTextArea for content editable fields.
    return InputTypeTextArea;
}

bool InputHandler::isElementTypePlugin(const WebCore::Element* element)
{
    if (!element)
        return false;

    if (element->hasTagName(HTMLNames::objectTag)
        || element->hasTagName(HTMLNames::embedTag)
        || element->hasTagName(HTMLNames::appletTag))
        return true;

    return false;
}

static inline HTMLTextFormControlElement* toTextControlElement(WebCore::Node* node)
{
    if (!(node && node->isElementNode()))
        return 0;

    Element* element = static_cast<Element*>(node);
    if (!element->isFormControlElement())
        return 0;

    HTMLFormControlElement* formElement = static_cast<HTMLFormControlElement*>(element);
    if (!formElement->isTextFormControl())
        return 0;

    return static_cast<HTMLTextFormControlElement*>(formElement);
}

// Check if this is an input field that will be focused & require input support.
static bool isTextBasedContentEditableElement(const Element* element)
{
    if (!element)
        return false;

    if (element->isReadOnlyFormControl())
        return false;

    return element->isTextFormControl() || element->isContentEditable();
}

void InputHandler::nodeFocused(Node* node)
{
    if (isActiveTextEdit() && m_currentFocusElement == node) {
        setNavigationMode(true);
        return;
    }

    if (node && node->isElementNode()) {
        Element* element = static_cast<Element*>(node);
        if (isElementTypePlugin(element)) {
            setPluginFocused(element);
            return;
        }
        if (isTextBasedContentEditableElement(element)) {
            // Focused node is a text based input field, textarea or content editable field.
            setElementFocused(element);
            return;
        }
    }

    if (isActiveTextEdit() && m_currentFocusElement->isContentEditable()) {
        // This is a special handler for content editable fields.  The focus node is the top most
        // field that is content editable, however, by enabling / disabling designmode and
        // content editable, it is possible through javascript or selection to trigger the focus node to
        // change even while maintaining editing on the same selection point.  If the focus element
        // isn't contentEditable, but the current selection is, don't send a change notification.

        // When processing changes selection changes occur that may reset the focus element
        // and could potentially cause a crash as m_currentFocusElement should not be
        // changed during processing of an EditorCommand.
        if (processingChange())
            return;

        Frame* frame = m_currentFocusElement->document()->frame();
        ASSERT(frame);

        // Test the current selection to make sure that the content in focus is still content
        // editable.  This may mean Javascript triggered a focus change by modifying the
        // top level parent of this object's content editable state without actually modifying
        // this particular object.
        // Example site: html5demos.com/contentEditable - blur event triggers focus change.
        if (frame == m_webPage->focusedOrMainFrame() && frame->selection()->start().anchorNode()
            && frame->selection()->start().anchorNode()->isContentEditable())
                return;
    }

    // No valid focus element found for handling.
    setElementUnfocused();
}

void InputHandler::setPluginFocused(Element* element)
{
    ASSERT(isElementTypePlugin(element));

    if (isActiveTextEdit())
        setElementUnfocused();

    m_currentFocusElementType = Plugin;
    m_currentFocusElement = element;
}

void InputHandler::setElementUnfocused(bool refocusOccuring)
{
    if (isActiveTextEdit()) {
        unsigned int oldFrameId = m_currentFocusElement->document() ? idFromFrame(m_currentFocusElement->document()->frame()) : 0;
        unsigned int oldFocusId = idFromElement(m_currentFocusElement.get());

#if SHOWDEBUG_INPUTHANDLER
        Olympia::Platform::log(Olympia::Platform::LogLevelCritical, "InputHandler::setElementFocused unfocusing frameId=%u, FocusId=%u", oldFrameId, oldFocusId);
#endif

#if OS(BLACKBERRY)
        if (elementType(m_currentFocusElement.get()) == InputTypePassword)
            applyPasswordFieldUnmaskSecure(m_currentFocusElement.get(), 0, 0);

        if (oldFrameId)
            removeAttributedTextMarker();

        m_numberOfOutstandingSyncMessages = 0;
        m_syncWatchdogTimer->stop();
#endif

        // Send change notifications.  Only cancel navigation mode if we are not
        // refocusing to avoid flashing the keyboard when switching between two
        // input fields.
        setNavigationMode(false, !refocusOccuring);
        m_webPage->m_client->inputFocusLost(oldFrameId, oldFocusId);
    }

    // Clear the node details.
    m_currentFocusElement = 0;
    m_currentFocusElementType = TextEdit;
}

void InputHandler::setElementFocused(Element* element)
{
    bool elementIsTextBasedInput = isTextBasedContentEditableElement(element);

    // Clear the existing focus node details.
    setElementUnfocused(elementIsTextBasedInput);

    if (!elementIsTextBasedInput)
        return;

    ASSERT(element->document() && element->document()->frame());

    // Mark this element as active and add to frame set.
    m_currentFocusElement = element;
    m_currentFocusElementType = TextEdit;
    addElementToFrameSet(element);

    // Send details to the client about this element.
    OlympiaInputType type = elementType(element);
    unsigned int currentFrameId = idFromFrame(element->document()->frame());
    unsigned int currentFocusId = idFromElement(element);
    unsigned int length = elementText(element).length();

    // Always send selection as zero since cursor placement doesn't occur until after
    // focus is given.
    unsigned int selectionStart = 0;
    unsigned int selectionEnd = 0;

#if SHOWDEBUG_INPUTHANDLER
    Olympia::Platform::log(Olympia::Platform::LogLevelCritical, "InputHandler::setElementFocused frameId=%u, FocusId=%u, Type=%d, length=%u, selectionStart=%u, selectionEnd=%u",
                currentFrameId, currentFocusId, type, length, selectionStart, selectionEnd);
#endif
    m_webPage->m_client->inputFocusGained(currentFrameId, currentFocusId, type, length, selectionStart, selectionEnd);

    // Send navigation mode active to the client if it is a mouse press, or the page is not currently loading.
    bool sendNavigationMode = Olympia::Platform::Settings::get()->alwaysShowKeyboardOnFocus()
                              || m_webPage->m_mainFrame->eventHandler()->mousePressed()
                              || (m_webPage->m_mainFrame->loader() ? !m_webPage->m_mainFrame->loader()->isLoading() : false);
    setNavigationMode(true, sendNavigationMode);

    handleInputLocaleChanged(m_webPage->m_webSettings->isWritingDirectionRTL());
}

void InputHandler::addElementToFrameSet(Element* element)
{
    ASSERT(element);
    ASSERT(element->document() && element->document()->frame());

    Frame* frame = element->document()->frame();
    // FIXME: is this slow?
    if (!m_frameElements.contains(frame)) {
        ElementHash hashSet;
        hashSet.add(element);
        m_frameElements.set(frame, hashSet);
    } else if (!m_frameElements.get(frame).contains(element)) {
        ElementHash hashSet = m_frameElements.get(frame);
        hashSet.add(element);
        m_frameElements.set(frame, hashSet);
    }
}

bool InputHandler::validElement(Frame* frame, const Element* element)
{
    ASSERT(frame);
    ASSERT(element);
    return (m_frameElements.contains(frame) && m_frameElements.get(frame).contains(const_cast<Element*>(element)));
}

static bool isTypeDateTime(OlympiaInputType type)
{
    return type == InputTypeDate || type == InputTypeDateTime  || type == InputTypeDateTimeLocal
            || type == InputTypeMonth  || type == InputTypeTime;
}

bool InputHandler::openDatePopup(HTMLInputElement* element, OlympiaInputType type)
{
    if (!element || element->disabled() || !isTypeDateTime(type))
        return false;

    if (isActiveTextEdit())
        clearCurrentFocusElement();

    m_currentFocusElement = element;
    m_currentFocusElementType = TextPopup;

    WTF::String value = element->value();
    WTF::String min = element->getAttribute(HTMLNames::minAttr).string();
    WTF::String max = element->getAttribute(HTMLNames::maxAttr).string();
    double step = element->getAttribute(HTMLNames::stepAttr).toDouble();
    m_webPage->m_client->openDateTimePopup(type, value, min, max, step);
    return true;
}

bool InputHandler::openColorPopup(HTMLInputElement* element)
{
    if (!element || element->disabled())
        return false;

    if (isActiveTextEdit())
        clearCurrentFocusElement();

    m_currentFocusElement = element;
    m_currentFocusElementType = TextPopup;

    m_webPage->m_client->openColorPopup(element->value());
    return true;
}

void InputHandler::setInputValue(const WTF::String& value)
{
    if (!isActiveTextPopup())
        return;

    HTMLInputElement* inputElement = static_cast<HTMLInputElement*>(m_currentFocusElement.get());
    inputElement->setValue(value);
    clearCurrentFocusElement();
}

void InputHandler::nodeTextChanged(const Node* node)
{
#if OS(BLACKBERRY)
    if (processingChange() || !node)
        return;

    if (node->isElementNode())
        notifyTextChanged(static_cast<const Element*>(node));
#endif
}

void InputHandler::ensureFocusTextElementVisible(CaretScrollType scrollType)
{
    if (!m_currentFocusElement || !m_navigationMode || !m_currentFocusElement->document())
        return;

    Frame* elementFrame = m_currentFocusElement->document()->frame();
    if (!elementFrame)
        return;

    Frame* mainFrame = m_webPage->mainFrame();
    if (!mainFrame)
        return;

    FrameView* mainFrameView = mainFrame->view();
    if (!mainFrameView)
        return;

    WebCore::IntRect selectionFocusRect;
    switch (elementFrame->selection()->selectionType()) {
        case VisibleSelection::CaretSelection:
            selectionFocusRect = elementFrame->selection()->absoluteCaretBounds();
            break;
        case VisibleSelection::RangeSelection:
            selectionFocusRect = VisiblePosition(elementFrame->selection()->start()).absoluteCaretBounds();
            break;
        case VisibleSelection::NoSelection:
        default:
            return;
    }

    int fontHeight = selectionFocusRect.height();

    if (elementFrame != mainFrame) {  // Element is in a subframe.
        // Remove any scroll offset within the subframe to get the point relative to the main frame.
        selectionFocusRect.move(-elementFrame->view()->scrollPosition().x(), -elementFrame->view()->scrollPosition().y());

        // Adjust the selection rect based on the frame offset in relation to the main frame if it's a subframe.
        if (elementFrame->ownerRenderer()) {
            WebCore::IntPoint frameOffset = elementFrame->ownerRenderer()->absoluteContentBox().location();
            selectionFocusRect.move(frameOffset.x(), frameOffset.y());
        }
    }

    WebCore::IntRect actualScreenRect = WebCore::IntRect(mainFrameView->scrollPosition(), m_webPage->actualVisibleSize());
    Position start = elementFrame->selection()->start();
    if (start.node() && start.node()->renderer()) {
        if (RenderLayer* layer = start.node()->renderer()->enclosingLayer()) {
            ScrollAlignment horizontalScrollAlignment = ScrollAlignment::alignToEdgeIfNeeded;
            ScrollAlignment verticalScrollAlignment = ScrollAlignment::alignToEdgeIfNeeded;

            if (scrollType != EdgeIfNeeded) {
                // Align the selection rect if possible so that we show the field's
                // outline if the caret is at the edge of the field
                if (RenderObject* focusedRenderer = m_currentFocusElement->renderer()) {
                    WebCore::IntRect nodeOutlineBounds = focusedRenderer->absoluteOutlineBounds();
                    WebCore::IntRect caretAtEdgeRect = rectForCaret(0);
                    int paddingX = abs(caretAtEdgeRect.x() - nodeOutlineBounds.x());
                    int paddingY = abs(caretAtEdgeRect.y() - nodeOutlineBounds.y());

                    if (selectionFocusRect.x() - paddingX == nodeOutlineBounds.x())
                        selectionFocusRect.setX(nodeOutlineBounds.x());
                    else if (selectionFocusRect.right() + paddingX == nodeOutlineBounds.right())
                        selectionFocusRect.setRight(nodeOutlineBounds.right());
                    if (selectionFocusRect.y() - paddingY == nodeOutlineBounds.y())
                        selectionFocusRect.setY(nodeOutlineBounds.y());
                    else if (selectionFocusRect.bottom() + paddingY == nodeOutlineBounds.bottom())
                        selectionFocusRect.setBottom(nodeOutlineBounds.bottom());

                    // If the editing point is on the left hand side of the screen when the node's
                    // rect is edge aligned, edge align the node rect.
                    if (selectionFocusRect.x() - caretAtEdgeRect.x() < actualScreenRect.width() / 2)
                        selectionFocusRect.setX(nodeOutlineBounds.x());
                    else
                        horizontalScrollAlignment = ScrollAlignment::alignCenterIfNeeded;

                }
                verticalScrollAlignment = (scrollType == CenterAlways) ? ScrollAlignment::alignCenterAlways : ScrollAlignment::alignCenterIfNeeded;
            }

            WebCore::IntRect revealRect = layer->getRectToExpose(actualScreenRect, selectionFocusRect,
                                                                 horizontalScrollAlignment,
                                                                 verticalScrollAlignment);

            mainFrameView->setScrollPosition(revealRect.location());
        }
    }

    // If the text is too small, zoom in to make it a minimum size.
    static const int s_minimumTextHeightInPixels = 6;
    if (fontHeight && fontHeight < s_minimumTextHeightInPixels)
        m_webPage->zoomAboutPoint(s_minimumTextHeightInPixels / fontHeight, m_webPage->centerOfVisibleContentsRect());
}

void InputHandler::ensureFocusPluginElementVisible()
{
    if (!isActivePlugin() || !m_currentFocusElement->document())
        return;

    Frame* elementFrame = m_currentFocusElement->document()->frame();
    if (!elementFrame)
        return;

    Frame* mainFrame = m_webPage->mainFrame();
    if (!mainFrame)
        return;

    FrameView* mainFrameView = mainFrame->view();
    if (!mainFrameView)
        return;

    WebCore::IntRect selectionFocusRect;

    RenderWidget* renderWidget = static_cast<RenderWidget*>(m_currentFocusElement->renderer());
    if (renderWidget) {
        PluginView* pluginView = static_cast<PluginView*>(renderWidget->widget());

        if (pluginView)
            selectionFocusRect = pluginView->ensureVisibleRect();
    }

    if (selectionFocusRect.isEmpty())
        return;

    //TODO: We may need to scroll the subframe (recursively) in the future. Revisit this...
    if (elementFrame != mainFrame) {  // Element is in a subframe.
        // Remove any scroll offset within the subframe to get the point relative to the main frame.
        selectionFocusRect.move(-elementFrame->view()->scrollPosition().x(), -elementFrame->view()->scrollPosition().y());

        // Adjust the selection rect based on the frame offset in relation to the main frame if it's a subframe.
        if (elementFrame->ownerRenderer()) {
            WebCore::IntPoint frameOffset = elementFrame->ownerRenderer()->absoluteContentBox().location();
            selectionFocusRect.move(frameOffset.x(), frameOffset.y());
        }
    }

    WebCore::IntRect actualScreenRect = WebCore::IntRect(mainFrameView->scrollPosition(), m_webPage->actualVisibleSize());
    if (actualScreenRect.contains(selectionFocusRect))
        return;

    // Calculate a point such that the center of the requested rectangle
    // is at the center of the screen. TODO: If the element was partially on screen
    // we might want to just bring the offscreen portion into view, someone needs
    // to decide if that's the behavior we want or not.k
    WebCore::IntPoint pos(selectionFocusRect.center().x() - actualScreenRect.width() / 2,
                 selectionFocusRect.center().y() - actualScreenRect.height() / 2);

    mainFrameView->setScrollPosition(pos);
}

void InputHandler::ensureFocusElementVisible(bool centerInView)
{
    if (isActivePlugin())
        ensureFocusPluginElementVisible();
    else
        ensureFocusTextElementVisible(centerInView ? CenterAlways : CenterIfNeeded);
}

#if OS(BLACKBERRY)
void InputHandler::notifyTextChanged(const Element* element)
{
    if (!element)
        return;

    ASSERT(element->document());
    Frame* frame = element->document()->frame();

    // If the function is called by timer, the document may have been detached from the frame.
    if (!frame)
        return;

    if (validElement(frame, element)) {
        unsigned int focusId = idFromElement(element);
        unsigned int currentFrameId = idFromFrame(frame);
        Element* activeElement = const_cast<Element*>(element);
        WTF::String currentText = elementText(activeElement);

        int selectionStartOffset = -1;
        int selectionEndOffset = -1;
        // Fixme.  Selection might work out of focus?
        if (activeElement == m_currentFocusElement) {
            selectionStartOffset = selectionStart();
            selectionEndOffset = selectionStart();
        }

#if SHOWDEBUG_INPUTHANDLER
        Olympia::Platform::log(Olympia::Platform::LogLevelCritical, "InputHandler::notifyTextChanged frameId=%u, FocusId=%u, currentText=%s, length=%u, selectionStart=%u, selectionEnd=%u",
                    currentFrameId, focusId, currentText.utf8().data(), currentText.length(), selectionStartOffset, selectionEndOffset);
#endif
        m_webPage->m_client->inputTextChanged(currentFrameId, focusId, currentText.characters(), currentText.length(), selectionStartOffset, selectionEndOffset);
#if SHOWDEBUG_SYNCHANDLING
        Olympia::Platform::log(Olympia::Platform::LogLevelCritical, "InputHandler::notifyTextChanged syncMessage sent current m_numberOfOutstandingSyncMessages = %d", m_numberOfOutstandingSyncMessages);
#endif
        syncMessageSent();
    }
}
#endif

void InputHandler::requestElementText(int requestedFrameId, int requestedElementId, int offset, int length)
{
#if OS(BLACKBERRY)
    // Offset -1 means this is an ack notification of a change.
    if (offset == -1) {
        if (isActiveTextEdit() && m_currentFocusElement == elementFromId(requestedElementId))
            syncAckReceived();
        return;
    }

    if (validElement(frameFromId(requestedFrameId), elementFromId(requestedElementId))) {
        Element* element = elementFromId(requestedElementId);

        ASSERT(element && element->document());
        if (offset < 0)
            offset = 0;

        WTF::String currentText = elementText(element);
        if (length == -1 || length > (currentText.length() - offset))
            length = currentText.length() - offset;
        currentText = currentText.substring(offset, length);

        int selectionStartOffset = -1;
        int selectionEndOffset = -1;
        if (element == m_currentFocusElement) {
            selectionStartOffset = selectionStart();
            selectionEndOffset = selectionStart();
        }

#if SHOWDEBUG_INPUTHANDLER
        Olympia::Platform::log(Olympia::Platform::LogLevelCritical, "InputHandler::requestElementText frameId=%u, FocusId=%u, currentText=%s, length=%u, selectionStart=%u, selectionEnd=%u",
                    requestedFrameId, requestedElementId, currentText.utf8().data(), currentText.length(), selectionStartOffset, selectionEndOffset);
#endif
        m_webPage->m_client->inputTextForElement(requestedFrameId, requestedElementId, offset, length, selectionStartOffset, selectionEndOffset, currentText.characters(), length);
#if SHOWDEBUG_SYNCHANDLING
        Olympia::Platform::log(Olympia::Platform::LogLevelCritical, "InputHandler::requestElementText syncMessage sent current m_numberOfOutstandingSyncMessages = %d", m_numberOfOutstandingSyncMessages);
#endif
        syncMessageSent();
    } else {
        // Invalid Request.
        length = -1;
#if SHOWDEBUG_INPUTHANDLER
        Olympia::Platform::log(Olympia::Platform::LogLevelCritical, "InputHandler::requestElementText frameId=%u, FocusId=%u, invalid element", requestedFrameId, requestedElementId);
#endif
        m_webPage->m_client->inputTextForElement(requestedFrameId, requestedElementId, offset, length, -1, -1, WTF::String().characters(), 0);
#if SHOWDEBUG_SYNCHANDLING
        Olympia::Platform::log(Olympia::Platform::LogLevelCritical, "InputHandler::requestElementText syncMessage sent current m_numberOfOutstandingSyncMessages = %d", m_numberOfOutstandingSyncMessages);
#endif
        syncMessageSent();
    }
#endif
}

void InputHandler::cleanFrameSet(Frame* frame)
{
    m_frameElements.remove(frame);
}

void InputHandler::frameUnloaded(Frame *frame)
{
#if OS(BLACKBERRY) || SHOWDEBUG_INPUTHANDLER
    unsigned int currentFrameId = idFromFrame(frame);
#endif
    cleanFrameSet(frame);

#if OS(BLACKBERRY)
    m_webPage->m_client->inputFrameCleared(currentFrameId);
#endif

    if (isActiveTextEdit()) {
#if OS(BLACKBERRY)
        setNavigationMode(false, false);
#else
        setNavigationMode(false);
#endif

        // Clear the node details.
        m_currentFocusElement = 0;
        m_currentFocusElementType = TextEdit;
    }

#if SHOWDEBUG_INPUTHANDLER
    Olympia::Platform::log(Olympia::Platform::LogLevelCritical, "InputHandler::frameUnloaded frameId=%u", currentFrameId);
#endif
}

void InputHandler::setNavigationMode(bool active, bool sendMessage)
{
    if (active && !isActiveTextEdit()) {
        if (!m_navigationMode)
            return;

        // We can't be active if there is no element.  Send out notification that we
        // aren't in navigation mode.
        active = false;
    }

    // Don't send the change if it's setting the event setting navigationMode true
    // if we are already in navigation mode and this is a navigation move event.
    // We need to send the event when it's triggered by a touch event or mouse
    // event to allow display of the VKB, but do not want to send it when it's
    // triggered by a navigation event as it has no effect.
    // Touch events are simulated as mouse events so mousePressed will be active
    // when it is a re-entry event.
    // See RIM Bugs #369 & #878.
    if (active && active == m_navigationMode && !m_webPage->m_mainFrame->eventHandler()->mousePressed())
        return;

    m_navigationMode = active;

#if SHOWDEBUG_INPUTHANDLER
    Olympia::Platform::log(Olympia::Platform::LogLevelCritical, "InputHandler::setNavigationMode %s, %s", active ? "true" : "false", sendMessage ? "message sent" : "message not sent");
#endif

    if (sendMessage)
        m_webPage->m_client->inputSetNavigationMode(active);
}

bool InputHandler::selectionAtStartOfElement()
{
    if (!isActiveTextEdit())
        return false;

    ASSERT(m_currentFocusElement->document() && m_currentFocusElement->document()->frame());

    if (selectionStart() == 0)
        return true;

    return false;
}

bool InputHandler::selectionAtEndOfElement()
{
    if (!isActiveTextEdit())
        return false;

    ASSERT(m_currentFocusElement->document() && m_currentFocusElement->document()->frame());

    if (selectionStart() == elementText(m_currentFocusElement.get()).length())
        return true;

    return false;
}

unsigned int InputHandler::selectionStart()
{
    if (!m_currentFocusElement->document() || !m_currentFocusElement->document()->frame())
        return 0;

    HTMLTextFormControlElement* controlElement = toTextControlElement(m_currentFocusElement.get());
    if (controlElement)
        return controlElement->selectionStart();

    SelectionController caretSelection;
    caretSelection.setSelection(m_currentFocusElement->document()->frame()->selection()->selection());
    RefPtr<Range> rangeSelection = caretSelection.selection().toNormalizedRange();
    if (rangeSelection) {
        unsigned int selectionStartInNode = rangeSelection->startOffset();
        Node* startContainerNode = rangeSelection->startContainer();

        ExceptionCode ec;
        RefPtr<Range> rangeForNode = rangeOfContents(m_currentFocusElement.get());
        rangeForNode->setEnd(startContainerNode, selectionStartInNode, ec);
        ASSERT(!ec);

        return TextIterator::rangeLength(rangeForNode.get());
    }
    return 0;
}

unsigned int InputHandler::selectionEnd()
{
    if (!m_currentFocusElement->document() || !m_currentFocusElement->document()->frame())
        return 0;

    HTMLTextFormControlElement* controlElement = toTextControlElement(m_currentFocusElement.get());
    if (controlElement)
        return controlElement->selectionEnd();

    SelectionController caretSelection;
    caretSelection.setSelection(m_currentFocusElement->document()->frame()->selection()->selection());
    RefPtr<Range> rangeSelection = caretSelection.selection().toNormalizedRange();
    if (rangeSelection) {
        unsigned int selectionEndInNode = rangeSelection->endOffset();
        Node* endContainerNode = rangeSelection->endContainer();

        ExceptionCode ec;
        RefPtr<Range> rangeForNode = rangeOfContents(m_currentFocusElement.get());
        rangeForNode->setEnd(endContainerNode, selectionEndInNode, ec);
        ASSERT(!ec);

        return TextIterator::rangeLength(rangeForNode.get());
    }
    return 0;
}

void InputHandler::selectionChanged()
{
#if OS(BLACKBERRY)
    if (processingChange() || !isActiveTextEdit())
        return;

    if (!m_currentFocusElement->document() || !m_currentFocusElement->document()->frame())
        return;

    unsigned int currentFrameId = idFromFrame(m_currentFocusElement->document()->frame());
    unsigned int currentFocusId = idFromElement(m_currentFocusElement.get());

    unsigned int selectionStartOffset = selectionStart();
    unsigned int selectionEndOffset = selectionEnd();

#if SHOWDEBUG_INPUTHANDLER
    Olympia::Platform::log(Olympia::Platform::LogLevelCritical, "InputHandler::selectionChanged frameId=%u, FocusId=%u, selectionStart=%u, selectionEnd=%u",
                currentFrameId, currentFocusId, selectionStartOffset, selectionEndOffset);
#endif
    m_webPage->m_client->inputSelectionChanged(currentFrameId, currentFocusId, selectionStartOffset, selectionEndOffset);
#if SHOWDEBUG_SYNCHANDLING
    Olympia::Platform::log(Olympia::Platform::LogLevelCritical, "InputHandler::selectionChanged syncMessage sent current m_numberOfOutstandingSyncMessages = %d", m_numberOfOutstandingSyncMessages);
#endif
    syncMessageSent();
#endif
#if OS(QNX)
    // FIXME:  We probably want this for all platforms so that if the caret is moved offscreen
    // a scroll is forced.
    if (isActiveTextEdit())
        ensureFocusTextElementVisible(EdgeIfNeeded);
#endif
}

RefPtr<Range> InputHandler::subrangeForFocusElement(unsigned int offsetStart, unsigned int offsetLength)
{
    // NOTE:  Function should only be used for non-HTMLTextFormControlElement as rangeOfContents
    // is empty for those elements.
    ASSERT(m_currentFocusElement);

    RefPtr<Range> rangeForNode = rangeOfContents(m_currentFocusElement.get());
    return TextIterator::subrange(rangeForNode.get(), offsetStart, offsetLength);
}

RefPtr<Range> InputHandler::focusElementRange(unsigned int offsetStart, unsigned int offsetLength)
{
    if (!isActiveTextEdit())
        return 0;

    ASSERT(m_currentFocusElement->document() && m_currentFocusElement->document()->frame());

    unsigned int currentTextLength = elementText(m_currentFocusElement.get()).length();
    if (offsetLength < 1 || offsetStart + offsetLength > currentTextLength)
        return 0; // Invalid request.

    HTMLTextFormControlElement* controlElement = toTextControlElement(m_currentFocusElement.get());
    if (controlElement) {
        RenderTextControl* textRender = toRenderTextControl(controlElement->renderer());
        return VisibleSelection(textRender->visiblePositionForIndex(offsetStart), textRender->visiblePositionForIndex(offsetStart + offsetLength)).toNormalizedRange();
    }

    return subrangeForFocusElement(offsetStart, offsetLength);
}

void InputHandler::setSelection(unsigned int selectionStart, unsigned int selectionEnd)
{
    if (!isActiveTextEdit())
        return;

    ASSERT(m_currentFocusElement->document() && m_currentFocusElement->document()->frame());

    HTMLTextFormControlElement* controlElement = toTextControlElement(m_currentFocusElement.get());
    if (controlElement) {
        controlElement->setSelectionStart(selectionStart);
        controlElement->setSelectionEnd(selectionEnd);
    } else {
        RefPtr<Range> selectionRange = subrangeForFocusElement(selectionStart, selectionEnd - selectionStart);
        if (selectionStart == selectionEnd)
            m_currentFocusElement->document()->frame()->selection()->setSelection(VisibleSelection(selectionRange.get()->startPosition(), DOWNSTREAM));
        else
            m_currentFocusElement->document()->frame()->selection()->setSelectedRange(selectionRange.get(), DOWNSTREAM, false);
    }
#if SHOWDEBUG_INPUTHANDLER
        Olympia::Platform::log(Olympia::Platform::LogLevelCritical, "InputHandler::setSelection selectionStart=%u, selectionEnd=%u", selectionStart, selectionEnd);
#endif
}

void InputHandler::setCaretPosition(int requestedFrameId, int requestedElementId, int caretPosition)
{
    if (!isActiveTextEdit() || caretPosition < 0 || !allowSyncInput())
        return;

    ASSERT(m_currentFocusElement->document() && m_currentFocusElement->document()->frame());

    if (requestedFrameId < 1 || requestedElementId < 1)
        return;

    if ((static_cast<unsigned int>(requestedFrameId) == idFromFrame(m_currentFocusElement->document()->frame())) && (static_cast<unsigned int>(requestedElementId) == idFromElement(m_currentFocusElement.get()))) {

#if SHOWDEBUG_INPUTHANDLER
        Olympia::Platform::log(Olympia::Platform::LogLevelCritical, "InputHandler::setCaretPosition frameId=%u, FocusId=%u, caretPosition=%u", requestedFrameId, requestedElementId, caretPosition);
#endif
        ProcessingChangeGuard guard(this);
        setSelection(caretPosition, caretPosition);
    }
}
Olympia::Platform::IntRect InputHandler::rectForCaret(int index)
{
    if (!isActiveTextEdit())
        return Olympia::Platform::IntRect();

    ASSERT(m_currentFocusElement->document() && m_currentFocusElement->document()->frame());

    unsigned int currentTextLength = elementText(m_currentFocusElement.get()).length();
    if (index < 0 || static_cast<unsigned int>(index) > currentTextLength)
        // Invalid request
        return Olympia::Platform::IntRect();

    VisiblePosition visiblePosition;
    SelectionController caretSelection;
    HTMLTextFormControlElement* controlElement = toTextControlElement(m_currentFocusElement.get());
    if (controlElement) {
        RenderTextControl* textRender = toRenderTextControl(controlElement->renderer());
        visiblePosition = textRender->visiblePositionForIndex(index);
        if (!controlElement->hasTagName(HTMLNames::textareaTag) && !textRender->softBreaksBeforeIndex(index))
            return visiblePosition.absoluteCaretBounds();

        caretSelection.setSelection(visiblePosition);
    } else {
        RefPtr<Range> selectionRange = subrangeForFocusElement(0, index);
        caretSelection.setSelection(VisibleSelection(selectionRange->endPosition(), DOWNSTREAM));
    }

    caretSelection.modify(SelectionController::AlterationExtend, SelectionController::DirectionForward, CharacterGranularity);
    visiblePosition = caretSelection.selection().visibleStart();
    return visiblePosition.absoluteCaretBounds();
}

void InputHandler::cancelSelection()
{
    if (!isActiveTextEdit())
        return;

    ASSERT(m_currentFocusElement->document() && m_currentFocusElement->document()->frame());

    unsigned int selectionStartPosition = selectionStart();
    ProcessingChangeGuard guard(this);
    setSelection(selectionStartPosition, selectionStartPosition);
}

#if OS(BLACKBERRY)
void InputHandler::applyPasswordFieldUnmaskSecure(const Element* element, int startUnmask, int lengthUnmask)
{
#warning This code path has been removed and should not be used anymore.  Clean up should occur after tree merge.
#if 0
    ASSERT(element);
    ASSERT(elementType(element) == InputTypePassword);

    if (!element->renderer())
        return;

    RenderTextControl* renderTextControl = toRenderTextControl(m_currentFocusElement->renderer());
    renderTextControl->setInnerTextUnmaskSecure(startUnmask, lengthUnmask);

#if SHOWDEBUG_INPUTHANDLER
    Olympia::Platform::log(Olympia::Platform::LogLevelCritical, "InputHandler::applyPasswordFieldUnmaskSecure start = %d, length = %d", startUnmask, lengthUnmask);
#endif
#endif
}
#endif

bool InputHandler::handleKeyboardInput(PlatformKeyboardEvent::Type type, unsigned short character, unsigned modifiers)
{
#if SHOWDEBUG_INPUTHANDLER
    Olympia::Platform::log(Olympia::Platform::LogLevelCritical, "InputHandler::handleKeyboardInput received character=%lc, type=%d", character, type);
#endif

    unsigned adjustedModifiers = modifiers;
    if (WTF::isASCIIUpper(character))
        adjustedModifiers |= Olympia::Platform::KEYMOD_SHIFT;

    ASSERT(m_webPage->m_page->focusController());
    bool keyboardEventHandled = false;
    if (Frame* focusedFrame = m_webPage->m_page->focusController()->focusedFrame()) {
        Olympia::Platform::KeyboardEvent keyboardEvent(character, type == PlatformKeyboardEvent::KeyDown || type == PlatformKeyboardEvent::Char, adjustedModifiers);
        keyboardEventHandled = focusedFrame->eventHandler()->keyEvent(PlatformKeyboardEvent(keyboardEvent));

        // Handle Char event as keyDown followed by KeyUp.
        if (type == PlatformKeyboardEvent::Char) {
            keyboardEvent.setKeyDown(false);
            keyboardEventHandled = focusedFrame->eventHandler()->keyEvent(PlatformKeyboardEvent(keyboardEvent)) || keyboardEventHandled;
        }
    }
    return keyboardEventHandled;
}

void InputHandler::handleNavigationMove(unsigned short character, bool shiftDown, bool altDown)
{
#if SHOWDEBUG_INPUTHANDLER
    Olympia::Platform::log(Olympia::Platform::LogLevelCritical, "InputHandler::handleNavigationMove received character=%lc", character);
#endif

    ASSERT(m_webPage->m_page->focusController());
    if (Frame* focusedFrame = m_webPage->m_page->focusController()->focusedFrame()) {
        switch (character) {
        case Olympia::Platform::KEY_LEFT:
            if (altDown) {
                // If alt is down, do not break out of the field.
                if (shiftDown)
                    focusedFrame->editor()->command("MoveToBeginningOfLineAndModifySelection").execute();
                else
                    focusedFrame->editor()->command("MoveToBeginningOfLine").execute();
            } else if (shiftDown) {
                focusedFrame->editor()->command("MoveBackwardAndModifySelection").execute();
            } else {
                // If we are at the extent of the edit box restore the cursor.
                if (selectionAtStartOfElement())
                    setNavigationMode(false);
                else
                    focusedFrame->editor()->command("MoveBackward").execute();
            }
            break;
        case Olympia::Platform::KEY_RIGHT:
            if (altDown) {
                // If alt is down, do not break out of the field.
                if (shiftDown)
                    focusedFrame->editor()->command("MoveToEndOfLineAndModifySelection").execute();
                else
                    focusedFrame->editor()->command("MoveToEndOfLine").execute();
            } else if (shiftDown) {
                focusedFrame->editor()->command("MoveForwardAndModifySelection").execute();
            } else {
                // If we are at the extent of the edit box restore the cursor.
                if (selectionAtEndOfElement())
                    setNavigationMode(false);
                else
                    focusedFrame->editor()->command("MoveForward").execute();
            }
            break;
        case Olympia::Platform::KEY_UP:
            if (altDown) {
                // If alt is down, do not break out of the field.
                if (shiftDown)
                    focusedFrame->editor()->command("MoveToBeginningOfDocumentAndModifySelection").execute();
                else
                    focusedFrame->editor()->command("MoveToBeginningOfDocument").execute();
            } else if (shiftDown) {
                focusedFrame->editor()->command("MoveUpAndModifySelection").execute();
            } else {
                // If we are at the extent of the edit box restore the cursor.
                if (selectionAtStartOfElement())
                    setNavigationMode(false);
                else
                    focusedFrame->editor()->command("MoveUp").execute();
            }
            break;
        case Olympia::Platform::KEY_DOWN:
            if (altDown) {
                // If alt is down, do not break out of the field.
                if (shiftDown)
                    focusedFrame->editor()->command("MoveToEndOfDocumentAndModifySelection").execute();
                else
                    focusedFrame->editor()->command("MoveToEndOfDocument").execute();
            } else if (shiftDown) {
                focusedFrame->editor()->command("MoveDownAndModifySelection").execute();
            } else {
                // If we are at the extent of the edit box restore the cursor.
                if (selectionAtEndOfElement())
                    setNavigationMode(false);
                else
                    focusedFrame->editor()->command("MoveDown").execute();
            }
            break;
        }
    }
}

void InputHandler::deleteSelection()
{
    if (!isActiveTextEdit())
        return;

    ASSERT(m_currentFocusElement->document() && m_currentFocusElement->document()->frame());
    Frame* frame = m_currentFocusElement->document()->frame();

    if (frame->selection()->selectionType() != VisibleSelection::RangeSelection)
        return;

    ASSERT(frame->editor());
    frame->editor()->command("DeleteBackward").execute();
}

void InputHandler::insertText(const WTF::String& string)
{
    if (!isActiveTextEdit())
        return;

    ASSERT(m_currentFocusElement->document() && m_currentFocusElement->document()->frame() && m_currentFocusElement->document()->frame()->editor());
    Editor* editor = m_currentFocusElement->document()->frame()->editor();

    editor->command("InsertText").execute(string);
}

void InputHandler::clearField()
{
    if (!isActiveTextEdit())
        return;

    ASSERT(m_currentFocusElement->document() && m_currentFocusElement->document()->frame() && m_currentFocusElement->document()->frame()->editor());
    Editor* editor = m_currentFocusElement->document()->frame()->editor();

    editor->command("SelectAll").execute();
    editor->command("DeleteBackward").execute();
}

bool InputHandler::executeTextEditCommand(const WTF::String& commandName)
{
    ASSERT(m_webPage->focusedOrMainFrame() && m_webPage->focusedOrMainFrame()->editor());
    Editor* editor = m_webPage->focusedOrMainFrame()->editor();

    return editor->command(commandName).execute();
}

void InputHandler::cut()
{
#if OS(QNX)
    executeTextEditCommand("Cut");
#else
    notImplemented();
#endif
}

void InputHandler::copy()
{
#if OS(QNX)
    executeTextEditCommand("Copy");
#else
    notImplemented();
#endif
}

void InputHandler::paste()
{
#if OS(QNX)
    executeTextEditCommand("Paste");
#else
    notImplemented();
#endif
}

#if OS(BLACKBERRY)
bool InputHandler::setCursorPositionIfInputTextValidated(const WebCore::String& expectedString, unsigned int desiredCursorPosition)
{
    // The element may have lost focus while changing the input.  Don't validate if the focus element
    // has been lost.
    if (!isActiveTextEdit())
        return false;

    if (expectedString != elementText(m_currentFocusElement.get()))
        return false;

    ASSERT(m_currentFocusElement->document() && m_currentFocusElement->document()->frame());
    setSelection(desiredCursorPosition, desiredCursorPosition);
    m_currentFocusElement->document()->frame()->revealSelection();

    return true;
}

static WebCore::FontWeight convertFontWeight(const FontWeights& weight)
{
    switch (weight) {
    case ELightWeight:
        return WebCore::FontWeight300;
    case EBoldWeight:
        return WebCore::FontWeightBold;
    case EExtraBoldWeight:
        return WebCore::FontWeight800;
    case ESuperBoldWeight:
        return WebCore::FontWeight900;
    case EDefaultWeight:
    case EMediumWeight:
    default:
        return WebCore::FontWeightNormal;
    }
}

static WebCore::AttributeTextStyle::AttributeStyleState convertAttributeValue(const AttributeValue& value)
{
    switch (value) {
    case AttributeNotApplied:
        return WebCore::AttributeTextStyle::AttributeNotApplied;
    case AttributeApplied:
        return WebCore::AttributeTextStyle::AttributeApplied;
    case AttributeNotProvided:
    default:
        return WebCore::AttributeTextStyle::AttributeNotProvided;
    }
}

static WebCore::AttributeTextStyle::UnderlineStyle convertUnderlineStyle(const UnderlineStyles& style)
{
    switch (style) {
    case NoUnderline:
        return WebCore::AttributeTextStyle::NoUnderline;
    case StandardUnderline:
        return WebCore::AttributeTextStyle::StandardUnderline;
    case BrokenUnderline:
        return WebCore::AttributeTextStyle::BrokenUnderline;
    case DottedUnderline:
        return WebCore::AttributeTextStyle::DottedUnderline;
    case WavyUnderline:
    case UnderlineNotProvided:
    default:
        return WebCore::AttributeTextStyle::UnderlineNotProvided;
    }
}

ReplaceTextErrorCode InputHandler::replaceText(const ReplaceArguments& replaceArguments, const AttributedText& attributedText)
{
    ReplaceTextErrorCode errorCode = processReplaceText(replaceArguments, attributedText);

    // If an error occurred during processing, send a textChanged event so the client
    // can update it's cache of the input field details.
    if (errorCode != ReplaceTextSuccess && errorCode != ReplaceTextChangeDeniedSyncMismatch)
        notifyTextChanged(m_currentFocusElement.get());

    return errorCode;
}

ReplaceTextErrorCode InputHandler::processReplaceText(const ReplaceArguments& replaceArguments, const AttributedText& attributedText)
{
    if (!allowSyncInput())
        return ReplaceTextChangeDeniedSyncMismatch;

    if (!isActiveTextEdit())
        return ReplaceTextNonSpecifiedFault;

    ASSERT(m_currentFocusElement->document() && m_currentFocusElement->document()->frame());
    Frame* frame = m_currentFocusElement->document()->frame();

    unsigned int start = replaceArguments.start;
    unsigned int end = replaceArguments.end;
    unsigned int attributeStart = replaceArguments.attributeStart;
    unsigned int attributeEnd = replaceArguments.attributeEnd;
    unsigned int desiredCursorPosition = replaceArguments.cursorPosition;

    unsigned int stringLength = elementText(m_currentFocusElement.get()).length();
    if (start > stringLength)
        return ReplaceTextWrongStartArgument;
    if (end < start || end > stringLength)
        return ReplaceTextWrongEndArgument;

    WTF::String replacementText(attributedText.text, attributedText.length);
    WTF::String expectedString = elementText(m_currentFocusElement.get());
    if (expectedString.isEmpty())
        expectedString = replacementText;
    else
        expectedString.replace(start, end - start, replacementText);

    if (attributeStart + start > expectedString.length())
        return ReplaceTextWrongAttributeStart;

    if (attributeEnd < attributeStart || attributeEnd + start > expectedString.length())
        return ReplaceTextWrongAttributeEnd;

    if (desiredCursorPosition > expectedString.length())
        return ReplaceTextWrongCursorLocation;

    // Validation of input parameters is complete.  A change will be made!
    ProcessingChangeGuard guard(this);

#if SHOWDEBUG_INPUTHANDLER
    Olympia::Platform::log(Olympia::Platform::LogLevelCritical, "InputHandler::replaceText Request being processed start=%d, end=%d, attributeStart=%d, attributeEnd=%d, desiredCursor=%d, stringLength=%d, attributeTextLength=%d, runsCount=%d", start, end, attributeStart, attributeEnd, desiredCursorPosition, stringLength, attributedText.length, attributedText.runsCount);

    Olympia::Platform::log(Olympia::Platform::LogLevelCritical, "InputHandler::replaceText Text before %s", elementText(m_currentFocusElement.get()).latin1().data());
#endif

    // Disable selectionHandler's active selection as we will be resetting and these
    // changes should not be handled as notification event.
    m_webPage->m_selectionHandler->setSelectionActive(false);

    // Remove any attributes currently set on the text in the field before making any changes.
    removeAttributedTextMarker();

    bool isPasswordField = elementType(m_currentFocusElement.get()) == InputTypePassword;
    if (isPasswordField)
        applyPasswordFieldUnmaskSecure(m_currentFocusElement.get(), 0, 0);

    // Place the cursor at the desired location or select the appropriate text.
    if (selectionStart() != start || selectionEnd() != end)
        setSelection(start, end);

    if (attributedText.length == 1 && attributeStart == attributeEnd && start == end) {
        // Handle single key non-attributed entry as key press rather than replaceText to allow
        // triggering of javascript events.

#if ENABLE_DEBUG_UNDOREDO
        if (replacementText[0] == 'u')
            editor->command("Undo").execute();
        else if (replacementText[0] == 'r')
            editor->command("Redo").execute();
        else
#endif
            handleKeyboardInput(PlatformKeyboardEvent::Char, replacementText[0]);

        return setCursorPositionIfInputTextValidated(expectedString, desiredCursorPosition) ? ReplaceTextSuccess : ReplaceTextChangeNotValidated;
    }

    Editor* editor = frame->editor();
    ASSERT(editor);

    // Perform the text change as a single command if there is one.
    if (end > start)
        editor->command("DeleteBackward").execute();
    if (attributedText.length)
        editor->command("InsertText").execute(replacementText);

#if SHOWDEBUG_INPUTHANDLER
    Olympia::Platform::log(Olympia::Platform::LogLevelCritical, "InputHandler::replaceText Text after %s", elementText(m_currentFocusElement.get()).latin1().data());
#endif

    if (!setCursorPositionIfInputTextValidated(expectedString, desiredCursorPosition))
        return ReplaceTextChangeNotValidated;

    // Apply the attributes to the field.
    AttribRun* attributeRun = reinterpret_cast<AttribRun*>(attributedText.runs);
    for (int i = 0; i < attributedText.runsCount; i++) {
        unsigned int runStartPosition = start + attributeRun->offset;
        unsigned int runEndPosition = runStartPosition + attributeRun->length;
        if (runEndPosition > expectedString.length() || attributeRun->offset + attributeRun->length > attributeEnd)
            return ReplaceTextInvalidRunlength;

        addAttributedTextMarker(runStartPosition, attributeRun->length, attributeRun->attributes);

        // This is a hack.  It would be nice to test against the password attribute, but it is not present
        // on the reduced keyboard, but other attributes are and we need to show the text.  Additionally,
        // we can't just check if there is a run and show the text because we get alot of useless requests
        // with empty attributes (no change) being applied over top of the text.

        // There should never be more than one run, just in case there is, we'll only actually be using the
        // last one.
        if (isPasswordField && attributeRun->attributes)
            applyPasswordFieldUnmaskSecure(m_currentFocusElement.get(), runStartPosition, attributeRun->length);

        attributeRun++;
    }
    return ReplaceTextSuccess;
}

void InputHandler::addAttributedTextMarker(unsigned int startPosition, unsigned int length, uint64_t attributes)
{
    if (!attributes) // No attributes set, don't create an empty AttributeText marker.
        return;

    AttributeTextStyle textStyle;
    textStyle.setItalic(convertAttributeValue(static_cast<AttributeValue>(getItalicAttrib(attributes))));
    textStyle.setStrikethrough(convertAttributeValue(static_cast<AttributeValue>(getStrikethroughAttrib(attributes))));
    textStyle.setUnderline(convertUnderlineStyle(static_cast<UnderlineStyles>(getUnderlineAttrib(attributes))));

    AttributeValue highlightAttribute = static_cast<AttributeValue>(getHighlightAttrib(attributes));
    if (highlightAttribute == AttributeApplied) {
        textStyle.setBackgroundColor(Color("blue"));
        textStyle.setTextColor(Color("white"));
    } else if (highlightAttribute == AttributeNotApplied) {
        textStyle.setBackgroundColor(Color("white"));
        textStyle.setTextColor(Color("black"));
    }

    FontWeights fontWeight = static_cast<FontWeights>(getFontWeightAttrib(attributes));
    if (fontWeight != EUnsetWeight)
        textStyle.setFontWeight(convertFontWeight(static_cast<FontWeights>(fontWeight)));

    RefPtr<Range> markerRange = focusElementRange(startPosition, length);
    m_currentFocusElement->document()->markers()->addMarker(markerRange.get(), DocumentMarker::AttributeText, textStyle);

#if SHOWDEBUG_INPUTHANDLER
    Olympia::Platform::log(Olympia::Platform::LogLevelCritical, "InputHandler::addAttributedTextMarker Run Details start=%u, length=%u, italic=%d, strikethrough=%d, underline=%d, highlight=%d, fontWeight=%d"
                           , startPosition, length, static_cast<AttributeValue>(getItalicAttrib(attributes)), static_cast<AttributeValue>(getStrikethroughAttrib(attributes))
                           , static_cast<UnderlineStyles>(getUnderlineAttrib(attributes)), highlightAttribute, fontWeight);
#endif
}

void InputHandler::removeAttributedTextMarker()
{
    // Remove all attribute text markers.
    if (m_currentFocusElement && m_currentFocusElement->document())
        m_currentFocusElement->document()->markers()->removeMarkers(DocumentMarker::AttributeText);
}
#endif
void InputHandler::handleInputLocaleChanged(bool isRTL)
{
    if (!isActiveTextEdit())
        return;

    ASSERT(m_currentFocusElement->document() && m_currentFocusElement->document()->frame());
    Editor* editor = m_currentFocusElement->document()->frame()->editor();
    RenderObject* renderer = m_currentFocusElement->renderer();

    if (!renderer)
        return;
    ASSERT(editor);

    if ((renderer->style()->direction() == RTL) != isRTL)
        editor->setBaseWritingDirection(isRTL ? RightToLeftWritingDirection : LeftToRightWritingDirection);
}

void InputHandler::clearCurrentFocusElement()
{
    if (m_currentFocusElement)
        m_currentFocusElement->blur();
}

bool InputHandler::willOpenPopupForNode(Node* node)
{
    // This method must be kept synchronized with InputHandler::didNodeOpenPopup.
    if (!node)
        return false;

    if (node->hasTagName(HTMLNames::selectTag) || node->hasTagName(HTMLNames::optionTag)) {
        // We open list popups for options and selects
        return true;
    }

    if (node->isElementNode() && node->hasTagName(HTMLNames::inputTag)) {
        Element* element = static_cast<Element*>(node);
        OlympiaInputType type = elementType(element);
        if ((isTypeDateTime(type) && type != InputTypeWeek)
                || type == InputTypeColor) {
            // We open popups for these input types
            return true;
        }
    }
    return false;
}

bool InputHandler::didNodeOpenPopup(Node* node)
{
    // This method must be kept synchronized with InputHandler::willOpenPopupForNode.
    if (!node)
        return false;

    if (node->hasTagName(HTMLNames::selectTag))
        return openSelectPopup(static_cast<HTMLSelectElement*>(node));

    if (node->hasTagName(HTMLNames::optionTag)) {
        HTMLOptionElement* optionElement = static_cast<HTMLOptionElement*>(node);
        return openSelectPopup(optionElement->ownerSelectElement());
    }

    if (node->isElementNode() && node->hasTagName(HTMLNames::inputTag)) {
        Element* element = static_cast<Element*>(node);
        OlympiaInputType type = elementType(element);
        if (isTypeDateTime(type) && type != InputTypeWeek)
            return openDatePopup(static_cast<HTMLInputElement*>(element), type);

        if (type == InputTypeColor)
            return openColorPopup(static_cast<HTMLInputElement*>(element));
    }
    return false;
}

bool InputHandler::openSelectPopup(HTMLSelectElement* select)
{
    if (!select || select->disabled())
        return false;

    if (isActiveTextEdit())
        clearCurrentFocusElement();

    m_currentFocusElement = select;
    m_currentFocusElementType = SelectPopup;
    const WTF::Vector<Element*>& listItems = select->listItems();
    int size = listItems.size();
    if (!size) {
#if OS(BLACKBERRY)
        // FIXME:  A warning or empty dialog should be displayed when no options are available.
        return false;
#else
        ScopeArray<WebString> labels;
        bool* enableds = 0;
        int* itemTypes = 0;
        bool* selecteds = 0;
        m_webPage->m_client->openPopupList(false, size, labels, enableds, itemTypes, selecteds);
        return true;
#endif
    }

    bool multiple = select->multiple();

    ScopeArray<WebString> labels;
    labels.reset(new WebString[size]);
    bool* enableds = new bool[size];
    int* itemTypes = new int[size];
    bool* selecteds = new bool[size];

    for (int i = 0; i < size; i++) {
        if (listItems[i]->hasTagName(HTMLNames::optionTag)) {
            HTMLOptionElement* option = static_cast<HTMLOptionElement*>(listItems[i]);
            labels[i] = option->textIndentedToRespectGroupLabel();
            enableds[i] = option->disabled() ? 0 : 1;
            selecteds[i] = option->selected();
            itemTypes[i] = TypeOption;
        } else if (listItems[i]->hasTagName(HTMLNames::optgroupTag)) {
            HTMLOptGroupElement* optGroup = static_cast<HTMLOptGroupElement*>(listItems[i]);
            labels[i] = optGroup->groupLabelText();
            enableds[i] = optGroup->disabled() ? 0 : 1;
            selecteds[i] = false;
            itemTypes[i] = TypeGroup;
        } else if (listItems[i]->hasTagName(HTMLNames::hrTag)) {
            enableds[i] = false;
            selecteds[i] = false;
            itemTypes[i] = TypeSeparator;
        }
    }

    m_webPage->m_client->openPopupList(multiple, size, labels, enableds, itemTypes, selecteds);
    return true;
}

void InputHandler::setPopupListIndex(int index)
{
    if (index == -2) // Abandon
        return clearCurrentFocusElement();

    if (!isActiveSelectPopup())
        return clearCurrentFocusElement();

    RenderObject* renderer = m_currentFocusElement->renderer();
    if (renderer && renderer->isMenuList()) {
        RenderMenuList* renderMenu = toRenderMenuList(renderer);
        renderMenu->hidePopup();
    }

    HTMLSelectElement* selectElement = static_cast<HTMLSelectElement*>(m_currentFocusElement.get());
    int optionIndex = (static_cast<WebCore::SelectElement*>(selectElement))->listToOptionIndex(index);
    selectElement->setSelectedIndexByUser(optionIndex, /* deselect = true */ true, /* fireOnChangeNow = false */ true);
    clearCurrentFocusElement();
}

void InputHandler::setPopupListIndexes(int size, bool* selecteds)
{
    if (!isActiveSelectPopup())
        return clearCurrentFocusElement();

    if (size < 0)
        return;

    HTMLSelectElement* selectElement = static_cast<HTMLSelectElement*>(m_currentFocusElement.get());

    const WTF::Vector<Element*>& items = selectElement->listItems();
    if (items.size() != static_cast<unsigned int>(size))
        return;

    HTMLOptionElement* option;
    for (int i = 0; i < size; i++) {
        if (items[i]->hasTagName(HTMLNames::optionTag)) {
            option = static_cast<HTMLOptionElement*>(items[i]);
            option->setSelectedState(selecteds[i]);
        }
    }
    // Force repaint because we do not send mouse events to the select element
    // and the element doesn't automatically repaint itself.
    selectElement->dispatchFormControlChangeEvent();
    selectElement->renderer()->repaint(true);
    clearCurrentFocusElement();
}

}
}
