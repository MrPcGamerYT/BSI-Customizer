#pragma once
#include <QWebEngineUrlRequestInterceptor>

// Apply to the Chromium profile before constructing the WebEngineView.
// OAuth and updater networking remain in the unchanged native managers.
class LocalContentPolicy final : public QWebEngineUrlRequestInterceptor
{
public:
    using QWebEngineUrlRequestInterceptor::QWebEngineUrlRequestInterceptor;
    void interceptRequest(QWebEngineUrlRequestInfo &info) override;
};
