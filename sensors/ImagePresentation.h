#pragma once

#include "../demo/DepthSdkClient.h"
#include <QtCore/QCoreApplication>
#include <QtCore/QPointF>
#include <QtCore/QRectF>
#include <QtGui/QImage>
#include <QtGui/QPainter>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QPushButton>

// Presentation only: source pixels, metadata and saved snapshots stay unchanged.
namespace ImagePresentation {
inline constexpr int Margin = 6;
inline constexpr int Spacing = 4;
inline constexpr int ToolbarHeight = 28;
inline constexpr int MinimumViewportWidth = 260;
inline constexpr int MinimumViewportHeight = 170;
inline constexpr auto CaptureProgressTextStyle = "color: #dce6f0; background: transparent; font-weight: 600;";
inline constexpr auto CaptureProgressBarStyle =
    "QProgressBar { border: 1px solid #344355; border-radius: 5px; background: #202936; color: #ffffff; text-align: center; } "
    "QProgressBar::chunk { background: #176bb8; border-radius: 4px; }";

inline bool updateCaptureButtons(const DepthSdkClient *session, QPushButton &start,
                                 QPushButton &stop, QPushButton &reconnect)
{
    const bool available = session && session->available();
    const bool connecting = available && session->automaticConnecting();
    const bool canReconnect = available && !session->busy() && !connecting;
    const bool stopped = !available || session->userStopped();
    start.setEnabled(canReconnect && stopped);
    stop.setEnabled(available && !session->stopping() && (!stopped || connecting));
    reconnect.setEnabled(canReconnect);
    return canReconnect;
}

inline int normalizedRotation(int degrees)
{
    switch (degrees) {
    case 90: case 180: case 270: return degrees;
    default: return 0;
    }
}

inline QSizeF rotatedSize(QSizeF source, int degrees)
{
    const int rotation = normalizedRotation(degrees);
    return rotation == 90 || rotation == 270
        ? QSizeF(source.height(), source.width()) : source;
}

inline QRectF fittedRect(QRectF available, QSizeF source, int degrees)
{
    if (available.isEmpty() || source.isEmpty())
        return {};
    QSizeF target = rotatedSize(source, degrees);
    target.scale(available.size(), Qt::KeepAspectRatio);
    return QRectF(available.center() - QPointF(target.width() / 2, target.height() / 2), target);
}

inline QRectF unrotatedRect(const QRectF &target, int degrees)
{
    const QSizeF size = rotatedSize(target.size(), degrees);
    return QRectF(-size.width() / 2, -size.height() / 2, size.width(), size.height());
}

inline void drawImage(QPainter &painter, const QRectF &target, const QImage &image, int degrees,
                      const QRectF &source = {})
{
    if (image.isNull() || target.isEmpty())
        return;
    painter.save();
    painter.translate(target.center());
    painter.rotate(normalizedRotation(degrees)); // Positive angles are clockwise in widget coordinates.
    painter.drawImage(unrotatedRect(target, degrees), image, source.isEmpty()?QRectF(image.rect()):source);
    painter.restore();
}

inline void retranslateRotationCombo(QComboBox *combo)
{
    combo->setItemText(0, QCoreApplication::translate("ImagePresentation", "0° (Original)"));
    combo->setItemText(1, QCoreApplication::translate("ImagePresentation", "90° clockwise"));
    combo->setItemText(2, QCoreApplication::translate("ImagePresentation", "180°"));
    combo->setItemText(3, QCoreApplication::translate("ImagePresentation", "270° clockwise"));
    combo->setAccessibleName(QCoreApplication::translate("ImagePresentation", "Display rotation"));
    combo->setToolTip(QCoreApplication::translate("ImagePresentation", "Rotate both depth and camera displays on this computer. Source data and saved images keep their original orientation."));
}
}
