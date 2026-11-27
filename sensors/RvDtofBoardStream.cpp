#include "RvDtofBoardStream.h"
#include <QtConcurrent/QtConcurrentRun>
#include <atomic>
#include <chrono>

struct RvDtofBoardStream::Decoder {std::atomic_bool cancelled{false};};
namespace {
using namespace RvCaptureContract;
constexpr int HeaderBytes=36,MaximumJsonBytes=4096,NativePacketLimit=32768;

RvTofFrame nativeMeasurement(const QByteArray& bytes,const QJsonObject& packet,const QSize& grid,
                             quint64 sequence,quint64 timestamp,int fov){
    RvTofFrame frame{bytes,packet};auto& m=frame.metadata;
    m["dtof_fov_correction"]=fov;m["frame_id"]=qint64(sequence);m["tof_sequence"]=qint64(sequence);m["tof_timestamp_ns"]=qint64(timestamp);
    m["tof_reused"]=false;m["work_mode"]="dtof_candidate";
    m["model"] = RvDtofCandidates::name(m["candidate_mode"].toInt());
    m["dtof_width"]=grid.width();m["dtof_height"]=grid.height();m["dtof_bytes"]=bytes.size();
    m["dtof_format"]="range_uq2_u16le_snr_u8_valid_u8";m["dtof_range_unit"]="0.25_mm";
    m["dtof_source"]="tmf8829_ipc_first_peak";m["dtof_coordinate"]="sensor_radial_native";
    m["tof_filter_raw_preserved"]=true;m["pairing_policy"]="independent_streams";
    return frame;
}

QThreadPool *liveDecodePool(){
    static QThreadPool pool;
    static const bool configured = [] {
        pool.setObjectName("rvLiveDecodePool");
        pool.setMaxThreadCount(1);
        return true;
    }();
    (void)configured;
    return &pool;
}
}
RvDtofBoardStream::RvDtofBoardStream(const QJsonObject& receipt,
    QObject* parent,const QString& host,quint16 port,QThreadPool* pool)
    :QObject(parent),expected([&]{auto value=receipt;value["protocol"]=RvCaptureContract::PairedProtocol;return value;}()),
     decoder(std::make_shared<Decoder>()),decodePool(pool?pool:liveDecodePool()){
    socket.setProxy(QNetworkProxy::NoProxy);socket.setReadBufferSize(NativePacketLimit);
    poll.setInterval(20);timeout.setSingleShot(true);progress.start();
    connect(&poll,&QTimer::timeout,this,[this]{ask();});
    connect(&socket,&QTcpSocket::connected,this,[this]{socket.setSocketOption(QAbstractSocket::LowDelayOption,1);poll.start();ask();});
    connect(&socket,&QTcpSocket::readyRead,this,[this]{read();});
    const auto disconnected=[this]{fail(tr("CXWL Depth Camera candidate stream disconnected"));};
    connect(&socket,&QTcpSocket::errorOccurred,this,disconnected);
    connect(&socket,&QTcpSocket::disconnected,this,disconnected);
    connect(&timeout,&QTimer::timeout,this,[this]{fail(tr("CXWL Depth Camera candidate stream timed out"));});
    connect(&watcher,&QFutureWatcher<Decoded>::finished,this,[this]{finish();});
    timeout.start(3000);socket.connectToHost(host,port);
}
RvDtofBoardStream::~RvDtofBoardStream(){
    decoder->cancelled.store(true);rawFrame={};videoProgress={};error={};
    watcher.disconnect(this);socket.disconnect(this);socket.abort();
}
void RvDtofBoardStream::fail(const QString& text){
    if(failed)return;failed=true;decoder->cancelled.store(true);poll.stop();timeout.stop();socket.abort();
    if(error){const auto callback=error;callback(text);}
}
void RvDtofBoardStream::ask(){
    if(failed||pending||socket.state()!=QAbstractSocket::ConnectedState)return;
    if(progress.elapsed()>10000){fail(tr("CXWL Depth Camera candidate stream timed out"));return;}
    QByteArray bytes(32,0);qToLittleEndian<quint32>(RvCaptureContract::PairedRequestMagic,bytes.data());
    qToLittleEndian<quint32>(++request,bytes.data()+4);
    if(expected["point_cloud_format"]==PointCloudFormat)
        qToLittleEndian<quint32>(RequestOrganizedXyz,bytes.data()+8);
    // LSG2 uses a zero cursor to return the newest sample in the board's queue.
    qToLittleEndian<quint64>(requestNewest ? 0 : identities.rawSequence,bytes.data()+16);
    pending=true;roundtrip.restart();timeout.start(3000);socket.write(bytes);
}
void RvDtofBoardStream::read(){
    if(failed)return;
    if(!pending||decoding||socket.bytesAvailable()+buffer.size()>NativePacketLimit){fail(tr("Invalid candidate stream size"));return;}
    buffer+=socket.readAll();if(buffer.size()<HeaderBytes)return;
    const auto u=[&](int off){return qFromLittleEndian<quint32>(buffer.constData()+off);};
    const auto jsonSize=u(4),rawSize=u(16),xyzSize=u(20);
    const auto grid=nativeCaptureGrid(expected);
    if(u(0)!=PairedResponseMagic||jsonSize==0||jsonSize>MaximumJsonBytes
        ||u(8)||u(12)||u(24)||u(28)||u(32)||!grid.isValid()||(rawSize&&rawSize!=quint32(grid.width()*grid.height()*4))){
        fail(tr("Invalid candidate stream header"));return;
    }
    const bool wantsXyz=expected["point_cloud_format"]==PointCloudFormat;
    if(xyzSize!=(wantsXyz?rawSize/4*sizeof(Point):0)){fail(tr("Invalid candidate stream header"));return;}
    const auto total=HeaderBytes+jsonSize+rawSize+xyzSize;if(buffer.size()<total)return;
    if(buffer.size()!=total){fail(tr("Unexpected candidate stream payload"));return;}
    decoding=true;auto packet=std::move(buffer);buffer.clear();
    watcher.setFuture(QtConcurrent::run(decodePool,[state=decoder,packet=std::move(packet),receipt=expected,previous=identities,id=request,newest=requestNewest]{
        return decode(state,packet,receipt,previous,id,newest);
    }));
}
RvDtofBoardStream::Decoded RvDtofBoardStream::decode(const std::shared_ptr<Decoder>& decoder,
    const QByteArray& buffer,const QJsonObject& expected,Identities previous,quint32 request,bool requestedNewest){
    Decoded out;out.request=request;out.identities=previous;if(decoder->cancelled.load())return out;
    const auto reject=[&](const QString& message){out.error=message;return out;};
    const auto u=[&](int off){return qFromLittleEndian<quint32>(buffer.constData()+off);};
    const auto jsonSize=u(4),rawSize=u(16);QJsonParseError parse;
    const auto doc=QJsonDocument::fromJson(buffer.mid(HeaderBytes,jsonSize),&parse);const auto m=doc.object();
    const int expectedFov=expected["expected_fov"].toInt(6);
    const auto grid=nativeCaptureGrid(expected);
    if(expectedFov!=1&&expectedFov!=2&&expectedFov!=6)return reject(tr("Unsupported device calibration"));
    if(parse.error!=QJsonParseError::NoError||!doc.isObject()||!boardImagingIdentity(m)||!nativeVideoIdentity(m)
        ||m["point_cloud_format"]!=expected["point_cloud_format"]||m["protocol"]!=expected["protocol"]||m["boot_id"]!=expected["boot_id"]||m["generation"]!=expected["generation"]
        ||m["candidate_mode"]!=expected["candidate_mode"]||m["options"]!=expected["options"]||m["request"].toInteger()!=request
        ||m["data"]!=false||m["photon_data"]!=false||m["rgb_data"]!=false||!m["raw_data"].isBool()
        ||m["raw_data"].toBool()!=bool(rawSize)||m["native_capture_protocol"]!=RvCaptureContract::NativeCaptureProtocolV2
        ||m["native_columns"]!=grid.width()||m["native_rows"]!=grid.height())
        return reject(tr("Candidate identity or parameters mismatch"));
    bool a=false,b=false,c=false;
    const auto encoded=m["video_encoded_sequence"].toString().toULongLong(&a),retrieved=m["video_retrieved_sequence"].toString().toULongLong(&b),observed=m["video_observed_ns"].toString().toULongLong(&c);
    const auto& last=previous;const bool same=encoded==last.videoEncoded&&retrieved==last.videoRetrieved&&observed==last.videoObserved;
    out.videoAge=m["video_age_ms"].toDouble(-1);
    if(m["video_progress_source"]!="venc_info:chn0"||!a||!b||!c||!encoded||!retrieved||!observed||retrieved>encoded
        ||encoded>9007199254740991ULL||observed>9007199254740991ULL||!std::isfinite(out.videoAge)||out.videoAge<0||out.videoAge>=1000
        ||(!same&&(encoded<=last.videoEncoded||retrieved<=last.videoRetrieved||observed<=last.videoObserved)))
        return reject(tr("Invalid HD encoder progress or identity"));
    out.identities.videoEncoded=encoded;out.identities.videoRetrieved=retrieved;out.identities.videoObserved=observed;out.videoMeta=m;
    out.rawPresent=rawSize!=0;
    if(rawSize){
        bool seqOk=false,stampOk=false;const auto seq=m["raw_sequence"].toString().toULongLong(&seqOk),stamp=m["raw_timestamp_ns"].toString().toULongLong(&stampOk);
        out.rawAge=m["raw_age_ms"].toDouble(-1);
        const bool repeated=requestedNewest && last.rawSequence && seq==last.rawSequence && stamp==last.rawCapture;
        if(!seqOk||!stampOk||(!repeated&&(seq<=last.rawSequence||stamp<=last.rawCapture))||seq>9007199254740991ULL||stamp>9007199254740991ULL
            ||!std::isfinite(out.rawAge)||out.rawAge<0||m["raw_fov_correction"]!=expectedFov||m["raw_columns"]!=grid.width()||m["raw_rows"]!=grid.height()
            ||!m["tof_warning_flags"].isDouble()||m["tof_warning_flags"].toDouble()!=m["tof_warning_flags"].toInteger(-1)
            ||(quint64(m["tof_warning_flags"].toInteger(-1))&~quint64(0x18))!=0)
            return reject(tr("Invalid candidate measurement identity"));
        // A newest read can return the last seen sample. It cannot refresh depth.
        out.rawPresent=!repeated;
        out.identities.rawSequence=seq;out.identities.rawCapture=stamp;
        out.native=nativeMeasurement(buffer.mid(HeaderBytes+jsonSize,rawSize),m,grid,seq,stamp,expectedFov);
        if(u(20)){
            out.native.boardPoints=buffer.mid(HeaderBytes+jsonSize+rawSize,u(20));
            const auto* source=out.native.boardPoints.constData();
            for(int i=0;i<out.native.sampleCount();++i){
                Point point;
                std::memcpy(&point,source+i*sizeof(Point),sizeof(point));
                const bool measured=out.native.valid(i)&&out.native.rangeUq2(i)>0;
                if(!std::isfinite(point.x)||!std::isfinite(point.y)||!std::isfinite(point.z)
                    ||(measured?point.z<=0:(point.x!=0||point.y!=0||point.z!=0)))
                    return reject(tr("Invalid candidate depth values"));
            }
        }
        const auto& native=out.native;
        if(!native.isValid()||(m.contains("dtof_fov_correction")&&m["dtof_fov_correction"]!=expectedFov)
            ||(m.contains("tof_fov_correction")&&m["tof_fov_correction"]!=expectedFov))return reject(tr("Invalid candidate depth values"));
    }

    return out;
}
void RvDtofBoardStream::finish(){
    if(failed||!decoding)return;Decoded out;
    try{out=watcher.result();}catch(...){fail(tr("Invalid candidate stream header"));return;}
    if(out.request!=request){fail(tr("Candidate identity or parameters mismatch"));return;}
    if(!out.error.isEmpty()){fail(out.error);return;}
    requestNewest=false;
    const double videoAge=out.videoAge+roundtrip.nsecsElapsed()/1e6;
    // Drop the whole late packet without advancing accepted identities.
    if(videoAge>=1000){decoding=false;pending=false;timeout.stop();return;}
    identities=out.identities;QPointer<RvDtofBoardStream> guard(this);
    const auto alive=[&]{return guard&&!guard->failed;};
    out.videoMeta["video_age_ms"]=videoAge;
    if(videoProgress){const auto callback=videoProgress;callback(out.videoMeta);}
    if(!alive())return;
    if(out.rawPresent){const double requestMs=roundtrip.nsecsElapsed()/1e6;const double age=out.rawAge+requestMs;out.native.metadata["raw_age_ms"]=age;
        out.native.metadata["raw_board_age_ms"]=out.rawAge;
        out.native.metadata["raw_request_roundtrip_ms"]=requestMs;
        const auto hostNs=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
        out.native.metadata["raw_host_dispatch_ns"]=QString::number(hostNs);
        if(age<220){
            progress.restart();
            if(rawFrame){const auto callback=rawFrame;callback(out.native);}
        }else if(expected["point_cloud_format"]==PointCloudFormat){
            // Skip queued measurements after a late reply, then resume ordered reads.
            requestNewest=true;
        }
    }
    if(!alive())return;decoding=false;pending=false;timeout.stop();
    if(expected["point_cloud_format"]==PointCloudFormat)ask();
}
