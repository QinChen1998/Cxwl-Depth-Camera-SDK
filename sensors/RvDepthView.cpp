#include "RvDepthView.h"
#include "PointCloudColorScale.h"
#include "../tools/rv_capture/Contract.h"

#include <QtCore/QMetaObject>
#include <QtCore/QtEndian>
#include <cstring>
#include <QtGui/QHideEvent>
#include <QtGui/QShowEvent>
#include <condition_variable>
#include <array>
#include <mutex>
#include <optional>
#include <thread>

namespace {
float floatPixel(const QByteArray &bytes,int index){
    const quint32 bits=qFromLittleEndian<quint32>(bytes.constData()+4*index);
    float value;std::memcpy(&value,&bits,4);
    return value;
}
}

// One pending input, one completed image and one queued GUI notification.
// The worker owns its reusable buffers and never reads or changes a widget.
struct RvDepthView::Colorizer {
    struct Job {
        std::shared_ptr<const std::vector<float>> values;
        QSize size;
        double minimum=0,maximum=3,frameId=-1,generation=-1;
        bool blackOutside=false, dynamic=false;
        quint64 revision=0;
    };
    struct Result { QImage image; double frameId=-1,generation=-1; quint64 revision=0; };

    explicit Colorizer(RvDepthView *view):owner(view),worker([this]{run();}){}
    ~Colorizer(){
        {std::lock_guard<std::mutex> lock(mutex);stopping=true;pending.reset();}
        available.notify_one();worker.join();
    }
    void submit(Job job){
        {std::lock_guard<std::mutex> lock(mutex);pending=std::move(job);}
        available.notify_one();
    }
    void cancel(){std::lock_guard<std::mutex> lock(mutex);pending.reset();completed.reset();}
    std::optional<Result> take(){
        std::lock_guard<std::mutex> lock(mutex);
        notificationQueued=false;
        auto result=std::move(completed);completed.reset();return result;
    }
    static Job forView(const RvDepthView &view){
        return {view.depth,view.depthSize,view.rangeMinimum,view.rangeMaximum,
            view.property("frameId").toDouble(),view.property("generation").toDouble(),
            view.blackOutside,view.useDynamicColor,view.colorRevision};
    }
    static void render(const Job &job,QImage &buffer){
        if(buffer.size()!=job.size||buffer.format()!=QImage::Format_RGB32)buffer=QImage(job.size,QImage::Format_RGB32);
        // bits() detaches once if an explicit snapshot still owns this buffer.
        auto *pixels=reinterpret_cast<QRgb *>(buffer.bits());
        const int count=job.size.width()*job.size.height();
        std::optional<PointCloudColorScale::DynamicHistogram> histogram;
        if (job.dynamic) histogram.emplace(*job.values, [](float radialMetres) { return radialMetres; });
        for (int i = 0; i < count; ++i) {
            const float metres = (*job.values)[i];
            pixels[i] = histogram ? histogram->color(metres)
                                  : DepthColorScale::color(metres, job.minimum, job.maximum, true, job.blackOutside);
        }
    }
    void run(){
        std::array<QImage,2> buffers;int nextBuffer=0;
        for(;;){
            Job job;
            {std::unique_lock<std::mutex> lock(mutex);
             available.wait(lock,[this]{return stopping||pending.has_value();});
             if(stopping)return;
             job=std::move(*pending);pending.reset();}
            auto &buffer=buffers[nextBuffer];nextBuffer=1-nextBuffer;
            render(job, buffer);
            bool notify=false;
            {std::lock_guard<std::mutex> lock(mutex);
             if(stopping)return;
             completed=Result{buffer,job.frameId,job.generation,job.revision};
             notify=!notificationQueued;notificationQueued=true;}
            if(notify)QMetaObject::invokeMethod(owner,[view=owner]{view->takeColoredFrame();},Qt::QueuedConnection);
        }
    }
    RvDepthView *owner;
    std::mutex mutex;std::condition_variable available;
    std::optional<Job> pending;std::optional<Result> completed;
    bool stopping=false,notificationQueued=false;
    std::thread worker;
};

RvDepthView::RvDepthView(QWidget *parent):QWidget(parent),hoverTip(this){
    setObjectName("rvDepthCanvas");setMouseTracking(true);
    setMinimumSize(ImagePresentation::MinimumViewportWidth,ImagePresentation::MinimumViewportHeight);
}
RvDepthView::~RvDepthView(){colorizer.reset();}

void RvDepthView::setDynamicColoring(bool enabled)
{
    if (useDynamicColor == enabled) return;
    useDynamicColor = enabled;
    recolor();
}

bool RvDepthView::setDepth(const QByteArray &bytes, const QJsonObject &metadata)
{
    const int width = metadata["width"].toInt();
    const int height = metadata["height"].toInt();
    if (!RvCaptureContract::supportsProductGrid(width, height)
        || metadata["depth_unit"] != "sensor_radial_m_float32" || bytes.size() != width * height * 4) {
        clear();
        return false;
    }
    auto values = std::make_shared<std::vector<float>>(width * height);
    for (int i = 0; i < int(values->size()); ++i) {
        const float metres = floatPixel(bytes, i);
        if (!std::isfinite(metres)) { clear(); return false; }
        (*values)[i] = metres;
    }
    depth = std::move(values);
    depthSize = QSize(width, height);
    held = false;
    setProperty("frameId", metadata["frame_id"].toDouble());
    setProperty("generation", metadata["generation"].toDouble());
    setProperty("depthWidth", width);
    setProperty("depthHeight", height);
    recolor();
    return true;
}

void RvDepthView::holdFrame(){
    if(!hasFrame()||held)return;
    held=true;depth.reset();
    refreshHover();update();
    // An already submitted image may finish, but can only restore pixels.
    // Its numeric values never return to depthAt() or the saving snapshot.
}
void RvDepthView::clear(const QString &reason){
    fpsTimes.clear();fpsClock.invalidate();paintedId=paintedGeneration=-1;
    ++colorRevision;if(colorizer)colorizer->cancel();
    emptyReason=reason;held=false;
    depth.reset();depthSize={};image={};
    setProperty("frameId",QVariant());setProperty("generation",QVariant());
    setProperty("displaySourceRect",QVariant());
    hoverTip.clear();update();
}
void RvDepthView::recolor(){
    if(!depth||held)return;
    ++colorRevision;requestColorization();
}
void RvDepthView::requestColorization(){
    if(!isVisible()||!depth||held||imageRevision==colorRevision)return;
    if(!colorizer)colorizer=std::make_unique<Colorizer>(this);
    colorizer->submit(Colorizer::forView(*this));
    setProperty("colorizationRequests",property("colorizationRequests").toULongLong()+1);
}
void RvDepthView::takeColoredFrame(){
    if(!colorizer)return;
    auto result=colorizer->take();
    if(!result||result->revision!=colorRevision||!isVisible())return;
    image=std::move(result->image);imageRevision=result->revision;
    imageFrameId=result->frameId;imageGeneration=result->generation;
    setProperty("coloredFrameId",imageFrameId);
    setProperty("coloredFrames",property("coloredFrames").toULongLong()+1);refreshHover();update();
}
void RvDepthView::refreshHover()
{
    if (!hoverTip.tracking()) return;
    const auto size = presentationSize();
    bool invertible = false;
    const auto inverse = nativePresentationTransform().inverted(&invertible);
    const auto point = inverse.map(hoverTip.position());
    if (!invertible || image.isNull() || !QRectF(QPointF(), size).contains(point)) {
        hoverTip.hideText();
        return;
    }
    // Keep the readout paired with the displayed frame while coloring catches up.
    if (!held && imageRevision != colorRevision) return;
    const int x = std::clamp(int(point.x()), 0, image.width() - 1);
    const int y = std::clamp(int(point.y()), 0, image.height() - 1);
    const float metres = depthAt(x, y);
    hoverTip.showText(held ? tr("Last displayed frame; depth reading expired") : metres > 0
        ? tr("(%1, %2) dToF radial: %3 m").arg(x).arg(y).arg(metres, 0, 'f', 4) : tr("Invalid depth"));
}

QImage RvDepthView::presentationImage() const
{
    // Snapshot requests can render synchronously even while the view is hidden.
    if (depth && !held && imageRevision != colorRevision) {
        const auto job = Colorizer::forView(*this);
        Colorizer::render(job, image);
        imageRevision = colorRevision;
        imageFrameId = job.frameId;
        imageGeneration = job.generation;
    }
    return image;
}

void RvDepthView::showEvent(QShowEvent *event){QWidget::showEvent(event);requestColorization();}
void RvDepthView::hideEvent(QHideEvent *event){if(colorizer)colorizer->cancel();QWidget::hideEvent(event);}
