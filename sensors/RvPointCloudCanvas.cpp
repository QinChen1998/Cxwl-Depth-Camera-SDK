#include "RvPointCloudCanvas.h"
#include <QImage>
#include <QMatrix4x4>
#include <QMouseEvent>
#include <QPainter>
#include <QQuaternion>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <limits>

namespace {
constexpr double Pi = 3.14159265358979323846;
struct ScreenPoint { double x, y, z; QVector4D color; float diameter; };
QRgb rgb(const QVector4D &c) {
    return qRgb(qBound(0, qRound(c.x() * 255), 255), qBound(0, qRound(c.y() * 255), 255),
                qBound(0, qRound(c.z() * 255), 255));
}
}
RvPointCloudCanvas::RvPointCloudCanvas(QWidget *parent) : QWidget(parent) {
    setAttribute(Qt::WA_OpaquePaintEvent);
    setMinimumSize(300, 190);
}
void RvPointCloudCanvas::setFrame(const QVector<Point> &points, const QVector<quint32> &indices, bool mesh, int angle) {
    m_points = points; m_indices = indices; m_mesh = mesh; m_inputAngle = angle; update();
}
void RvPointCloudCanvas::clearFrame() { m_points.clear(); m_indices.clear(); update(); }
void RvPointCloudCanvas::resetView() {
    m_target = {0, 0, -600}; m_distance = 900; m_yaw = -25; m_pitch = -18; update();
}
QImage RvPointCloudCanvas::renderFrame() const {
    const double dpr = devicePixelRatioF();
    const int w = std::max(1, qRound(width() * dpr)), h = std::max(1, qRound(height() * dpr));
    QImage image(w, h, QImage::Format_RGB32);
    image.fill(QColor("#161b22"));
    if (m_points.isEmpty()) { image.setDevicePixelRatio(dpr); return image; }
    QVector<float> zBuffer(w * h, std::numeric_limits<float>::infinity());
    const double focal = h / (2 * std::tan(Pi / 8));
    const double nearPlane = std::max(1., m_distance / 2000);
    const QQuaternion orbit = QQuaternion::fromEulerAngles(float(m_pitch), float(m_yaw), 0);
    const QVector3D eye = m_target + orbit.rotatedVector({0, 0, float(m_distance)});
    const auto project = [&](const QVector3D &p) {
        const auto q = orbit.conjugated().rotatedVector(p - eye);
        const double z = -q.z();
        return ScreenPoint{w / 2. + q.x() * focal / z, h / 2. - q.y() * focal / z, z, {}, 0};
    };
    QVector<ScreenPoint> screen;
    screen.reserve(m_points.size());
    for (const auto &p : m_points) {
        auto v = project(p.position); v.color = p.color; v.diameter = p.diameter; screen.append(v);
    }
    const auto write = [&](int x, int y, double z, QRgb color) {
        auto &old = zBuffer[y * w + x];
        if (z < old) { old = float(z); reinterpret_cast<QRgb *>(image.scanLine(y))[x] = color; }
    };
    if (!m_mesh) {
        for (const auto &p : screen) {
            if (p.z <= nearPlane) continue;
            const double half = std::max(1., p.diameter * focal / p.z * .5);
            // Reject off-screen primitives before conversion to bounded integer pixel coordinates.
            if (p.x + half < 0 || p.y + half < 0 || p.x - half >= w || p.y - half >= h) continue;
            const int left = int(std::max(0., std::floor(p.x - half)));
            const int right = int(std::min(double(w - 1), std::ceil(p.x + half)));
            const int top = int(std::max(0., std::floor(p.y - half)));
            const int bottom = int(std::min(double(h - 1), std::ceil(p.y + half)));
            for (int y = top; y <= bottom; ++y)
                for (int x = left; x <= right; ++x) write(x, y, p.z, rgb(p.color));
        }
    } else {
        // Perspective-correct interpolation and a Z buffer preserve occlusion and smooth mesh color.
        for (qsizetype i = 0; i + 2 < m_indices.size(); i += 3) {
            const auto &a = screen[m_indices[i]], &b = screen[m_indices[i+1]], &c = screen[m_indices[i+2]];
            if (a.z <= nearPlane || b.z <= nearPlane || c.z <= nearPlane) continue;
            const double den = (b.y-c.y)*(a.x-c.x)+(c.x-b.x)*(a.y-c.y);
            if (std::abs(den) < 1e-9) continue;
            const double minX = std::min({a.x,b.x,c.x}), maxX = std::max({a.x,b.x,c.x});
            const double minY = std::min({a.y,b.y,c.y}), maxY = std::max({a.y,b.y,c.y});
            if (maxX < 0 || maxY < 0 || minX >= w || minY >= h) continue;
            const int left = int(std::max(0., std::floor(minX))), right = int(std::min(double(w-1),std::ceil(maxX)));
            const int top = int(std::max(0.,std::floor(minY))), bottom = int(std::min(double(h-1),std::ceil(maxY)));
            for (int y=top; y<=bottom; ++y) for (int x=left; x<=right; ++x) {
                const double u = ((b.y-c.y)*(x+.5-c.x)+(c.x-b.x)*(y+.5-c.y))/den;
                const double v = ((c.y-a.y)*(x+.5-c.x)+(a.x-c.x)*(y+.5-c.y))/den;
                const double t = 1-u-v;
                if (std::min({u,v,t}) < -1e-7) continue;
                const double inverseZ = u/a.z+v/b.z+t/c.z;
                const auto color = (a.color*float(u/a.z)+b.color*float(v/b.z)+c.color*float(t/c.z))/float(inverseZ);
                write(x,y,1/inverseZ,rgb(color));
            }
        }
    }
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    const auto origin = project({});
    if (origin.z > nearPlane) {
        const QVector3D axes[] = {{100,0,0},{0,-100,0},{0,0,-100}};
        const QColor colors[] = {QColor("#ce7979"),QColor("#7ca58a"),QColor("#7da7d2")};
        const QQuaternion rotation = QQuaternion::fromAxisAndAngle(0,0,1,float(-m_inputAngle));
        for (int i=0;i<3;++i) {
            const auto tip = project(rotation.rotatedVector(axes[i]));
            if (tip.z <= nearPlane) continue;
            painter.setPen(QPen(colors[i],std::max(1.,dpr)));
            painter.drawLine(QPointF(origin.x,origin.y),QPointF(tip.x,tip.y));
            painter.drawText(QPointF(tip.x+3*dpr,tip.y-3*dpr),QString(QChar('X'+i)));
        }
    }
    painter.end();
    image.setDevicePixelRatio(dpr);
    return image;
}
QImage RvPointCloudCanvas::grabFramebuffer() const { return renderFrame(); }
void RvPointCloudCanvas::paintEvent(QPaintEvent *) { QPainter(this).drawImage(QPoint(),renderFrame()); }
void RvPointCloudCanvas::mousePressEvent(QMouseEvent *event) { m_previous = event->position(); }
void RvPointCloudCanvas::mouseMoveEvent(QMouseEvent *event) {
    if (m_points.isEmpty() || !(event->buttons() & (Qt::LeftButton|Qt::MiddleButton))) return;
    const auto delta = event->position()-m_previous; m_previous=event->position();
    if (event->buttons() & Qt::MiddleButton) {
        const float scale = float(2*m_distance*std::tan(Pi/8)/std::max(1,height()));
        const auto orbit=QQuaternion::fromEulerAngles(float(m_pitch),float(m_yaw),0);
        m_target += orbit.rotatedVector(QVector3D(float(-delta.x()),float(delta.y()),0)*scale);
    } else { m_yaw -= delta.x()*.45; m_pitch=std::clamp(m_pitch-delta.y()*.45,-85.,85.); }
    update();
}
void RvPointCloudCanvas::wheelEvent(QWheelEvent *event) {
    if (m_points.isEmpty()) return;
    const double steps=event->angleDelta().y() ? event->angleDelta().y()/120. : event->pixelDelta().y()/40.;
    m_distance=std::clamp(m_distance*std::pow(.86,steps),20.,60000.);
    event->accept(); update();
}
