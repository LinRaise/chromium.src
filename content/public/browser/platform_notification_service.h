// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_PUBLIC_BROWSER_PLATFORM_NOTIFICATION_SERVICE_H_
#define CONTENT_PUBLIC_BROWSER_PLATFORM_NOTIFICATION_SERVICE_H_

#include <string>

#include "base/callback_forward.h"
#include "base/memory/scoped_ptr.h"
#include "content/common/content_export.h"
#include "third_party/WebKit/public/platform/WebNotificationPermission.h"

class GURL;
class SkBitmap;

namespace content {

class BrowserContext;
class DesktopNotificationDelegate;
struct PlatformNotificationData;
class ResourceContext;

// The service using which notifications can be presented to the user. There
// should be a unique instance of the PlatformNotificationService depending
// on the browsing context being used.
class CONTENT_EXPORT PlatformNotificationService {
 public:
  virtual ~PlatformNotificationService() {}

  // Checks if |origin| has permission to display Web Notifications. This method
  // must be called on the IO thread.
  virtual blink::WebNotificationPermission CheckPermission(
      ResourceContext* resource_context,
      const GURL& origin,
      int render_process_id) = 0;

  // Displays the notification described in |params| to the user. A closure
  // through which the notification can be closed will be stored in the
  // |cancel_callback| argument. This method must be called on the UI thread.
  virtual void DisplayNotification(
      BrowserContext* browser_context,
      const GURL& origin,
      const SkBitmap& icon,
      const PlatformNotificationData& notification_data,
      scoped_ptr<DesktopNotificationDelegate> delegate,
      int render_process_id,
      base::Closure* cancel_callback) = 0;

  // Displays the persistent notification described in |notification_data| to
  // the user. This method must be called on the UI thread.
  virtual void DisplayPersistentNotification(
      BrowserContext* browser_context,
      int64 service_worker_registration_id,
      const GURL& origin,
      const SkBitmap& icon,
      const PlatformNotificationData& notification_data,
      int render_process_id) = 0;

  // Closes the persistent notification identified by
  // |persistent_notification_id|. This method must be called on the UI thread.
  virtual void ClosePersistentNotification(
      BrowserContext* browser_context,
      const std::string& persistent_notification_id) = 0;
};

}  // namespace content

#endif  // CONTENT_PUBLIC_BROWSER_PLATFORM_NOTIFICATION_SERVICE_H_
