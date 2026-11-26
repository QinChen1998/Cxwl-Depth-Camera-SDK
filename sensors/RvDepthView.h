#pragma once
#include "ImagePresentation.h"
#include "DepthColorScale.h"
#include "ImageHoverTip.h"
#include <DepthSdk.h>
#include <QtWidgets/QWidget>
#include <QtGui/QPainter>
#include <QtGui/QMouseEvent>
#include <QtCore/QJsonObject>
#include <QtCore/QElapsedTimer>
#include <vector>
#include <algorithm>
#include <QtCore/QCoreApplication>
#include <cmath>
#include <memory>

// Native radial depth display with independent view orientation.
class RvDepthView : public QWidget {
 Q_DECLARE_TR_FUNCTIONS(RvDepthView)
public:
 double displayedFps()const{
  if(held||!isVisible()||!fpsClock.isValid()||fpsTimes.size()<2||fpsClock.elapsed()-fpsTimes.back()>=1000)return 0;
  const auto span=fpsTimes.back()-fpsTimes.front();
  return span>0?(fpsTimes.size()-1)*1000.0/span:0;
 }
 explicit RvDepthView(QWidget* parent=nullptr);
 ~RvDepthView() override;
 void setDisplayRotationDegrees(int degrees){rotation=ImagePresentation::normalizedRotation(degrees);refreshHover();update();}
 int displayRotationDegrees()const{return rotation;}
 void setNativeDisplayRotationDegrees(int degrees){
  nativeRotation=(degrees%360+360)%360;refreshHover();update();
 }
 int nativeDisplayRotationDegrees()const{return nativeRotation;}
 QSizeF presentationSize()const{return image.isNull()?QSizeF(CXWL_DEPTH_WIDTH,CXWL_DEPTH_HEIGHT):QSizeF(image.size());}
 QRect displayedSourceRect()const{return image.rect();}
 bool setDepth(const QByteArray& bytes,const QJsonObject& metadata);
 // Retain only display pixels; expired metric values must not remain queryable.
 void holdFrame();
 bool frameHeld()const{return held;}
 void clear(const QString& reason=QString());
 bool setDisplayRange(double minimum,double maximum){
  if(!std::isfinite(minimum)||!std::isfinite(maximum)||minimum<0||maximum>1000||maximum<=minimum)return false;
  if(rangeMinimum==minimum&&rangeMaximum==maximum)return true;
  rangeMinimum=minimum;rangeMaximum=maximum;recolor();return true;
 }
 double displayMinimum()const{return rangeMinimum;}
 double displayMaximum()const{return rangeMaximum;}
 void setBlackOutsideRange(bool enabled){if(blackOutside==enabled)return;blackOutside=enabled;recolor();}
 bool blackOutsideRange()const{return blackOutside;}
 void setDynamicColoring(bool enabled);
 bool dynamicColoring() const { return useDynamicColor; }
 QRgb colorAt(int x,int y)const{const auto current=presentationImage();return current.isNull()?qRgb(0,0,0):current.pixel(x,y);}
 QImage presentationImage() const;
 QImage snapshotImage() const { return presentationImage(); }
 bool hasFrame() const { return bool(depth) || !image.isNull(); }
 bool hasRenderableFrame() const { return depth && !held && !image.isNull() && imageRevision == colorRevision; }
 float depthAt(int x,int y)const{return !held&&depth&&x>=0&&x<depthSize.width()&&y>=0&&y<depthSize.height()?(*depth)[y*depthSize.width()+x]:0;}
protected:
 void showEvent(QShowEvent *event)override;
 void hideEvent(QHideEvent *event)override;
 void paintEvent(QPaintEvent*)override{
  QPainter p(this);p.fillRect(rect(),QColor("#11161d"));p.setPen(QColor("#c9d1d9"));
  if(image.isNull()){
   p.drawText(rect(),Qt::AlignCenter,emptyReason.isEmpty()?tr("Waiting for CXWL Depth Camera depth"):emptyReason);return;
  }
  const auto transform = nativePresentationTransform();
  const QRectF source(QPointF(), presentationSize());
  setProperty("presentationRect", transform.mapRect(source));
  p.save();
  p.setTransform(transform, true);
  p.drawImage(source, image, image.rect());
  p.restore();
  setProperty("displaySourceRect", image.rect());
  if(!held&&(paintedId!=imageFrameId||paintedGeneration!=imageGeneration)){
   if(!fpsClock.isValid())fpsClock.start();
   const auto now=fpsClock.elapsed();
   if(paintedGeneration!=imageGeneration||(!fpsTimes.empty()&&now-fpsTimes.back()>=1000))fpsTimes.clear();
   while(!fpsTimes.empty()&&now-fpsTimes.front()>2000)fpsTimes.erase(fpsTimes.begin());
   fpsTimes.push_back(now);
   paintedId=imageFrameId;paintedGeneration=imageGeneration;setProperty("paintedFrames",property("paintedFrames").toULongLong()+1);
  }
 }
 void mouseMoveEvent(QMouseEvent*)override{refreshHover();}
 void leaveEvent(QEvent*)override{hoverTip.clear();}
private:
 QTransform nativePresentationTransform()const{
  const auto size=presentationSize();const auto available=QRectF(rect()).adjusted(8,8,-8,-8);
  if(size.isEmpty()||available.isEmpty())return {};
  QTransform turn;turn.rotate((rotation+nativeRotation)%360);
  const auto bounds=turn.mapRect(QRectF(QPointF(),size));
  const auto scale=std::min(available.width()/bounds.width(),available.height()/bounds.height());
  QTransform transform;transform.translate(available.center().x(),available.center().y());
  // Mirror the final rotated native view horizontally in screen coordinates.
  // The same transform is inverted for hover; samples and saved pixels stay raw.
  transform.scale(-1,1);
  transform.rotate((rotation+nativeRotation)%360);transform.scale(scale,scale);transform.translate(-size.width()/2,-size.height()/2);
  return transform;
 }
 ImageHoverTip hoverTip;
 void refreshHover();
 bool held=false;
 struct Colorizer;
 std::unique_ptr<Colorizer> colorizer;
 void recolor();
 void requestColorization();
 void takeColoredFrame();
 quint64 colorRevision=0;
 mutable quint64 imageRevision=0;
 mutable double imageFrameId=-1,imageGeneration=-1;
 double paintedId=-1,paintedGeneration=-1;
 QElapsedTimer fpsClock;
 std::vector<qint64> fpsTimes;
 double rangeMinimum=0,rangeMaximum=3;
 bool blackOutside=false, useDynamicColor=false;
 int rotation=0;
 int nativeRotation=0;
 QString emptyReason;std::shared_ptr<const std::vector<float>> depth;QSize depthSize;mutable QImage image;
};
