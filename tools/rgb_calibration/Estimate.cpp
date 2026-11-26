// Derived from the stationary multi-exposure estimator delivered under LICENSE.txt.
#include "Estimate.h"
#include <QJsonArray>
#include <QStringList>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace RvRgbLsc {
namespace {
constexpr int Width = 2304;
constexpr int Height = 1296;
constexpr int Stride = 3072;
constexpr int TileSize = 16;
constexpr int Columns = Width / TileSize;
constexpr int Rows = Height / TileSize;
constexpr int Tiles = Columns * Rows;
constexpr double Pi = 3.14159265358979323846;
constexpr auto GreenR = "lsc_samples_greenR";
constexpr auto GreenB = "lsc_samples_greenB";

double median(std::vector<double> &samples)
{
    if (samples.empty()) return 0;
    std::sort(samples.begin(), samples.end());
    const auto middle = samples.size() / 2;
    return samples.size() % 2 ? samples[middle] : (samples[middle - 1] + samples[middle]) / 2;
}

Frame medianFrame(const std::vector<Frame> &frames)
{
    if (frames.empty()) throw std::runtime_error("Incomplete Bayer exposure bracket");
    Frame result(Tiles);
    std::vector<double> values;
    values.reserve(frames.size());
    const auto middle = [&](int tile, double Tile::*member) {
        values.clear();
        for (const auto &frame : frames) values.push_back(frame.at(tile).*member);
        return median(values);
    };
    for (int index = 0; index < Tiles; ++index) {
        result[index] = {
            middle(index, &Tile::greenDifference), middle(index, &Tile::brightness),
            middle(index, &Tile::texture), middle(index, &Tile::clipped)};
    }
    return result;
}

int reflected(int index, int size)
{
    if (index < 0) return 1;
    if (index == size) return size - 2;
    return index;
}

bool usable(const Tile &tile)
{
    return tile.brightness < 940 && tile.clipped == 0
        && tile.texture <= std::max(14.0, .32 * (tile.brightness - 64) + 4);
}

struct Position {
    int main = -1;
    int sub = -1;
};

Position dayIsp(const QJsonObject &iq)
{
    const auto scenes = iq["main_scene"].toArray();
    Position match;
    for (qsizetype m = 0; m < scenes.size(); ++m) {
        const auto scene = scenes.at(m).toObject();
        if (scene["name"] != "normal") continue;
        const auto subs = scene["sub_scene"].toArray();
        for (qsizetype s = 0; s < subs.size(); ++s) {
            const auto sub = subs.at(s).toObject();
            if (sub["name"] != "day" || !sub["scene_isp32"].isObject()) continue;
            if (match.main >= 0) throw std::runtime_error("Ambiguous normal/day ISP32 IQ");
            match = {int(m), int(s)};
        }
    }
    if (match.main < 0) throw std::runtime_error("Missing normal/day ISP32 IQ");
    return match;
}

class LscTables {
public:
    explicit LscTables(const QJsonObject &iq) : position(dayIsp(iq)), scenes(iq["main_scene"].toArray()),
        main(scenes.at(position.main).toObject()), subs(main["sub_scene"].toArray()),
        sub(subs.at(position.sub).toObject()), isp(sub["scene_isp32"].toObject()),
        lsc(isp["lsc_v2"].toObject()), tables(lsc["tbl"].toObject()), entries(tables["tableAll"].toArray())
    {
        const auto common = lsc["common"].toObject();
        if (common["enable"].toInt() != 1) throw std::runtime_error("RGB LSC is disabled");
        const auto spacing = [](const QJsonArray &values, int pixels) {
            return values.size() == GridSize - 1
                && std::all_of(values.begin(), values.end(), [pixels](const QJsonValue &value) { return value.toInt() == pixels; });
        };
        for (const auto &value : common["resolutionAll"].toArray()) {
            const auto resolution = value.toObject();
            if (spacing(resolution["lsc_sect_size_x"].toArray(), 144)
                && spacing(resolution["lsc_sect_size_y"].toArray(), 81)) {
                resolutions.push_back(resolution["name"].toString());
            }
        }
        for (qsizetype i = 0; i < entries.size(); ++i) {
            const auto table = entries.at(i).toObject();
            if (!resolutions.contains(table["resolution"].toString())) continue;
            for (const auto *key : {GreenR, GreenB}) {
                const auto coefficients = table[key].toObject()["uCoeff"].toArray();
                if (coefficients.size() != GridNodes || std::any_of(coefficients.begin(), coefficients.end(),
                    [](const QJsonValue &value) { return value.toInt() < 1 || value.toInt() >= 4096; }))
                    throw std::runtime_error("Invalid SC3336 green coefficients");
            }
            selected.push_back(i);
        }
        if (selected.empty()) throw std::runtime_error("Missing 2304x1296 SC3336 LSC tables");
    }

    QJsonObject replace(QJsonObject iq)
    {
        tables["tableAll"] = entries;
        lsc["tbl"] = tables;
        isp["lsc_v2"] = lsc;
        sub["scene_isp32"] = isp;
        subs.replace(position.sub, sub);
        main["sub_scene"] = subs;
        scenes.replace(position.main, main);
        iq["main_scene"] = scenes;
        return iq;
    }

private:
    Position position;
    QJsonArray scenes;
    QJsonObject main;
    QJsonArray subs;
    QJsonObject sub, isp, lsc, tables;
    QStringList resolutions;

public:
    QJsonArray entries;
    std::vector<qsizetype> selected;
};
}

Frame measure(QByteArrayView raw)
{
    if (raw.size() != RawBytes) throw std::runtime_error("Invalid compact BGGR10 frame size");
    std::vector<uint16_t> pixels(Width * Height);
    for (int y = 0; y < Height; ++y) {
        const auto *packed = reinterpret_cast<const uint8_t *>(raw.data() + y * Stride);
        auto *row = pixels.data() + y * Width;
        for (int x = 0; x < Width; x += 4, packed += 5) {
            row[x] = packed[0] | ((packed[1] & 3) << 8);
            row[x + 1] = (packed[1] >> 2) | ((packed[2] & 15) << 6);
            row[x + 2] = (packed[2] >> 4) | ((packed[3] & 63) << 4);
            row[x + 3] = (packed[3] >> 6) | (packed[4] << 2);
        }
    }
    const auto pixel = [&](int x, int y) { return pixels[reflected(y, Height) * Width + reflected(x, Width)]; };
    const auto error = [&](int x, int y) {
        return pixel(x, y) - .25 * (pixel(x - 1, y - 1) + pixel(x + 1, y - 1)
                                      + pixel(x - 1, y + 1) + pixel(x + 1, y + 1));
    };
    std::array<double, 8> hann{};
    for (int i = 0; i < 8; ++i) hann[i] = (.5 - .5 * std::cos(2 * Pi * i / 7)) / 3.5;
    Frame result;
    result.reserve(Tiles);
    for (int top = 0; top < Height; top += TileSize) {
        for (int left = 0; left < Width; left += TileSize) {
            Tile tile{};
            std::array<double, 64> levels;
            for (int y = 0; y < 8; ++y) {
                for (int x = 0; x < 8; ++x) {
                    const int col = left + 2 * x;
                    const int row = top + 2 * y;
                    const double mean = .5 * (pixel(col + 1, row) + pixel(col, row + 1));
                    levels[y * 8 + x] = mean;
                    tile.brightness += mean / 64;
                    tile.greenDifference += .5 * (error(col + 1, row) - error(col, row + 1)) * hann[x] * hann[y];
                    tile.clipped += (pixel(col, row) >= 1020) + (pixel(col + 1, row) >= 1020)
                                  + (pixel(col, row + 1) >= 1020) + (pixel(col + 1, row + 1) >= 1020);
                }
            }
            std::sort(levels.begin(), levels.end());
            tile.texture = (.15 * levels[59] + .85 * levels[60]) - (.85 * levels[3] + .15 * levels[4]);
            result.push_back(tile);
        }
    }
    return result;
}

Fit estimate(const Bracket &bracket)
{
    if (bracket.size() != 6) throw std::runtime_error("Incomplete SC3336 exposure bracket");
    std::vector<Frame> exposures;
    for (const int exposure : {4, 48, 160, 480, 960, 1280})
        exposures.push_back(medianFrame(bracket.at(exposure)));
    struct Observation { double x, y, ratio; };
    std::vector<Observation> training, validation;
    std::vector<double> ratios, deviations;
    for (int row = 0; row < Rows; ++row) {
        for (int col = 0; col < Columns; ++col) {
            const int index = row * Columns + col;
            ratios.clear();
            for (int a = 0; a < 6; ++a) {
                const auto &first = exposures[a][index];
                if (!usable(first)) continue;
                for (int b = a + 1; b < 6; ++b) {
                    const auto &last = exposures[b][index];
                    const double x = last.brightness - first.brightness;
                    const double y = first.greenDifference - last.greenDifference;
                    if (usable(last) && x >= 28 && std::abs(y) < .42 * x)
                        ratios.push_back(std::log((x + .5 * y) / (x - .5 * y)));
                }
            }
            if (ratios.size() < 2) continue;
            const double center = median(ratios);
            deviations.clear();
            for (const double ratio : ratios) deviations.push_back(std::abs(ratio - center));
            if (median(deviations) > .035) continue;
            auto &group = (row + col) % 2 ? validation : training;
            group.push_back({(col + .5) * 16 / Columns, (row + .5) * 16 / Rows, center});
        }
    }
    Fit fit;
    fit.trainingTiles = int(training.size());
    fit.validationTiles = int(validation.size());
    struct Neighbor { double ratio, weight; };
    std::vector<Neighbor> neighbors;
    for (int y = 0; y < GridSize; ++y) {
        for (int x = 0; x < GridSize; ++x) {
            neighbors.clear();
            double total = 0, minimum = 2.5 * 2.5;
            for (const auto &observation : training) {
                const double dx = observation.x - x, dy = observation.y - y;
                const double distance2 = dx * dx + dy * dy;
                if (distance2 >= 2.5 * 2.5) continue;
                const double weight = std::exp(-.5 * distance2 / (.68 * .68));
                total += weight;
                minimum = std::min(minimum, distance2);
                neighbors.push_back({observation.ratio, weight});
            }
            if (neighbors.empty()) continue;
            std::sort(neighbors.begin(), neighbors.end(), [](const auto &a, const auto &b) { return a.ratio < b.ratio; });
            const int index = y * GridSize + x;
            double sum = 0;
            for (const auto &neighbor : neighbors) {
                sum += neighbor.weight;
                if (sum < .5 * total) continue;
                fit.logRatio[index] = std::clamp(neighbor.ratio, -.28, .28);
                break;
            }
            fit.confidence[index] = std::min(1.0, total / 12)
                * std::clamp((1.9 - std::sqrt(minimum)) / .85, 0.0, 1.0);
            if (fit.confidence[index] > .99) ++fit.fullySupportedNodes;
            if (fit.confidence[index] > 0) --fit.nodesWithoutEvidence;
        }
    }
    const auto field = [&](int x, int y) { const int i = y * GridSize + x; return fit.logRatio[i] * fit.confidence[i]; };
    std::vector<double> before, after;
    for (const auto &sample : validation) {
        const int x = int(sample.x), y = int(sample.y);
        const double fx = sample.x - x, fy = sample.y - y;
        const double predicted = (1 - fy) * ((1 - fx) * field(x, y) + fx * field(x + 1, y))
                               + fy * ((1 - fx) * field(x, y + 1) + fx * field(x + 1, y + 1));
        before.push_back(std::abs(sample.ratio));
        after.push_back(std::abs(sample.ratio - predicted));
    }
    fit.before = median(before);
    fit.after = median(after);
    return fit;
}

bool compatible(const QJsonObject &iq)
{
    try { LscTables tables(iq); return true; }
    catch (const std::exception &) { return false; }
}

QJsonArray greenReference(const QJsonObject &original)
{
    const LscTables base(original);
    QJsonArray result;
    for (const auto index : base.selected) {
        const auto table = base.entries.at(index).toObject();
        result.append(QJsonObject{{"resolution", table["resolution"]}, {"name", table["name"]},
            {"gr", table[GreenR].toObject()["uCoeff"].toArray()},
            {"gb", table[GreenB].toObject()["uCoeff"].toArray()}});
    }
    return result;
}

QJsonObject apply(const QJsonArray &greenReference, QJsonObject current, const Fit &fit)
{
    LscTables live(current);
    for (const auto index : live.selected) {
        auto table = live.entries.at(index).toObject();
        QJsonObject reference;
        for (const auto &entry : greenReference) {
            const auto candidate = entry.toObject();
            if (candidate["resolution"] == table["resolution"] && candidate["name"] == table["name"]) {
                if (!reference.isEmpty()) throw std::runtime_error("Ambiguous original SC3336 LSC table");
                reference = candidate;
            }
        }
        if (reference.isEmpty()) throw std::runtime_error("Original SC3336 IQ has no matching LSC table");
        const auto red = reference["gr"].toArray();
        const auto blue = reference["gb"].toArray();
        if (red.size() != GridNodes || blue.size() != GridNodes)
            throw std::runtime_error("Incomplete original SC3336 green table");
        QJsonArray redResult, blueResult;
        for (int i = 0; i < GridNodes; ++i) {
            const int gr = red.at(i).toInt(), gb = blue.at(i).toInt();
            if (gr < 1 || gr >= 4096 || gb < 1 || gb >= 4096)
                throw std::runtime_error("Invalid original SC3336 green coefficient");
            const double mid = std::sqrt(gr * gb);
            const double ratio = (1 - fit.confidence[i]) * std::log(double(gr) / gb) - fit.confidence[i] * fit.logRatio[i];
            const double r = mid * std::exp(ratio / 2), b = mid * std::exp(-ratio / 2);
            const double lift = std::max(1.0, 1024 / std::min(r, b));
            const int wantedR = int(std::nearbyint(r * lift)), wantedB = int(std::nearbyint(b * lift));
            if (wantedR < 1024 || wantedB < 1024 || wantedR >= 4096 || wantedB >= 4096)
                throw std::runtime_error("SC3336 candidate coefficient outside the ISP range");
            redResult.append(wantedR);
            blueResult.append(wantedB);
        }
        auto greenR = table[GreenR].toObject(), greenB = table[GreenB].toObject();
        greenR["uCoeff"] = redResult;
        greenB["uCoeff"] = blueResult;
        table[GreenR] = greenR;
        table[GreenB] = greenB;
        live.entries.replace(index, table);
    }
    return live.replace(std::move(current));
}
}
