// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_PUBLIC_BROWSER_PUSH_MESSAGING_SERVICE_H_
#define CONTENT_PUBLIC_BROWSER_PUSH_MESSAGING_SERVICE_H_

#include <string>

#include "base/callback.h"
#include "content/common/content_export.h"
#include "content/public/common/push_messaging_status.h"
#include "third_party/WebKit/public/platform/WebPushPermissionStatus.h"
#include "url/gurl.h"

namespace content {

// A push service-agnostic interface that the Push API uses for talking to
// push messaging services like GCM. Must only be used on the UI thread.
class CONTENT_EXPORT PushMessagingService {
 public:
  typedef base::Callback<void(const std::string& /* registration_id */,
                              PushRegistrationStatus /* status */)>
      RegisterCallback;

  virtual ~PushMessagingService() {}

  // Returns the absolute URL exposed by the push server where the webapp server
  // can send push messages. This is currently assumed to be the same for all
  // origins and push registrations.
  virtual GURL PushEndpoint() = 0;

  // Register the given |sender_id| with the push messaging service in a
  // document context. The frame is known and a permission UI may be displayed
  // to the user.
  virtual void RegisterFromDocument(const GURL& requesting_origin,
                                    int64 service_worker_registration_id,
                                    const std::string& sender_id,
                                    int renderer_id,
                                    int render_frame_id,
                                    bool user_visible_only,
                                    const RegisterCallback& callback) = 0;

  // Register the given |sender_id| with the push messaging service. The frame
  // is not known so if permission was not previously granted by the user this
  // request should fail.
  virtual void RegisterFromWorker(const GURL& requesting_origin,
                                  int64 service_worker_registration_id,
                                  const std::string& sender_id,
                                  const RegisterCallback& callback) = 0;

  // Check whether the requester has permission to register for Push
  // Messages
  // TODO(mvanouwerkerk): Delete once the Push API flows through platform.
  // https://crbug.com/389194
  virtual blink::WebPushPermissionStatus GetPermissionStatus(
      const GURL& requesting_origin,
      int renderer_id,
      int render_frame_id) = 0;

  // Checks the permission status for the requesting origin. Permission is only
  // ever granted when the requesting origin matches the top level embedding
  // origin.
  virtual blink::WebPushPermissionStatus GetPermissionStatus(
      const GURL& requesting_origin,
      const GURL& embedding_origin) = 0;
};

}  // namespace content

#endif  // CONTENT_PUBLIC_BROWSER_PUSH_MESSAGING_SERVICE_H_
