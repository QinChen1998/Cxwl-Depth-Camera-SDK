#pragma once

#include <QByteArrayView>
#include <QJsonArray>
#include <QJsonObject>
#include <array>
#include <map>
#include <vector>

namespace RvRgbLsc {
inline constexpr int RawBytes = 3072 * 1296;
inline constexpr int GridSize = 17;
inline constexpr int GridNodes = GridSize * GridSize;
inline constexpr int CaptureFrames = 21;
inline constexpr int FramesPerExposure = 3;
inline constexpr std::array<int, 7> Exposures{4, 48, 160, 480, 960, 1280, 160};
static_assert(CaptureFrames == FramesPerExposure * Exposures.size());

struct Tile {
    double greenDifference;
    double brightness;
    double texture;
    double clipped;
};

using Frame = std::vector<Tile>;
using Bracket = std::map<int, std::vector<Frame>>;

struct Fit {
    std::array<double, GridNodes> logRatio{};
    std::array<double, GridNodes> confidence{};
    int trainingTiles = 0;
    int validationTiles = 0;
    int fullySupportedNodes = 0;
    int nodesWithoutEvidence = GridNodes;
    double before = 0;
    double after = 0;
};

Frame measure(QByteArrayView raw);
Fit estimate(const Bracket &bracket);
bool compatible(const QJsonObject &iq);
QJsonArray greenReference(const QJsonObject &original);
QJsonObject apply(const QJsonArray &greenReference, QJsonObject current, const Fit &fit);
}
