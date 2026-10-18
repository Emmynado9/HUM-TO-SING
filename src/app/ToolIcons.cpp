#include "ToolIcons.hpp"
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
namespace hum::app {
QIcon toolIcon(ToolIcon symbol, bool light) {
    QIcon icon;
    for (const auto checked : {false, true}) {
        QPixmap pixmap(64,64); pixmap.fill(Qt::transparent); pixmap.setDevicePixelRatio(2);
        QPainter p(&pixmap); p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen((checked || light) ? Qt::white : QColor("#494b50"), 2.1, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(Qt::NoBrush);
        const auto line = [&](int x,int y,int x2,int y2){p.drawLine(x,y,x2,y2);};
        switch (symbol) {
        case ToolIcon::paint: {
            QPainterPath path; path.moveTo(13,18); path.lineTo(23,5); path.quadTo(26,3,27,7);
            path.lineTo(18,21); path.closeSubpath(); p.drawPath(path);
            QPainterPath bristle; bristle.moveTo(13,18); bristle.cubicTo(6,17,10,26,4,26);
            bristle.cubicTo(16,30,20,24,18,21); p.drawPath(bristle); break;
        }
        case ToolIcon::select: p.drawPolygon(QPolygonF{{7,5},{26,14},{18,18},{14,27},{7,5}}); break;
        case ToolIcon::resize:
            line(5,16,27,16); line(5,16,10,11); line(5,16,10,21);
            line(27,16,22,11); line(27,16,22,21); break;
        case ToolIcon::erase:
            p.drawPolygon(QPolygonF{{5,20},{18,6},{27,15},{15,27},{11,27},{5,20}});
            line(10,15,20,24); line(12,27,28,27); break;
        case ToolIcon::mute: case ToolIcon::speaker:
            p.drawPolygon(QPolygonF{{5,12},{10,12},{17,6},{17,26},{10,20},{5,20},{5,12}});
            if (symbol == ToolIcon::mute) {line(23,12,29,20);line(29,12,23,20);}
            else {p.drawArc(QRectF(15,8,12,16),-60*16,120*16);}
            break;
        case ToolIcon::slice:
            p.drawEllipse(QPointF(8,8),3,3); p.drawEllipse(QPointF(8,24),3,3);
            line(11,10,27,25); line(11,22,27,6); break;
        case ToolIcon::zoom:
            p.drawEllipse(QPointF(13,13),8,8); line(19,19,28,28);line(9,13,17,13);line(13,9,13,17);break;
        case ToolIcon::snap: {
            QPainterPath path; path.moveTo(8,7); path.lineTo(8,18); path.cubicTo(8,29,24,29,24,18);
            path.lineTo(24,7);path.lineTo(18,7);path.lineTo(18,18);path.cubicTo(18,21,14,21,14,18);
            path.lineTo(14,7);path.closeSubpath();p.drawPath(path);line(8,12,14,12);line(18,12,24,12);break;
        }
        case ToolIcon::microphone:
            p.drawRoundedRect(QRectF(12,3,8,17),4,4); p.drawArc(QRectF(7,8,18,17),180*16,180*16);
            line(16,25,16,29);line(11,29,21,29);break;
        case ToolIcon::piano:
            p.drawRoundedRect(QRectF(3,6,26,22),2,2);line(12,6,12,28);line(21,6,21,28);
            p.fillRect(QRectF(9,6,5,12),p.pen().color());p.fillRect(QRectF(18,6,5,12),p.pen().color());break;
        case ToolIcon::pitch:
            p.drawPolyline(QPolygonF{{3,16},{7,16},{11,5},{15,27},{19,10},{23,20},{27,16},{30,16}});break;
        case ToolIcon::play: p.drawPolygon(QPolygonF{{9,5},{27,16},{9,27},{9,5}});break;
        case ToolIcon::pause: p.drawRect(QRectF(8,6,5,20));p.drawRect(QRectF(20,6,5,20));break;
        case ToolIcon::loop: case ToolIcon::loopAll:
            p.drawPolyline(QPolygonF{{6,18},{6,8},{26,8},{22,4}});
            p.drawPolyline(QPolygonF{{26,14},{26,24},{6,24},{10,28}});
            if (symbol == ToolIcon::loopAll) { auto f=p.font();f.setPixelSize(11);f.setBold(true);p.setFont(f);p.drawText(QRectF(10,10,12,12),Qt::AlignCenter,QStringLiteral("A")); }
            break;
        case ToolIcon::loopRange:
            p.drawPolyline(QPolygonF{{9,5},{5,5},{5,27},{9,27}});
            p.drawPolyline(QPolygonF{{23,5},{27,5},{27,27},{23,27}});
            line(10,16,22,16);line(10,16,14,12);line(22,16,18,20);break;
        case ToolIcon::stop: p.drawRoundedRect(QRectF(7,7,18,18),2,2);break;
        case ToolIcon::exportMidi:
            line(16,4,16,21);line(10,15,16,21);line(16,21,22,15);
            p.drawPolyline(QPolygonF{{5,22},{5,28},{27,28},{27,22}});break;
        case ToolIcon::addTrack:
            line(5,7,20,7);line(5,15,20,15);line(5,23,15,23);line(24,19,24,29);line(19,24,29,24);break;
        case ToolIcon::duplicate:
            p.drawRoundedRect(QRectF(10,10,17,17),2,2);p.drawPolyline(QPolygonF{{6,22},{4,22},{4,4},{22,4},{22,6}});break;
        case ToolIcon::undo: case ToolIcon::redo: {
            if(symbol==ToolIcon::redo){p.translate(32,0);p.scale(-1,1);}
            QPainterPath path;path.moveTo(7,12);path.cubicTo(27,7,30,25,12,26);p.drawPath(path);
            p.drawPolyline(QPolygonF{{11,5},{5,12},{12,18}});break;
        }
        case ToolIcon::metronome:
            p.drawPolygon(QPolygonF{{11,4},{21,4},{27,28},{5,28},{11,4}});line(16,23,25,8);line(11,23,21,23);break;
        }
        p.end();
        icon.addPixmap(pixmap,QIcon::Normal,checked?QIcon::On:QIcon::Off);
    }
    return icon;
}
}
