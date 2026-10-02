// MIT License
//
// Copyright (c) 2026 PDF Fire contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#ifndef PDFFIRETHEME_H
#define PDFFIRETHEME_H

#include "pdfviewerglobal.h"

#include <QColor>

namespace pdfviewer
{

/// The look of PDF Fire - the palette and the style sheet of the application,
/// in a dark and in a light variant. The variant follows the color scheme of
/// the application (see pdf::PDFWidgetUtils::setDarkTheme), so the theme must
/// be applied after the color scheme is decided.
class PDF4QTLIBGUILIBSHARED_EXPORT PDFFireTheme
{
public:
    /// Applies the theme to the application
    static void apply();

    /// Accent color, taken from the flame of the application logo
    static QColor getAccentColor();
};

}   // namespace pdfviewer

#endif // PDFFIRETHEME_H
