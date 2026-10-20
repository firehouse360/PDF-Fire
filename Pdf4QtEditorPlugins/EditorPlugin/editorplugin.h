// MIT License
//
// Copyright (c) 2018-2026 Jakub Melka and Contributors
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

#ifndef EDITORSPLUGIN_H
#define EDITORSPLUGIN_H

#include "pdfplugin.h"
#include "pdffont.h"
#include "pdfpagecontentelements.h"
#include "pdfpagecontenteditortools.h"
#include "pdfpagecontenteditorprocessor.h"

#include <QObject>
#include <QPointer>
#include <QTimer>
#include <map>
#include <memory>
#include <tuple>
#include <vector>

namespace pdf
{
class PDFPageContentEditorWidget;
class PDFFireInlineTextEditor;
class PDFFireInlineParagraphEditor;
}

namespace pdfplugin
{

class EditorPlugin : public pdf::PDFPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "PDF4QT.EditorPlugin" FILE "EditorPlugin.json")

private:
    using BaseClass = pdf::PDFPlugin;

public:
    EditorPlugin();

    virtual void setWidget(pdf::PDFWidget* widget) override;
    virtual void setDocument(const pdf::PDFModifiedDocument& document) override;
    virtual std::vector<QAction*> getActions() const override;
    virtual QString getPluginMenuName() const override;
    virtual PluginMenuLocation getPluginMenuLocation() const override;
    virtual std::vector<QAction*> getToolbarActions() const override;

    /// Returns true, if the page content editing is active. The edited page
    /// content is written into the document when the editing is finished,
    /// so while it is active, the edits are not a part of the document.
    virtual bool hasUnwrittenChanges() const override;

    /// Writes the edited page content into the document. The user is not asked
    /// for a confirmation - the caller is responsible for it.
    virtual bool writeUnwrittenChanges() override;

    bool save();

private:
    void onSceneActivityChanged();
    void onSceneChanged(bool graphicsOnly);
    void onSceneSelectionChanged();
    void onWidgetSelectionChanged();
    void onToolActivityChanged();
    void onSceneEditElement(const std::set<pdf::PDFInteger>& elements);
    void onSceneEditSingleElement(pdf::PDFInteger elementId);

    /// Performs one step of the local editor undo history.
    void onUndoTriggered();

    /// Performs one step of the local editor redo history.
    void onRedoTriggered();

    void onPenChanged(const QPen& pen);
    void onBrushChanged(const QBrush& brush);
    void onFontChanged(const QFont& font);
    void onAlignmentChanged(Qt::Alignment alignment);
    void onTextAngleChanged(pdf::PDFReal angle);

    /// Turns on/off the creation of multiple elements by all the creation tools
    /// and stores the setting, so it is restored in the next session.
    void onCreateMultipleElementsTriggered(bool enabled);

    enum Action
    {
        // Activate action
        Activate,

        // PDF Fire: Edit Text - a click on a text edits it in place
        EditText,

        // Local undo/redo actions
        Undo,
        Redo,

        // Create graphics actions
        Text,
        FreehandCurve,
        AcceptMark,
        RejectMark,
        Rectangle,
        RoundedRectangle,
        HorizontalLine,
        VerticalLine,
        Line,
        Dot,
        SvgImage,

        // Settings of the creation tools
        CreateMultipleElements,

        Clear,

        LastAction
    };

    enum Tools
    {
        TextTool,
        FreehandCurveTool,
        AcceptMarkTool,
        RejectMarkTool,
        RectangleTool,
        RoundedRectangleTool,
        HorizontalLineTool,
        VerticalLineTool,
        LineTool,
        DotTool,
        ImageTool,
        LastTool
    };

    static constexpr int MAX_UNDO_STEPS = 20;
    static constexpr int MAX_REDO_STEPS = 20;

    void setActive(bool active);
    void onSetActive(bool active);

    void updateActions();
    void updateGraphics();

    /// Reads the settings of the plugin, which are not a part of the application
    /// settings (the creation of multiple elements).
    void readSettings();

    /// Writes the settings of the plugin.
    void writeSettings();

    void updateDockWidget();
    void updateEditedPages();

    /// Clears both local undo and redo stacks.
    void clearUndoRedo();

    /// Initializes local undo/redo history from the current scene state.
    void initializeUndoRedo();

    /// Appends the current scene state to the local undo stack.
    void createUndoStep();

    /// Creates a deep copy of a move-only scene snapshot.
    /// \param state Source scene snapshot
    /// \return Cloned snapshot that can be safely restored later
    pdf::PDFPageContentScene::SceneState copySceneState(const pdf::PDFPageContentScene::SceneState& state) const;

    /// Writes the edited page content of all edited pages into the document.
    /// The editing session is finished - the scene and the undo/redo history
    /// are cleared and the modified document is sent to the application.
    /// \returns False, if the content was not written into the document
    bool writePageContentToDocument();

    bool updatePageContent(pdf::PDFInteger pageIndex,
                           const std::vector<const pdf::PDFPageContentElement*>& elements,
                           pdf::PDFDocumentBuilder* builder);
    bool updateTextElement(pdf::PDFPageContentElementEdited* element);

    /// PDF Fire: edits the text of the element in place (a caret in the text, like
    /// a word processor). Returns false, when the text cannot be edited in place
    /// (rotated text, no text) - the edit dialog is used then.
    bool startInlineTextEdit(pdf::PDFPageContentElement* element);

    /// PDF Fire: a line of the text on a page, as the paragraph editing needs it
    struct InlineLine
    {
        pdf::PDFInteger id = -1;
        pdf::PDFInteger pageIndex = -1;
        QRectF rect;            ///< Bounding box in the page coordinates
        QString text;
        QByteArray fontName;
        pdf::PDFFontPointer font;

        /// PDF Fire: a line written as several text objects (Word starts a new one,
        /// where the font changes) is edited as one text - the elements after the first
        /// one, and the first element with the texts joined (see joinLineFragments)
        std::vector<pdf::PDFInteger> fragmentIds;
        std::shared_ptr<const pdf::PDFPageContentElement> joinedElement;

        /// All the elements of the line
        std::vector<pdf::PDFInteger> getElementIds() const;
        bool containsElement(pdf::PDFInteger elementId) const;
    };

    /// PDF Fire: a paragraph - lines with the same left margin, at the same distance
    struct Paragraph
    {
        std::vector<InlineLine> lines;  ///< From the top
        qreal bodyLeft = 0.0;           ///< Left margin (without the indentation of the first line)
        qreal maxRight = 0.0;           ///< Right edge of the column
        qreal pitch = 0.0;              ///< Distance of the lines
    };

    bool getInlineLine(const pdf::PDFPageContentElement* element, InlineLine* line) const;

    /// PDF Fire: returns the lines of the page (the texts of one line written as several
    /// text objects are joined into one line)
    std::vector<InlineLine> getPageLines(pdf::PDFInteger pageIndex) const;

    /// PDF Fire: returns the line of the page, which contains the element
    bool getJoinedInlineLine(const pdf::PDFPageContentElement* element, InlineLine* line) const;
    bool findParagraph(const pdf::PDFPageContentElement* element, Paragraph* paragraph) const;
    bool startInlineParagraphEdit(pdf::PDFPageContentElement* element);
    QFont getInlineEditorFont(const pdf::PDFFontPointer& pdfFont);

    /// PDF Fire: the result of an in-place edit of a text - the changed, the added and
    /// the removed elements. It is both the live preview and the edit itself, so the
    /// preview shows exactly, what is written.
    struct TextEditResult
    {
        std::vector<std::shared_ptr<const pdf::PDFPageContentElement>> replacedElements;
        std::vector<std::shared_ptr<const pdf::PDFPageContentElement>> addedElements;
        std::vector<pdf::PDFInteger> removedElementIds;
        QStringList errors;     ///< Errors of writing the text (a character with no glyph in any font)
        bool isFailed = false;  ///< A line could not be written
    };

    /// PDF Fire: the text, which is being edited in place
    struct InlineEditSession
    {
        bool isActive = false;
        bool isParagraph = false;
        Paragraph paragraph;        ///< The edited lines (one line, if a line is edited alone)
        QStringList originalLines;  ///< Texts of the lines, as the editor shows them
        QFont editorFont;           ///< Font of the editor of one line (without the letter spacing)
        QRectF deviceTextRect;      ///< The original text in the widget coordinates (one line)
    };

    /// PDF Fire: a changed line, as it is written (cached, the preview and the edit share it)
    struct ChangedLine
    {
        std::shared_ptr<const pdf::PDFPageContentElement> element;
        QStringList errors;
    };
    using ChangedLineKey = std::tuple<pdf::PDFInteger, qreal, qreal, QString>;

    /// PDF Fire: returns the line \p sourceId with the text \p text, moved by \p offset
    /// (an added line of a paragraph is a moved copy of the last line), or nullptr
    const ChangedLine* getChangedLine(const InlineLine& sourceLine, const QString& text, QPointF offset);

    /// PDF Fire: creates the edit of the text of the session to the given lines
    TextEditResult createInlineEdit(const QStringList& newLines);

    /// PDF Fire: applies the edit to the scene (one step of undo). Returns false, if
    /// the edit cannot be applied - the scene is not changed then.
    bool applyTextEditResult(const TextEditResult& result);

    /// PDF Fire: starts the session of the in-place editing, connects the editor
    void beginInlineEdit(InlineEditSession session, QWidget* editor);

    /// PDF Fire: the text of the editor of the session (lines)
    QStringList getInlineEditorLines() const;

    /// PDF Fire: shows the edited text with the glyphs of the document in the page,
    /// as it will be written (the editor shows the caret and the selection only)
    void updateInlinePreview();

    /// PDF Fire: the editor committed the text - the edit is applied, or the editor
    /// stays open with the typed text, if the edit could not be applied
    void onInlineEditCommitted(const QStringList& lines);

    /// PDF Fire: ends the session (the editor is removed, the preview is cleared)
    void endInlineEdit();

    /// PDF Fire: finishes (commits) the editing in place, if any; returns false, if
    /// the editor stays open (the edit could not be applied)
    bool finishInlineEdit();

    /// PDF Fire: the Edit Text tool was switched on or off
    void onEditTextTriggered(bool checked);
    void setTextEditMode(bool enabled);
    void onTextElementHovered(pdf::PDFInteger elementId);
    void updateTextOutlines();

    /// PDF Fire: the rectangle of the text, which is edited by a click on the element
    /// (its paragraph, or the element alone), in the page coordinates
    QRectF getEditableTextRect(const pdf::PDFPageContentElement* element) const;

    /// PDF Fire: refreshes the text element without asking the user (errors are returned)
    bool refreshTextElement(pdf::PDFPageContentElementEdited* element, QStringList* errors);

    /// PDF Fire: asks the user, if the errors of writing of the content stream are
    /// acceptable. Returns false, if the user aborts.
    bool confirmContentStreamErrors(QStringList errors, pdf::PDFInteger pageIndex);

    void onDrawSpaceChanged();

    pdf::PDFWidgetTool* getActiveTool();

    std::array<QAction*, LastAction> m_actions;
    std::array<pdf::PDFWidgetTool*, LastTool> m_tools;
    pdf::PDFPageContentEditorWidget* m_editorWidget;

    pdf::PDFPageContentScene m_scene;
    std::map<pdf::PDFInteger, pdf::PDFEditedPageContent> m_editedPageContent;
    bool m_sceneSelectionChangeEnabled;
    bool m_isSaving;
    /// True while a local undo/redo restore is in progress.
    bool m_isUndoRedoInProgress;
    /// Timeline of scene snapshots used by the local undo action.
    std::vector<pdf::PDFPageContentScene::SceneState> m_undoStates;
    /// Timeline of scene snapshots used by the local redo action.
    std::vector<pdf::PDFPageContentScene::SceneState> m_redoStates;
    QPointer<pdf::PDFFireInlineTextEditor> m_inlineEditor;
    QPointer<pdf::PDFFireInlineParagraphEditor> m_inlineParagraphEditor;

    // PDF Fire: the in-place editing of the text
    InlineEditSession m_inlineSession;
    std::map<ChangedLineKey, ChangedLine> m_changedLines;
    QTimer m_previewTimer;
    pdf::PDFInteger m_hoveredTextElementId = pdf::PDFPageContentScene::INVALID_ELEMENT_ID;
    bool m_isCommittingInlineEdit = false;
    bool m_isSceneActivatedByEditText = false;  ///< The page content editing was switched on by the Edit Text tool
};

}   // namespace pdfplugin

#endif // EDITORSPLUGIN_H
