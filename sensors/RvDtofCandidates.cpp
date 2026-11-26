#include "RvDtofCandidates.h"
#include "RvImagingSchemes.h"
namespace RvDtofCandidates {
QString name(int mode)
{
    for (const auto& scheme : ImagingSchemes)
        if (scheme.boardMode == mode) return QLatin1String(scheme.name);
    return {};
}
int modeForScheme(const QString &name)
{
    const auto* scheme = schemeForName(name);
    return scheme ? scheme->boardMode : -1;
}
}
