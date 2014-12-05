// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/push_messaging/push_messaging_message_filter.h"

#include <string>

#include "base/bind.h"
#include "base/logging.h"
#include "base/metrics/histogram.h"
#include "base/strings/string_number_conversions.h"
#include "content/browser/renderer_host/render_process_host_impl.h"
#include "content/browser/service_worker/service_worker_context_wrapper.h"
#include "content/common/push_messaging_messages.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/push_messaging_service.h"
#include "content/public/common/child_process_host.h"
#include "third_party/WebKit/public/platform/WebPushPermissionStatus.h"

namespace content {
namespace {

void RecordRegistrationStatus(PushRegistrationStatus status) {
  UMA_HISTOGRAM_ENUMERATION("PushMessaging.RegistrationStatus",
                            status,
                            PUSH_REGISTRATION_STATUS_LAST + 1);
}

}  // namespace

PushMessagingMessageFilter::RegisterData::RegisterData()
    : request_id(0),
      service_worker_registration_id(0),
      render_frame_id(ChildProcessHost::kInvalidUniqueID),
      user_visible_only(false) {}

bool PushMessagingMessageFilter::RegisterData::FromDocument() const {
  return render_frame_id != ChildProcessHost::kInvalidUniqueID;
}

PushMessagingMessageFilter::PushMessagingMessageFilter(
    int render_process_id,
    ServiceWorkerContextWrapper* service_worker_context)
    : BrowserMessageFilter(PushMessagingMsgStart),
      render_process_id_(render_process_id),
      service_worker_context_(service_worker_context),
      service_(NULL),
      weak_factory_ui_to_ui_(this) {
}

PushMessagingMessageFilter::~PushMessagingMessageFilter() {}

bool PushMessagingMessageFilter::OnMessageReceived(
    const IPC::Message& message) {
  bool handled = true;
  IPC_BEGIN_MESSAGE_MAP(PushMessagingMessageFilter, message)
    IPC_MESSAGE_HANDLER(PushMessagingHostMsg_RegisterFromDocument,
                        OnRegisterFromDocument)
    IPC_MESSAGE_HANDLER(PushMessagingHostMsg_RegisterFromWorker,
                        OnRegisterFromWorker)
    IPC_MESSAGE_HANDLER(PushMessagingHostMsg_PermissionStatus,
                        OnPermissionStatusRequest)
    IPC_MESSAGE_HANDLER(PushMessagingHostMsg_GetPermissionStatus,
                        OnGetPermissionStatus)
    IPC_MESSAGE_UNHANDLED(handled = false)
  IPC_END_MESSAGE_MAP()
  return handled;
}

void PushMessagingMessageFilter::OnRegisterFromDocument(
    int render_frame_id,
    int request_id,
    const std::string& sender_id,
    bool user_visible_only,
    int service_worker_provider_id) {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);
  // TODO(mvanouwerkerk): Validate arguments?
  ServiceWorkerProviderHost* service_worker_host =
      service_worker_context_->context()->GetProviderHost(
          render_process_id_, service_worker_provider_id);
  if (!service_worker_host || !service_worker_host->active_version()) {
    PushRegistrationStatus status =
        PUSH_REGISTRATION_STATUS_NO_SERVICE_WORKER;
    Send(new PushMessagingMsg_RegisterFromDocumentError(render_frame_id,
                                                        request_id, status));
    RecordRegistrationStatus(status);
    return;
  }

  // TODO(mvanouwerkerk): Persist sender id in Service Worker storage.
  // https://crbug.com/437298

  // TODO(peter): Persist |user_visible_only| in Service Worker storage.

  RegisterData data;
  data.request_id = request_id;
  data.requesting_origin =
      service_worker_host->active_version()->scope().GetOrigin();
  data.service_worker_registration_id =
      service_worker_host->active_version()->registration_id();
  data.render_frame_id = render_frame_id;
  data.user_visible_only = user_visible_only;

  BrowserThread::PostTask(
      BrowserThread::UI, FROM_HERE,
      base::Bind(&PushMessagingMessageFilter::RegisterOnUI,
                 this, data, sender_id));
}

void PushMessagingMessageFilter::OnRegisterFromWorker(
    int request_id,
    int64 service_worker_registration_id) {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);
  ServiceWorkerRegistration* service_worker_registration =
      service_worker_context_->context()->GetLiveRegistration(
          service_worker_registration_id);
  DCHECK(service_worker_registration);
  if (!service_worker_registration)
    return;

  // TODO(mvanouwerkerk): Get sender id from Service Worker storage.
  // https://crbug.com/437298
  std::string sender_id = "";

  RegisterData data;
  data.request_id = request_id;
  data.requesting_origin = service_worker_registration->pattern().GetOrigin();
  data.service_worker_registration_id = service_worker_registration_id;

  BrowserThread::PostTask(
      BrowserThread::UI, FROM_HERE,
      base::Bind(&PushMessagingMessageFilter::RegisterOnUI,
                 this, data, sender_id));
}

void PushMessagingMessageFilter::OnPermissionStatusRequest(
    int render_frame_id,
    int service_worker_provider_id,
    int permission_callback_id) {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);
  ServiceWorkerProviderHost* service_worker_host =
      service_worker_context_->context()->GetProviderHost(
          render_process_id_, service_worker_provider_id);

  if (service_worker_host && service_worker_host->active_version()) {
    BrowserThread::PostTask(
        BrowserThread::UI,
        FROM_HERE,
        base::Bind(&PushMessagingMessageFilter::DoPermissionStatusRequest,
                   this,
                   service_worker_host->active_version()->scope().GetOrigin(),
                   render_frame_id,
                   permission_callback_id));
  } else {
    Send(new PushMessagingMsg_PermissionStatusFailure(
          render_frame_id, permission_callback_id));
  }
}

void PushMessagingMessageFilter::OnGetPermissionStatus(
    int request_id,
    int64 service_worker_registration_id) {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);
  ServiceWorkerRegistration* service_worker_registration =
      service_worker_context_->context()->GetLiveRegistration(
          service_worker_registration_id);
  DCHECK(service_worker_registration);
  if (!service_worker_registration)
    return;

  BrowserThread::PostTask(
      BrowserThread::UI, FROM_HERE,
      base::Bind(&PushMessagingMessageFilter::GetPermissionStatusOnUI,
                 this,
                 service_worker_registration->pattern().GetOrigin(),
                 request_id));
}

void PushMessagingMessageFilter::RegisterOnUI(
    const RegisterData& data,
    const std::string& sender_id) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  if (!service()) {
    SendRegisterError(data, PUSH_REGISTRATION_STATUS_SERVICE_NOT_AVAILABLE);
    return;
  }

  if (data.FromDocument()) {
    service()->RegisterFromDocument(
        data.requesting_origin, data.service_worker_registration_id, sender_id,
        render_process_id_, data.render_frame_id, data.user_visible_only,
        base::Bind(&PushMessagingMessageFilter::DidRegister,
                   weak_factory_ui_to_ui_.GetWeakPtr(), data));
  } else {
    service()->RegisterFromWorker(
        data.requesting_origin, data.service_worker_registration_id, sender_id,
        base::Bind(&PushMessagingMessageFilter::DidRegister,
                   weak_factory_ui_to_ui_.GetWeakPtr(), data));
  }
}

void PushMessagingMessageFilter::DoPermissionStatusRequest(
    const GURL& requesting_origin,
    int render_frame_id,
    int callback_id) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  if (!service()) {
    Send(new PushMessagingMsg_PermissionStatusFailure(render_frame_id,
                                                      callback_id));
    return;
  }
  blink::WebPushPermissionStatus permission_value =
      service()->GetPermissionStatus(
          requesting_origin, render_process_id_, render_frame_id);

  Send(new PushMessagingMsg_PermissionStatusResult(
      render_frame_id, callback_id, permission_value));
}

void PushMessagingMessageFilter::GetPermissionStatusOnUI(
    const GURL& requesting_origin,
    int request_id) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  if (!service()) {
    Send(new PushMessagingMsg_GetPermissionStatusError(request_id));
    return;
  }
  GURL embedding_origin = requesting_origin;
  blink::WebPushPermissionStatus permission_status =
      service()->GetPermissionStatus(requesting_origin, embedding_origin);
  Send(new PushMessagingMsg_GetPermissionStatusSuccess(request_id,
                                                       permission_status));
}

void PushMessagingMessageFilter::DidRegister(
    const RegisterData& data,
    const std::string& push_registration_id,
    PushRegistrationStatus status) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  if (status == PUSH_REGISTRATION_STATUS_SUCCESS) {
    SendRegisterSuccess(data, push_registration_id);
  } else {
    SendRegisterError(data, status);
  }
}

void PushMessagingMessageFilter::SendRegisterError(
    const RegisterData& data, PushRegistrationStatus status) {
  if (data.FromDocument()) {
    Send(new PushMessagingMsg_RegisterFromDocumentError(
      data.render_frame_id, data.request_id, status));
  } else {
    Send(new PushMessagingMsg_RegisterFromWorkerError(
      data.request_id, status));
  }
  RecordRegistrationStatus(status);
}

void PushMessagingMessageFilter::SendRegisterSuccess(
    const RegisterData& data, const std::string& push_registration_id) {
  GURL push_endpoint(service()->PushEndpoint());
  if (data.FromDocument()) {
    Send(new PushMessagingMsg_RegisterFromDocumentSuccess(
        data.render_frame_id,
        data.request_id, push_endpoint, push_registration_id));
  } else {
    Send(new PushMessagingMsg_RegisterFromWorkerSuccess(
        data.request_id, push_endpoint, push_registration_id));
  }
  RecordRegistrationStatus(PUSH_REGISTRATION_STATUS_SUCCESS);
}

PushMessagingService* PushMessagingMessageFilter::service() {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  if (!service_) {
    RenderProcessHost* process_host =
        RenderProcessHost::FromID(render_process_id_);
    if (!process_host)
      return NULL;
    service_ = process_host->GetBrowserContext()->GetPushMessagingService();
  }
  return service_;
}

}  // namespace content
