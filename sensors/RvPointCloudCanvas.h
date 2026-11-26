#pragma once
#include <QWidget>
#include <QVector3D>
#include <QVector4D>
#include <QVector>

/* Qt Widgets raster renderer. No Qt Quick 3D, QML or GPL-only module. */
class RvPointCloudCanvas final : public QWidget {
    Q_OBJECT
    Q_PROPERTY(QVector3D cameraTarget MEMBER m_target)
    Q_PROPERTY(double cameraDistance MEMBER m_distance)
    Q_PROPERTY(double orbitYaw MEMBER m_yaw)
    Q_PROPERTY(double orbitPitch MEMBER m_pitch)
    Q_PROPERTY(int inputAngleDegrees MEMBER m_inputAngle)
    Q_PROPERTY(bool meshVisible READ meshVisible)
public:
    struct Point { QVector3D position; QVector4D color; float diameter; };
    explicit RvPointCloudCanvas(QWidget *parent = nullptr);
    void setFrame(const QVector<Point> &points, const QVector<quint32> &indices, bool mesh, int angle);
    void clearFrame();
    void resetView();
    bool meshVisible() const { return m_mesh && !m_indices.isEmpty(); }
    int pointCount() const { return m_mesh ? 0 : int(m_points.size()); }
    int meshIndexCount() const { return m_mesh ? int(m_indices.size()) : 0; }
    QVector3D pointPosition(int i) const { return m_points.at(i).position; }
    QImage grabFramebuffer() const;
protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void wheelEvent(QWheelEvent *) override;
private:
    QImage renderFrame() const;
    QVector<Point> m_points;
    QVector<quint32> m_indices;
    QVector3D m_target{0, 0, -600};
    double m_distance = 900, m_yaw = -25, m_pitch = -18;
    int m_inputAngle = 90;
    bool m_mesh = false;
    QPointF m_previous;
};
