// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_MEDIA_ANDROID_ROUTER_MEDIA_ROUTER_ANDROID_H_
#define CHROME_BROWSER_MEDIA_ANDROID_ROUTER_MEDIA_ROUTER_ANDROID_H_

#include "chrome/browser/media/router/media_router.h"

namespace content {
class BrowserContext;
}

namespace media_router {

// A stub implementation of MediaRouter interface on Android.
class MediaRouterAndroid : public MediaRouter {
 public:
  ~MediaRouterAndroid() override;

  // MediaRouter implementation.
  void CreateRoute(
      const MediaSource::Id& source_id,
      const MediaSink::Id& sink_id,
      const GURL& origin,
      int tab_id,
      const std::vector<MediaRouteResponseCallback>& callbacks) override;
  void JoinRoute(
      const MediaSource::Id& source,
      const std::string& presentation_id,
      const GURL& origin,
      int tab_id,
      const std::vector<MediaRouteResponseCallback>& callbacks) override;
  void CloseRoute(const MediaRoute::Id& route_id) override;
  void SendRouteMessage(const MediaRoute::Id& route_id,
                        const std::string& message,
                        const SendRouteMessageCallback& callback) override;
  void ListenForRouteMessages(
      const std::vector<MediaRoute::Id>& route_ids,
      const PresentationSessionMessageCallback& message_cb) override;
  void ClearIssue(const Issue::Id& issue_id) override;

 private:
  friend class MediaRouterFactory;

  explicit MediaRouterAndroid(content::BrowserContext*);

  // MediaRouter implementation.
  void RegisterMediaSinksObserver(MediaSinksObserver* observer) override;
  void UnregisterMediaSinksObserver(MediaSinksObserver* observer) override;
  void RegisterMediaRoutesObserver(MediaRoutesObserver* observer) override;
  void UnregisterMediaRoutesObserver(MediaRoutesObserver* observer) override;
  void RegisterIssuesObserver(IssuesObserver* observer) override;
  void UnregisterIssuesObserver(IssuesObserver* observer) override;

  DISALLOW_COPY_AND_ASSIGN(MediaRouterAndroid);
};

} // namespace media_router

#endif  // CHROME_BROWSER_MEDIA_ANDROID_ROUTER_MEDIA_ROUTER_ANDROID_H_
