/*
 * Copyright (C) 2009 Torch Mobile Inc. http://www.torchmobile.com/
 * Copyright (C) Research In Motion Limited 2009-2011. All rights reserved.
 */

#include "config.h"
#include "ResourceRequest.h"

#include "CString.h"
#include "NotImplemented.h"

#include <BlackBerryPlatformClient.h>
#include <streams/NetworkRequest.h>

using Olympia::Platform::NetworkRequest;

namespace WebCore {

unsigned initializeMaximumHTTPConnectionCountPerHost()
{
#if OS(BLACKBERRY)
    return 40;
#else
    return 6;
#endif
}

static inline NetworkRequest::CachePolicy platformCachePolicyForRequest(const ResourceRequest& request)
{
    switch (request.cachePolicy()) {
    case WebCore::UseProtocolCachePolicy:
        return NetworkRequest::UseProtocolCachePolicy;
    case WebCore::ReloadIgnoringCacheData:
        return NetworkRequest::ReloadIgnoringCacheData;
    case WebCore::ReturnCacheDataElseLoad:
        return NetworkRequest::ReturnCacheDataElseLoad;
    case WebCore::ReturnCacheDataDontLoad:
        return NetworkRequest::ReturnCacheDataDontLoad;
    default:
        ASSERT_NOT_REACHED();
        return NetworkRequest::UseProtocolCachePolicy;
    }
}

static inline NetworkRequest::TargetType platformTargetTypeForRequest(const ResourceRequest& request)
{
    if (request.isXMLHTTPRequest())
        return NetworkRequest::TargetIsXMLHTTPRequest;

    switch (request.targetType()) {
    case ResourceRequest::TargetIsMainFrame:
        return NetworkRequest::TargetIsMainFrame;
    case ResourceRequest::TargetIsSubframe:
        return NetworkRequest::TargetIsSubframe;
    case ResourceRequest::TargetIsSubresource:
        return NetworkRequest::TargetIsSubresource;
    case ResourceRequest::TargetIsStyleSheet:
        return NetworkRequest::TargetIsStyleSheet;
    case ResourceRequest::TargetIsScript:
        return NetworkRequest::TargetIsScript;
    case ResourceRequest::TargetIsFontResource:
        return NetworkRequest::TargetIsFontResource;
    case ResourceRequest::TargetIsImage:
        return NetworkRequest::TargetIsImage;
    case ResourceRequest::TargetIsObject:
        return NetworkRequest::TargetIsObject;
    case ResourceRequest::TargetIsMedia:
        return NetworkRequest::TargetIsMedia;
    case ResourceRequest::TargetIsWorker:
        return NetworkRequest::TargetIsWorker;
    case ResourceRequest::TargetIsSharedWorker:
        return NetworkRequest::TargetIsSharedWorker;
    default:
        ASSERT_NOT_REACHED();
        return NetworkRequest::TargetIsUnknown;
    }
}

void ResourceRequest::initializePlatformRequest(NetworkRequest& platformRequest, bool isInitial) const
{
    // if this is the initial load, skip the request body and headers
    if (isInitial)
        platformRequest.setRequestInitial(timeoutInterval());
    else {
        platformRequest.setRequestUrl(url().string().utf8().data(),
                httpMethod().latin1().data(),
                platformCachePolicyForRequest(*this),
                platformTargetTypeForRequest(*this),
                timeoutInterval());

        if (httpBody() && !httpBody()->isEmpty()) {
            const Vector<FormDataElement>& elements = httpBody()->elements();
            // use setData for simple forms because it is slightly more efficient
            if (elements.size() == 1 && elements[0].m_type == FormDataElement::data)
                platformRequest.setData(elements[0].m_data.data(), elements[0].m_data.size());
            else {
                for (unsigned i = 0; i < elements.size(); ++i) {
                    const FormDataElement& element = elements[i];
                    if (element.m_type == FormDataElement::data)
                        platformRequest.addMultipartData(element.m_data.data(), element.m_data.size());
                    else if (element.m_type == FormDataElement::encodedFile)
                        platformRequest.addMultipartFilename(element.m_filename.characters(), element.m_filename.length());
#if ENABLE(BLOB)
                    else if (element.m_type == FormDataElement::encodedBlob)
                        continue; // We don't support blob now!
#endif
                    else
                        ASSERT_NOT_REACHED(); // unknown type
                }
            }
        }

        for (HTTPHeaderMap::const_iterator it = httpHeaderFields().begin();
                it != httpHeaderFields().end();
                ++it)
        {
            WTF::String key = it->first;
            WTF::String value = it->second;
            if (!key.isEmpty() && !value.isEmpty())
                platformRequest.addHeader(key.latin1().data(), value.latin1().data());
        }

        // Locale has the form "en-US". Construct accept language like "en-US, en;q=0.8".
        std::string locale = Olympia::Platform::Client::get()->getLocale();
        std::string acceptLanguage = locale + ", " + locale.substr(0, 2) + ";q=0.8";
        platformRequest.addHeader("Accept-Language", acceptLanguage.c_str());
    }
}

PassOwnPtr<CrossThreadResourceRequestData> ResourceRequest::doPlatformCopyData(PassOwnPtr<CrossThreadResourceRequestData> data) const
{
    data->m_token = m_token;
    data->m_anchorText = m_anchorText;
    data->m_isXMLHTTPRequest = m_isXMLHTTPRequest;
    data->m_mustHandleInternally = m_mustHandleInternally;
    return data;
}

void ResourceRequest::doPlatformAdopt(PassOwnPtr<CrossThreadResourceRequestData> data)
{
    m_token = data->m_token;
    m_anchorText = data->m_anchorText;
    m_isXMLHTTPRequest = data->m_isXMLHTTPRequest;
    m_mustHandleInternally = data->m_mustHandleInternally;
}

} // namespace WebCore
