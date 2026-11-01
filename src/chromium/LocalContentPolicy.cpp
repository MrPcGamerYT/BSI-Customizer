#include "LocalContentPolicy.h"
#include <QWebEngineUrlRequestInfo>
#include <QUrl>

void LocalContentPolicy::interceptRequest(QWebEngineUrlRequestInfo &info)
{
    const QUrl url = info.requestUrl();
    const bool localAsset = url.scheme() == QLatin1String("qrc") && url.host().isEmpty()
        && (url.path().startsWith(QLatin1String("/bsi-web/"))
            || url.path() == QLatin1String("/qtwebchannel/qwebchannel.js"));
    const bool previewImage = url.scheme() == QLatin1String("data")
        && info.resourceType() == QWebEngineUrlRequestInfo::ResourceTypeImage;
    info.block(!localAsset && !previewImage);
}
