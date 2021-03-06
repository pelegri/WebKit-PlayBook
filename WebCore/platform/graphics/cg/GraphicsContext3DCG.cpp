/*
 * Copyright (C) 2010 Apple Inc. All rights reserved.
 * Copyright (C) 2010 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE COMPUTER, INC. ``AS IS'' AND ANY
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

#include "config.h"

#if ENABLE(3D_CANVAS)

#include "GraphicsContext3D.h"

#include "Image.h"

#include <CoreGraphics/CGBitmapContext.h>
#include <CoreGraphics/CGContext.h>
#include <CoreGraphics/CGDataProvider.h>
#include <CoreGraphics/CGImage.h>

#include <wtf/RetainPtr.h>

namespace WebCore {

bool GraphicsContext3D::getImageData(Image* image,
                                     unsigned int format,
                                     unsigned int type,
                                     bool premultiplyAlpha,
                                     Vector<uint8_t>& outputVector)
{
    if (!image)
        return false;
    CGImageRef cgImage;
    RetainPtr<CGImageRef> decodedImage;
    if (image->data()) {
        ImageSource decoder(false);
        decoder.setData(image->data(), true);
        if (!decoder.frameCount())
            return false;
        decodedImage = decoder.createFrameAtIndex(0);
        cgImage = decodedImage.get();
    } else
        cgImage = image->nativeImageForCurrentFrame();
    if (!cgImage)
        return false;
    size_t width = CGImageGetWidth(cgImage);
    size_t height = CGImageGetHeight(cgImage);
    if (!width || !height)
        return false;
    size_t bitsPerComponent = CGImageGetBitsPerComponent(cgImage);
    size_t bitsPerPixel = CGImageGetBitsPerPixel(cgImage);
    if (bitsPerComponent != 8 && bitsPerComponent != 16)
        return false;
    if (bitsPerPixel % bitsPerComponent)
        return false;
    size_t componentsPerPixel = bitsPerPixel / bitsPerComponent;
    bool srcByteOrder16Big = false;
    if (bitsPerComponent == 16) {
        CGBitmapInfo bitInfo = CGImageGetBitmapInfo(cgImage);
        switch (bitInfo & kCGBitmapByteOrderMask) {
        case kCGBitmapByteOrder16Big:
            srcByteOrder16Big = true;
            break;
        case kCGBitmapByteOrder16Little:
            srcByteOrder16Big = false;
            break;
        case kCGBitmapByteOrderDefault:
            // This is a bug in earlier version of cg where the default endian
            // is little whereas the decoded 16-bit png image data is actually
            // Big. Later version (10.6.4) no longer returns ByteOrderDefault.
            srcByteOrder16Big = true;
            break;
        default:
            return false;
        }
    }
    SourceDataFormat srcDataFormat = kSourceFormatRGBA8;
    AlphaOp neededAlphaOp = kAlphaDoNothing;
    switch (CGImageGetAlphaInfo(cgImage)) {
    case kCGImageAlphaPremultipliedFirst:
        // This path is only accessible for MacOS earlier than 10.6.4.
        // This is a special case for texImage2D with HTMLCanvasElement input,
        // in which case image->data() should be null.
        ASSERT(!image->data());
        if (!premultiplyAlpha)
            neededAlphaOp = kAlphaDoUnmultiply;
        switch (componentsPerPixel) {
        case 2:
            if (bitsPerComponent == 8)
                srcDataFormat = kSourceFormatAR8;
            else
                srcDataFormat = srcByteOrder16Big ? kSourceFormatAR16Big : kSourceFormatAR16Little;
            break;
        case 4:
            if (bitsPerComponent == 8)
                srcDataFormat = kSourceFormatARGB8;
            else
                srcDataFormat = srcByteOrder16Big ? kSourceFormatARGB16Big : kSourceFormatARGB16Little;
            break;
        default:
            return false;
        }
        break;
    case kCGImageAlphaFirst:
        // This path is only accessible for MacOS earlier than 10.6.4.
        if (premultiplyAlpha)
            neededAlphaOp = kAlphaDoPremultiply;
        switch (componentsPerPixel) {
        case 1:
            if (bitsPerComponent == 8)
                srcDataFormat = kSourceFormatA8;
            else
                srcDataFormat = srcByteOrder16Big ? kSourceFormatA16Big : kSourceFormatA16Little;
            break;
        case 2:
            if (bitsPerComponent == 8)
                srcDataFormat = kSourceFormatAR8;
            else
                srcDataFormat = srcByteOrder16Big ? kSourceFormatAR16Big : kSourceFormatAR16Little;
            break;
        case 4:
            if (bitsPerComponent == 8)
                srcDataFormat = kSourceFormatARGB8;
            else
                srcDataFormat = srcByteOrder16Big ? kSourceFormatARGB16Big : kSourceFormatARGB16Little;
            break;
        default:
            return false;
        }
        break;
    case kCGImageAlphaNoneSkipFirst:
        // This path is only accessible for MacOS earlier than 10.6.4.
        switch (componentsPerPixel) {
        case 2:
            if (bitsPerComponent == 8)
                srcDataFormat = kSourceFormatAR8;
            else
                srcDataFormat = srcByteOrder16Big ? kSourceFormatAR16Big : kSourceFormatAR16Little;
            break;
        case 4:
            if (bitsPerComponent == 8)
                srcDataFormat = kSourceFormatARGB8;
            else
                srcDataFormat = srcByteOrder16Big ? kSourceFormatARGB16Big : kSourceFormatARGB16Little;
            break;
        default:
            return false;
        }
        break;
    case kCGImageAlphaPremultipliedLast:
        // This is a special case for texImage2D with HTMLCanvasElement input,
        // in which case image->data() should be null.
        ASSERT(!image->data());
        if (!premultiplyAlpha)
            neededAlphaOp = kAlphaDoUnmultiply;
        switch (componentsPerPixel) {
        case 2:
            if (bitsPerComponent == 8)
                srcDataFormat = kSourceFormatRA8;
            else
                srcDataFormat = srcByteOrder16Big ? kSourceFormatRA16Big : kSourceFormatRA16Little;
            break;
        case 4:
            if (bitsPerComponent == 8)
                srcDataFormat = kSourceFormatRGBA8;
            else
                srcDataFormat = srcByteOrder16Big ? kSourceFormatRGBA16Big : kSourceFormatRGBA16Little;
            break;
        default:
            return false;
        }
        break;
    case kCGImageAlphaLast:
        if (premultiplyAlpha)
            neededAlphaOp = kAlphaDoPremultiply;
        switch (componentsPerPixel) {
        case 1:
            if (bitsPerComponent == 8)
                srcDataFormat = kSourceFormatA8;
            else
                srcDataFormat = srcByteOrder16Big ? kSourceFormatA16Big :  kSourceFormatA16Little;
            break;
        case 2:
            if (bitsPerComponent == 8)
                srcDataFormat = kSourceFormatRA8;
            else
                srcDataFormat = srcByteOrder16Big ? kSourceFormatRA16Big :  kSourceFormatRA16Little;
            break;
        case 4:
            if (bitsPerComponent == 8)
                srcDataFormat = kSourceFormatRGBA8;
            else
                srcDataFormat = srcByteOrder16Big ? kSourceFormatRGBA16Big : kSourceFormatRGBA16Little;
            break;
        default:
            return false;
        }
        break;
    case kCGImageAlphaNoneSkipLast:
        switch (componentsPerPixel) {
        case 2:
            if (bitsPerComponent == 8)
                srcDataFormat = kSourceFormatRA8;
            else
                srcDataFormat = srcByteOrder16Big ? kSourceFormatRA16Big : kSourceFormatRA16Little;
            break;
        case 4:
            if (bitsPerComponent == 8)
                srcDataFormat = kSourceFormatRGBA8;
            else
                srcDataFormat = srcByteOrder16Big ? kSourceFormatRGBA16Big :  kSourceFormatRGBA16Little;
            break;
        default:
            return false;
        }
        break;
    case kCGImageAlphaNone:
        switch (componentsPerPixel) {
        case 1:
            if (bitsPerComponent == 8)
                srcDataFormat = kSourceFormatR8;
            else
                srcDataFormat = srcByteOrder16Big ? kSourceFormatR16Big : kSourceFormatR16Little;
            break;
        case 3:
            if (bitsPerComponent == 8)
                srcDataFormat = kSourceFormatRGB8;
            else
                srcDataFormat = srcByteOrder16Big ? kSourceFormatRGB16Big : kSourceFormatRGB16Little;
            break;
        default:
            return false;
        }
        break;
    default:
        return false;
    }
    RetainPtr<CFDataRef> pixelData;
    pixelData.adoptCF(CGDataProviderCopyData(CGImageGetDataProvider(cgImage)));
    if (!pixelData)
        return false;
    const UInt8* rgba = CFDataGetBytePtr(pixelData.get());
    outputVector.resize(width * height * 4);
    unsigned int srcUnpackAlignment = 0;
    size_t bytesPerRow = CGImageGetBytesPerRow(cgImage);
    unsigned int padding = bytesPerRow - bitsPerPixel / 8 * width;
    if (padding) {
        srcUnpackAlignment = padding + 1;
        while (bytesPerRow % srcUnpackAlignment)
            ++srcUnpackAlignment;
    }
    bool rt = packPixels(rgba, srcDataFormat, width, height, srcUnpackAlignment,
                         format, type, neededAlphaOp, outputVector.data());
    return rt;
}

void GraphicsContext3D::paintToCanvas(const unsigned char* imagePixels, int imageWidth, int imageHeight, int canvasWidth, int canvasHeight, CGContextRef context)
{
    if (!imagePixels || imageWidth <= 0 || imageHeight <= 0 || canvasWidth <= 0 || canvasHeight <= 0 || !context)
        return;
    int rowBytes = imageWidth * 4;
    RetainPtr<CGDataProviderRef> dataProvider(AdoptCF, CGDataProviderCreateWithData(0, imagePixels, rowBytes * imageHeight, 0));
    RetainPtr<CGColorSpaceRef> colorSpace(AdoptCF, CGColorSpaceCreateDeviceRGB());
    RetainPtr<CGImageRef> cgImage(AdoptCF, CGImageCreate(imageWidth, imageHeight, 8, 32, rowBytes, colorSpace.get(), kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Host,
        dataProvider.get(), 0, false, kCGRenderingIntentDefault));
    // CSS styling may cause the canvas's content to be resized on
    // the page. Go back to the Canvas to figure out the correct
    // width and height to draw.
    CGRect rect = CGRectMake(0, 0, canvasWidth, canvasHeight);
    // We want to completely overwrite the previous frame's
    // rendering results.
    CGContextSaveGState(context);
    CGContextSetBlendMode(context, kCGBlendModeCopy);
    CGContextSetInterpolationQuality(context, kCGInterpolationNone);
    CGContextDrawImage(context, rect, cgImage.get());
    CGContextRestoreGState(context);
}

} // namespace WebCore

#endif // ENABLE(3D_CANVAS)
