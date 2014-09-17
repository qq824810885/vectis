#include "customscrollbar.h"
#include <QPainter>
#include <QTextBlock>
#include <QTextLayout>
#include <QtCore/qmath.h>
#include <QDebug>
#include <QStyleOptionSlider>

CustomScrollBar::CustomScrollBar ( QTextEdit * parent ) :
    QScrollBar(parent),
    m_parent(parent),
    m_textLineHeight(1),
    m_internalLineCount(1)
{
    Q_ASSERT( parent );

    //setAttribute( Qt::WA_TranslucentBackground );
    setAttribute( Qt::WA_OpaquePaintEvent, false );
   // setStyle(new CustomStyle());
}

void CustomScrollBar::resizeEvent ( QResizeEvent * event ) {

    // Hierarchy used to find the parent QPlainTextEdit widget
    // QScrollBar >parent> qt_scrollarea_vcontainer >parent> QPlainTextEdit
    // As long as we don't insert frames or tables, blocks == lines
    QTextBlock block = m_parent->document()->findBlockByNumber(0);
    QTextLayout *layout = block.layout(); // Layout di una riga
    QTextLine textLine = layout->lineAt(0);
    m_textLineHeight = textLine.height();
    m_maxNumLines = qFloor(qreal(m_parent->height()) / m_textLineHeight);
    // Aggiorna anche il maximum per permettere di scrollare l'ultima riga fino all'inizio della view
    m_internalLineCount = (m_parent->document()->lineCount() - 1);
    setMaximum( m_internalLineCount * m_textLineHeight );
    //qDebug() << "resizeEvent: maximum aggiornato a: " << maximum();

    //qDebug() << "m_maxNumLines is now " << m_maxNumLines;

    qDebug() << "textLine.height() is  " << m_textLineHeight;

    QScrollBar::resizeEvent(event);
}

void CustomScrollBar::sliderChange ( SliderChange change ) {
    // Per poter simulare delle "righe vuote virtuali" alla fine e permettere di scrollare
    // l'ultima riga fino all'inizio della view è necessario rilevare quando cambia il valore dello
    // slider (e.g. sto scrollando o aggiungo/tolgo righe oppure wrappo) e aumentare il massimo dove si può scrollare
    m_internalLineCount = (m_parent->document()->lineCount() - 1);
    setMaximum( m_internalLineCount * m_textLineHeight );

    //qDebug() << "sliderChange: maximum aggiornato a: " << maximum();

    QAbstractSlider::sliderChange(change);
}
#include <QPlainTextEdit>
void CustomScrollBar::paintEvent ( QPaintEvent * event ) {

    QPainter p( this );

    QPlainTextEdit ea(m_parent); // TODO: come cazzo si può rilevare il numero di righe se ho il word wrapping????
    int lineCount = ea.document()->lineCount();
    if(m_internalLineCount != lineCount) {
        qDebug() << lineCount;
        m_internalLineCount = lineCount;
        setMaximum( m_internalLineCount * m_textLineHeight );
    }

    // Draw any scroll background - nota che per la trasparenza devi specificare come si blenda col background
    //p.setCompositionMode (QPainter::CompositionMode_Source);
    //p.fillRect( rc, QColor( 255, 255, 255, 50 ) );
    //p.setCompositionMode (QPainter::CompositionMode_SourceOver);

    // Calcolo la posizione dello slider
    //qDebug() << "value : " << value() << "maximum() :" << maximum();

    int extraBottomLines = (m_maxNumLines - 1); // Righe extra per scrollare il testo fino a visualizzare solo una riga
                                                // in alto

    // Dato che maximum() è SEMPRE maggiore di value() (il numero di linee del controllo è sempre
    // maggiore o uguale della prima riga visualizzata dalla view), posso esprimere il rapporto come
    // posizione_iniziale_slider = altezza_view * (riga_view / max_righe)
    float viewRelativePos = float(m_maxNumLines) * (float(value()) / float(maximum() + (extraBottomLines*m_textLineHeight)));


    // e ora trova la posizione assoluta nella rect del controllo
    // rect().height() : x = maxNumLines : viewRelativePos
    float rectAbsPos = (float(rect().height()) * viewRelativePos) / float(m_maxNumLines);

    //qDebug() << value();

    //qDebug() << "maxNumLines is " << maxNumLines << " and viewRelativePos is = " << viewRelativePos <<
    //            " rectAbsPos = " << rectAbsPos;

    // e ora calcolo la lunghezza della rect dello slider
    int currentLinesNum = m_parent->document()->lineCount();
    int lenSlider = int(float(rect().height()) * (float(m_maxNumLines) / float(currentLinesNum + extraBottomLines)));

    // imposta un minimo di lunghezza per lo slider ed evita che vada disegnato fuori
    if(lenSlider < 15)
        lenSlider = 15;

    if(rectAbsPos + lenSlider > rect().height())
        rectAbsPos -= (rectAbsPos + lenSlider) - rect().height();

    //qDebug() << lenSlider;

    // Disegno area dello slider
    QRect rcSlider(0, rectAbsPos, rect().width() - 1, lenSlider );
    //p.fillRect( rcSlider, QColor( 55, 4, 255, 100 ) );



    ////// Routines di disegno ///////

    // Disegna una linea di separazione di 1 px
    QPen lp(QColor(29,29,29));
    p.setPen(lp);
    p.drawLine(rect().left(), rect().top(), rect().left(), rect().bottom());

    // Leggero gradiente da sx a dx
    QLinearGradient bkGrad(rect().topLeft(), rect().topRight());
    bkGrad.setColorAt(0, QColor(33,33,33));
    bkGrad.setColorAt(1, QColor(50,50,50));
    QRect rc = rect();
    rc.setLeft(rc.left()+1);
    p.fillRect(rc, bkGrad);


    // Routine di disegno per lo slider
    // rcSlider è la hitbox, ma per il drawing ne prendiamo una sottosezione in larghezza
    QRect rcSliderSubsection(rcSlider);
    rcSliderSubsection.setX(rcSliderSubsection.x()+3);
    rcSliderSubsection.setWidth(rcSliderSubsection.width()-2);
    p.setRenderHint(QPainter::Antialiasing);
    QPainterPath path;
    path.setFillRule( Qt::WindingFill );
    path.addRoundedRect( rcSliderSubsection, 4, 4 );

    // seleziona un brush a gradiente
    QLinearGradient fillGrad(rect().topLeft(), rect().topRight());
    fillGrad.setColorAt(0, QColor(88,88,88));
    fillGrad.setColorAt(1, QColor(64,64,64));
    QBrush gradFill(fillGrad);
    p.setBrush(gradFill);


    //path.addRect( QRect( 200, 50, 50, 50 ) ); // Top right corner not rounded
        //path.addRect( QRect( 50, 100, 50, 50 ) ); // Bottom left corner not rounded
    p.drawPath( path.simplified() ); // Only Top left & bottom right corner rounded


/*    QPainter painter(this);


        QStyleOptionSlider option;
        initStyleOption(&option);

        option.subControls = QStyle::SC_All;


        style()->drawComplexControl(QStyle::CC_ScrollBar, &option, &painter, this);
*/
    //QScrollBar::paintEvent(event);
}
