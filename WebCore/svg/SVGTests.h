/*
 * Copyright (C) 2004, 2005, 2008 Nikolas Zimmermann <zimmermann@kde.org>
 * Copyright (C) 2004, 2005, 2006 Rob Buis <buis@kde.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifndef SVGTests_h
#define SVGTests_h

#if ENABLE(SVG)
#include "SVGStringList.h"

namespace WebCore {

class Attribute;
class QualifiedName;

class SVGTests {
public:
    SVGStringList& requiredFeatures() { return m_features; }
    SVGStringList& requiredExtensions() { return m_extensions; }
    SVGStringList& systemLanguage() { return m_systemLanguage; }

    bool hasExtension(const String&) const;
    bool isValid() const;

    bool parseMappedAttribute(Attribute*);
    bool isKnownAttribute(const QualifiedName&);

protected:
    SVGTests();

private:
    SVGStringList m_features;
    SVGStringList m_extensions;
    SVGStringList m_systemLanguage;
};

} // namespace WebCore

#endif // ENABLE(SVG)
#endif // SVGTests_h