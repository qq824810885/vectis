#include <UI/CodeTextEdit/CodeTextEdit.h>
#include <QPainter>
#include <QResizeEvent>

#include <QDebug>
#include <QElapsedTimer>

CodeTextEdit::CodeTextEdit(QWidget *parent) :
  QAbstractScrollArea(parent),
  m_document(nullptr)
{

  Q_ASSERT(parent);

  // WA_OpaquePaintEvent specifies that we'll redraw the control every time it is needed without
  // any system intervention
  // WA_NoSystemBackground avoids the system to draw the background (we'll handle it as well)
  setAttribute( Qt::WA_OpaquePaintEvent, true );
  setAttribute( Qt::WA_NoSystemBackground, true );
  setFrameShape( QFrame::NoFrame ); // No widget border allowed (otherwise there would be a separation
                                    // line that doesn't allow this control to blend in with tabs)
  setStyleSheet( "QWidget { background-color: rgb(22,23,19);     \
                            padding: 0px; }" ); // Also eliminate padding (needed to avoid QScrollBar spaces)

  // Create the vertical scrollbar and set it as "always on"
  m_verticalScrollBar = std::make_unique<ScrollBar>( this );
  this->setVerticalScrollBar( m_verticalScrollBar.get() );
  this->setVerticalScrollBarPolicy( Qt::ScrollBarAlwaysOn );

  // Default values for mouse wheel and pgUp/Down
  this->verticalScrollBar()->setSingleStep(2);
  this->verticalScrollBar()->setPageStep(65);

  // Set a font to use in this control
  // Consolas is installed by default on every Windows system, but not Linux. On Linux the
  // preferred one is Monospace. Anyway Qt's matching engine will try to find either or a
  // replacement monospace font
#ifdef _WIN32
  m_monospaceFont.setFamily( "Consolas" );
  m_monospaceFont.setPixelSize(14);
#else
  m_monospaceFont.setFamily( "Monospace" );
#endif
  m_monospaceFont.setStyleHint( QFont::Monospace );
  setFont( m_monospaceFont );

  // Stores the width of a single character in pixels with the given font (cache this value for
  // every document to use it)
  m_characterWidthPixels = fontMetrics().width('A');

  m_renderingThread = std::make_unique<RenderingThread>(*this);
  connect(m_renderingThread.get(), SIGNAL(documentSizeChangedFromThread(const QSizeF&, const qreal)),
          this, SLOT(documentSizeChangedFromThread(const QSizeF&, const qreal)));
}

void CodeTextEdit::unloadDocument() {
  if (m_renderingThread->isRunning() == true)
    m_renderingThread->wait(); // Wait for all drawing operations to finish

  m_documentMutex.lock();
  m_document = nullptr;
  m_documentPixmap.release();
  // WARNING: the rendering thread should NEVER be stopped while the CodeTextEdit control is running
  // [NO] m_renderingThread.release();
  m_documentMutex.unlock();
  emit documentSizeChanged( QSizeF(), fontMetrics().height(), 0 );
  repaint();
}

void CodeTextEdit::loadDocument(Document *doc, int VScrollbarPos) {

  if (m_renderingThread->isRunning() == true)
    m_renderingThread->wait(); // Wait for all drawing operations to finish


  m_documentMutex.lock();
  m_document = doc;

  // Save the scrollbar position if we have one
  m_document->m_storeSliderPos = VScrollbarPos;

  // Calculate the new document size
  m_document->recalculateDocumentLines();

  // Set a new pixmap for rendering this document ~ caveat: this is NOT the viewport dimension
  // since everything needs to be rendered, not just the viewport region
  m_documentPixmap = std::make_unique<QImage>(viewport()->width(), m_document->m_numberOfEditorLines *
                                              fontMetrics().height() + 20 /* Remember to compensate the offset */,
                                              QImage::Format_ARGB32_Premultiplied);
  m_documentMutex.unlock();

  m_messageQueueMutex.lock();
  m_documentUpdateMessages.emplace_back( viewport()->width(), viewport()->width(), m_document->m_numberOfEditorLines *
                                         fontMetrics().height() + 20 /* Remember to compensate the offset */);
  m_messageQueueMutex.unlock();

  if( m_renderingThread->isRunning() == false )
      m_renderingThread->start();  

//  QSizeF newSize;
//  newSize.setHeight( m_document->m_numberOfEditorLines );
//  newSize.setWidth ( m_document->m_maximumCharactersLine );

//  // Emit a documentSizeChanged signal. This will trigger scrollbars resizing
//  emit documentSizeChanged( newSize, fontMetrics().height() );

//  m_verticalScrollBar->setSliderPosition(0);

//  this->viewport()->repaint(); // Trigger a cache invalidation for the viewport (necessary)
}

int CodeTextEdit::getViewportWidth() const {
  return this->viewport()->width();
}

int CodeTextEdit::getCharacterWidthPixels() const {
  return this->m_characterWidthPixels;
}

// As the name suggests: render the entire document on the internal stored pixmap
void CodeTextEdit::renderDocumentOnPixmap() {

  //QElapsedTimer timer;
  //timer.start();

  m_backgroundBufferPixmap->fill(Qt::transparent); // Set the pixmap transparent

  QPainter painter(m_backgroundBufferPixmap.get()); // Draw into the pixmap
  painter.setFont(this->font()); // And grab the widget's monospace font

  // Drawing the background is not needed since it is already transparent (the background only
  // needs to be set on the viewport)
  // const QBrush backgroundBrush(QColor(39, 40, 34));
  // painter.setBrush(backgroundBrush);
  // painter.fillRect(m_documentPixmap->rect(), backgroundBrush);

  painter.setPen(QPen(Qt::white)); // A classic Monokai style
  auto setColor = [&painter](Style s) {
    switch(s) {
    case Comment: {
      painter.setPen(QPen(QColor(117,113,94))); // Gray-ish
    } break;
    case Keyword: {
      painter.setPen(QPen(QColor(249,38,114))); // Pink-ish
    } break;
    case QuotedString: {
      painter.setPen(QPen(QColor(230,219,88))); // Yellow-ish
    } break;
    case Identifier: {
      painter.setPen(QPen(QColor(166,226,46))); // Green-ish
    } break;
    case KeywordInnerScope:
    case FunctionCall: {
      painter.setPen(QPen(QColor(102,217,239))); // Light blue
    } break;
    case Literal: {
      painter.setPen(QPen(QColor(174,129,255))); // Purple-ish
    } break;
    default: {
      painter.setPen(QPen(Qt::white));
    } break;
    };
  };

  QPointF startpoint(5, 20);
  size_t documentRelativePos = 0;
  size_t lineRelativePos = 0;
  auto styleIt = m_document->m_styleDb.styleSegment.begin();
  auto styleEnd = m_document->m_styleDb.styleSegment.end();
  size_t nextDestination = -1;

  auto calculateNextDestination = [&]() {
    // We can have 2 cases here:
    // 1) Our position hasn't still reached a style segment (apply regular style and continue)
    // 2) Our position is exactly on the start of a style segment (apply segment style and continue)
    // If there are no other segments, use a regular style and continue till the end of the lines

    if (styleIt == styleEnd) { // No other segments
      nextDestination = -1;
      setColor( Normal );
      return;
    }

    if(styleIt->start > documentRelativePos) { // Case 1
      setColor( Normal );
      nextDestination = styleIt->start;
    } else if (styleIt->start == documentRelativePos) { // Case 2
      setColor( styleIt->style );
      nextDestination = styleIt->start + styleIt->count;
      ++styleIt; // This makes sure our document relative position is never ahead of a style segment
    }
  };

  // First time we don't have a destination set, just find one (if there's any)
  calculateNextDestination();

  // Implement the main rendering loop algorithm which renders characters segment by segment
  // on the viewport area
  for(auto& pl : m_document->m_physicalLines) {

    size_t editorLineIndex = 0; // This helps tracking the last EditorLine of a PhysicalLine
    for(auto& el : pl.m_editorLines) {
      ++editorLineIndex;

      do {
        startpoint.setX( 5 + lineRelativePos * m_characterWidthPixels );

        // If we don't have a destination OR we can't reach it within our line, just draw the entire line and continue
        if (nextDestination == -1 ||
            nextDestination > documentRelativePos + (el.m_characters.size() - lineRelativePos)) {

          // Multiple lines will have to be rendered, just render this till the end and continue

          int charsRendered = 0;
          if (el.m_characters.size() > 0) { // Empty lines must be skipped
            QString ts(el.m_characters.data() + lineRelativePos, static_cast<int>(el.m_characters.size() - lineRelativePos));
            painter.drawText(startpoint, ts);
            charsRendered = ts.size();
          }

          lineRelativePos = 0; // Next editor line will just start from the beginning
          documentRelativePos += charsRendered + /* Plus a newline if a physical line ended (NOT an EditorLine) */
              (editorLineIndex == pl.m_editorLines.size() ? 1 : 0);

          break; // Go and fetch a new line for the next cycle
        } else {

          // We can reach the goal within this line

          int charsRendered = 0;
          if (el.m_characters.size() > 0) { // Empty lines must be skipped
            QString ts(el.m_characters.data() + lineRelativePos, static_cast<int>(nextDestination - documentRelativePos));
            painter.drawText(startpoint, ts);
            charsRendered = ts.size();
          }

          bool goFetchNewLine = false; // If this goal also exhausted the current editor line, go fetch
                                       // another one
          bool addNewLine = false; // If this was the last editor line, also add a newline because it
                                   // corresponds to a new physical line starting
          if(nextDestination - documentRelativePos + lineRelativePos == el.m_characters.size()) {
            goFetchNewLine = true;

            // Do not allow EditorLine to insert a '\n'. They're virtual lines
            if (editorLineIndex == pl.m_editorLines.size())
              addNewLine = true;

            lineRelativePos = 0; // Next editor line will just start from the beginning
          } else
            lineRelativePos += charsRendered;

          documentRelativePos += charsRendered + (addNewLine ? 1 : 0); // Just add a newline if we also reached this line's
                                                                       // end AND a physical line ended, not an EditorLine

          calculateNextDestination(); // Need a new goal

          if( goFetchNewLine )
            break; // Go fetch a new editor line (possibly on another physical line),
                   // we exhausted this editor line
        }

      } while(true);

      // Move the rendering cursor (carriage-return)
      startpoint.setY(startpoint.y() + fontMetrics().height());
    }
  }

  //m_invalidatedPixmap = false; // QPixmap has been redrawn

  m_documentMutex.lock();
  m_documentPixmap.swap(m_backgroundBufferPixmap);
  m_documentMutex.unlock();
  //qDebug() << "Done rendering document lines in " << timer.elapsed() << " milliseconds";
}



void CodeTextEdit::paintEvent (QPaintEvent *) {

  QPainter view(viewport());

  // Draw control background (this blends with the TabsBar selected tab's bottom color)
  const QBrush backgroundBrush(QColor(39, 40, 34));
  view.setBrush(backgroundBrush);
  view.fillRect(rect(), backgroundBrush);

  m_documentMutex.lock();
  if (m_document != nullptr) {
    // Apply the offset and draw the pixmap on the viewport
    int scrollOffset = m_sliderValue * fontMetrics().height();
    QRectF pixmapRequestedRect(m_documentPixmap->rect().x(), m_documentPixmap->rect().y() + scrollOffset,
                               m_documentPixmap->rect().width(), viewport()->height());
    QRectF myViewRect = viewport()->rect();
    myViewRect.setWidth(pixmapRequestedRect.width());

    view.drawImage(myViewRect, *m_documentPixmap, pixmapRequestedRect);
    //view.drawPixmap (myViewRect, *m_documentPixmap, pixmapRequestedRect);
  }
  m_documentMutex.unlock();
}
void CodeTextEdit::resizeEvent (QResizeEvent *evt) {

  if (m_document == nullptr)
    return;

  m_messageQueueMutex.lock();
  m_documentMutex.lock();
  m_documentUpdateMessages.emplace_back( evt->size().width(), viewport()->width(), m_document->m_numberOfEditorLines *
                                         fontMetrics().height() + 20 /* Remember to compensate the offset */ );
  m_documentMutex.unlock();
  m_messageQueueMutex.unlock();

  if( m_renderingThread->isRunning() == false )
      m_renderingThread->start();
}

void CodeTextEdit::verticalSliderValueChanged (int value) {
  // This method is called each time there's a change in the vertical slider and we need to refresh the view
  m_sliderValue = value;
  repaint();
}

void CodeTextEdit::documentSizeChangedFromThread(const QSizeF &newSize, const qreal lineHeight) {
  emit documentSizeChanged( newSize, lineHeight, m_document->m_storeSliderPos ); // Forward the signal to our QScrollBar

//  // Since the thread updated the document size entirely, we're not able to adjust the scroll rate (mouse wheel and pgUp/Down)
//  // to better fit the number of lines of the document

//  // A document of 100 lines should be scrolled by 3 lines per scroll
//  m_document->m_numberOfEditorLines
//  this->verticalScrollBar()->setSingleStep(5);
//  this->verticalScrollBar()->setPageStep(50);

  this->repaint();
}

///////////////////////////////////////////////
///            RenderingThread              ///
///////////////////////////////////////////////

void RenderingThread::run() {

  while(getFrontElement() == true) {
    m_cte.m_documentMutex.lock();
    m_cte.m_document->setWrapWidth(m_currentElement.wrapWidth);
    m_cte.m_documentMutex.unlock();

    m_cte.m_backgroundBufferPixmap = std::make_unique<QImage>(m_currentElement.bufferWidth,
                                                              m_currentElement.bufferHeight,
                                                              QImage::Format_ARGB32_Premultiplied);    

    m_cte.renderDocumentOnPixmap();

    // Even if the document's size might not have changed, we still need to fire a documentSizeChanged
    // event since scrollbars use this also to calculate the maximum number of lines our viewport can display
    QSizeF newSize;
    qreal lineHeight = m_cte.fontMetrics().height();
    newSize.setHeight( m_cte.m_document->m_numberOfEditorLines );
    newSize.setWidth ( m_cte.m_document->m_maximumCharactersLine );

    // Emit a documentSizeChanged signal. This will trigger scrollbars 'maxViewableLines' calculations
    emit documentSizeChangedFromThread ( newSize, lineHeight );
  }
}

bool RenderingThread::getFrontElement() {
  bool elementFound = false;
  m_cte.m_messageQueueMutex.lock();
  if (m_cte.m_documentUpdateMessages.size() > 0) {
    m_currentElement = m_cte.m_documentUpdateMessages.back();
    m_cte.m_documentUpdateMessages.clear(); // We're ONLY interested in the front object
    elementFound = true;
  }
  m_cte.m_messageQueueMutex.unlock();
  return elementFound;
}
