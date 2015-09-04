// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/frame_host/render_frame_message_filter.h"

#include "base/threading/worker_pool.h"
#include "content/browser/bad_message.h"
#include "content/browser/child_process_security_policy_impl.h"
#include "content/browser/frame_host/render_frame_host_impl.h"
#include "content/browser/gpu/gpu_data_manager_impl.h"
#include "content/browser/renderer_host/render_widget_helper.h"
#include "content/common/frame_messages.h"
#include "content/common/view_messages.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/resource_context.h"
#include "content/public/common/content_constants.h"
#include "gpu/GLES2/gl2extchromium.h"
#include "net/base/keygen_handler.h"
#include "net/cookies/cookie_store.h"
#include "net/url_request/url_request_context.h"
#include "net/url_request/url_request_context_getter.h"
#include "url/gurl.h"

#if defined(OS_MACOSX)
#include "content/common/mac/font_descriptor.h"
#else
#include "third_party/khronos/GLES2/gl2.h"
#include "third_party/khronos/GLES2/gl2ext.h"
#endif

#if defined(OS_WIN)
#include "content/common/font_cache_dispatcher_win.h"
#endif

#if defined(ENABLE_PLUGINS)
#include "content/browser/plugin_service_impl.h"
#include "content/browser/ppapi_plugin_process_host.h"
#include "content/public/browser/plugin_service_filter.h"
#endif

namespace content {

namespace {

#if defined(ENABLE_PLUGINS)
const int kPluginsRefreshThresholdInSeconds = 3;
#endif

void CreateChildFrameOnUI(int process_id,
                          int parent_routing_id,
                          blink::WebTreeScopeType scope,
                          const std::string& frame_name,
                          blink::WebSandboxFlags sandbox_flags,
                          int new_routing_id) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  RenderFrameHostImpl* render_frame_host =
      RenderFrameHostImpl::FromID(process_id, parent_routing_id);
  // Handles the RenderFrameHost being deleted on the UI thread while
  // processing a subframe creation message.
  if (render_frame_host) {
    render_frame_host->OnCreateChildFrame(new_routing_id, scope, frame_name,
                                          sandbox_flags);
  }
}

// Common functionality for converting a sync renderer message to a callback
// function in the browser. Derive from this, create it on the heap when
// issuing your callback. When done, write your reply parameters into
// reply_msg(), and then call SendReplyAndDeleteThis().
class RenderMessageCompletionCallback {
 public:
  RenderMessageCompletionCallback(RenderFrameMessageFilter* filter,
                                  IPC::Message* reply_msg)
      : filter_(filter),
        reply_msg_(reply_msg) {
  }

  virtual ~RenderMessageCompletionCallback() {
    if (reply_msg_) {
      // If the owner of this class failed to call SendReplyAndDeleteThis(),
      // send an error reply to prevent the renderer from being hung.
      reply_msg_->set_reply_error();
      filter_->Send(reply_msg_);
    }
  }

  RenderFrameMessageFilter* filter() { return filter_.get(); }
  IPC::Message* reply_msg() { return reply_msg_; }

  void SendReplyAndDeleteThis() {
    filter_->Send(reply_msg_);
    reply_msg_ = nullptr;
    delete this;
  }

 private:
  scoped_refptr<RenderFrameMessageFilter> filter_;
  IPC::Message* reply_msg_;
};

}  // namespace

#if defined(ENABLE_PLUGINS)

class RenderFrameMessageFilter::OpenChannelToPpapiBrokerCallback
    : public PpapiPluginProcessHost::BrokerClient {
 public:
  OpenChannelToPpapiBrokerCallback(RenderFrameMessageFilter* filter,
                                   int routing_id)
      : filter_(filter),
        routing_id_(routing_id) {
  }

  ~OpenChannelToPpapiBrokerCallback() override {}

  void GetPpapiChannelInfo(base::ProcessHandle* renderer_handle,
                           int* renderer_id) override {
    *renderer_handle = filter_->PeerHandle();
    *renderer_id = filter_->render_process_id_;
  }

  void OnPpapiChannelOpened(const IPC::ChannelHandle& channel_handle,
                            base::ProcessId plugin_pid,
                            int /* plugin_child_id */) override {
    filter_->Send(new ViewMsg_PpapiBrokerChannelCreated(routing_id_,
                                                        plugin_pid,
                                                        channel_handle));
    delete this;
  }

  bool OffTheRecord() override { return filter_->incognito_; }

 private:
  scoped_refptr<RenderFrameMessageFilter> filter_;
  int routing_id_;
};

class RenderFrameMessageFilter::OpenChannelToNpapiPluginCallback
    : public RenderMessageCompletionCallback,
      public PluginProcessHost::Client {
 public:
  OpenChannelToNpapiPluginCallback(RenderFrameMessageFilter* filter,
                                   ResourceContext* context,
                                   IPC::Message* reply_msg)
      : RenderMessageCompletionCallback(filter, reply_msg),
        context_(context),
        host_(nullptr),
        sent_plugin_channel_request_(false) {
  }

  int ID() override { return filter()->render_process_id_; }

  ResourceContext* GetResourceContext() override { return context_; }

  bool OffTheRecord() override {
    if (filter()->incognito_)
      return true;
    if (GetContentClient()->browser()->AllowSaveLocalState(context_))
      return false;

    // For now, only disallow storing data for Flash <http://crbug.com/97319>.
    for (const auto& type : info_.mime_types) {
      if (type.mime_type == kFlashPluginSwfMimeType)
        return true;
    }
    return false;
  }

  void SetPluginInfo(const WebPluginInfo& info) override { info_ = info; }

  void OnFoundPluginProcessHost(PluginProcessHost* host) override {
    DCHECK(host);
    host_ = host;
  }

  void OnSentPluginChannelRequest() override {
    sent_plugin_channel_request_ = true;
  }

  void OnChannelOpened(const IPC::ChannelHandle& handle) override {
    WriteReplyAndDeleteThis(handle);
  }

  void OnError() override { WriteReplyAndDeleteThis(IPC::ChannelHandle()); }

  PluginProcessHost* host() const {
    return host_;
  }

  bool sent_plugin_channel_request() const {
    return sent_plugin_channel_request_;
  }

  void Cancel() {
    delete this;
  }

 private:
  void WriteReplyAndDeleteThis(const IPC::ChannelHandle& handle) {
    FrameHostMsg_OpenChannelToPlugin::WriteReplyParams(reply_msg(),
                                                       handle, info_);
    filter()->OnCompletedOpenChannelToNpapiPlugin(this);
    SendReplyAndDeleteThis();
  }

  ResourceContext* context_;
  WebPluginInfo info_;
  PluginProcessHost* host_;
  bool sent_plugin_channel_request_;
};

class RenderFrameMessageFilter::OpenChannelToPpapiPluginCallback
    : public RenderMessageCompletionCallback,
      public PpapiPluginProcessHost::PluginClient {
 public:
  OpenChannelToPpapiPluginCallback(RenderFrameMessageFilter* filter,
                                   ResourceContext* context,
                                   IPC::Message* reply_msg)
      : RenderMessageCompletionCallback(filter, reply_msg),
        context_(context) {
  }

  void GetPpapiChannelInfo(base::ProcessHandle* renderer_handle,
                           int* renderer_id) override {
    *renderer_handle = filter()->PeerHandle();
    *renderer_id = filter()->render_process_id_;
  }

  void OnPpapiChannelOpened(const IPC::ChannelHandle& channel_handle,
                            base::ProcessId plugin_pid,
                            int plugin_child_id) override {
    FrameHostMsg_OpenChannelToPepperPlugin::WriteReplyParams(
        reply_msg(), channel_handle, plugin_pid, plugin_child_id);
    SendReplyAndDeleteThis();
  }

  bool OffTheRecord() override { return filter()->incognito_; }

  ResourceContext* GetResourceContext() override { return context_; }

 private:
  ResourceContext* context_;
};

#endif  // ENABLE_PLUGINS

RenderFrameMessageFilter::RenderFrameMessageFilter(
    int render_process_id,
    PluginServiceImpl* plugin_service,
    BrowserContext* browser_context,
    net::URLRequestContextGetter* request_context,
    RenderWidgetHelper* render_widget_helper)
    : BrowserMessageFilter(FrameMsgStart),
#if defined(ENABLE_PLUGINS)
      plugin_service_(plugin_service),
      profile_data_directory_(browser_context->GetPath()),
#endif  // ENABLE_PLUGINS
      request_context_(request_context),
      resource_context_(browser_context->GetResourceContext()),
      render_widget_helper_(render_widget_helper),
      incognito_(browser_context->IsOffTheRecord()),
      render_process_id_(render_process_id) {
}

RenderFrameMessageFilter::~RenderFrameMessageFilter() {
  // This function should be called on the IO thread.
  DCHECK_CURRENTLY_ON(BrowserThread::IO);
#if defined(ENABLE_PLUGINS)
  DCHECK(plugin_host_clients_.empty());
#endif  // ENABLE_PLUGINS
}

void RenderFrameMessageFilter::OnChannelClosing() {
#if defined(ENABLE_PLUGINS)
  for (OpenChannelToNpapiPluginCallback* client : plugin_host_clients_) {
    if (client->host()) {
      if (client->sent_plugin_channel_request()) {
        client->host()->CancelSentRequest(client);
      } else {
        client->host()->CancelPendingRequest(client);
      }
    } else {
      plugin_service_->CancelOpenChannelToNpapiPlugin(client);
    }
    client->Cancel();
  }
  plugin_host_clients_.clear();
#endif  // ENABLE_PLUGINS
}

bool RenderFrameMessageFilter::OnMessageReceived(const IPC::Message& message) {
  bool handled = true;
  IPC_BEGIN_MESSAGE_MAP(RenderFrameMessageFilter, message)
    IPC_MESSAGE_HANDLER(FrameHostMsg_CreateChildFrame, OnCreateChildFrame)
    IPC_MESSAGE_HANDLER(FrameHostMsg_SetCookie, OnSetCookie)
    IPC_MESSAGE_HANDLER_DELAY_REPLY(FrameHostMsg_GetCookies, OnGetCookies)
    IPC_MESSAGE_HANDLER(FrameHostMsg_CookiesEnabled, OnCookiesEnabled)
    IPC_MESSAGE_HANDLER(FrameHostMsg_Are3DAPIsBlocked, OnAre3DAPIsBlocked)
    IPC_MESSAGE_HANDLER(FrameHostMsg_DidLose3DContext, OnDidLose3DContext)
    IPC_MESSAGE_HANDLER_DELAY_REPLY(FrameHostMsg_Keygen, OnKeygen)
#if defined(OS_MACOSX)
    IPC_MESSAGE_HANDLER_DELAY_REPLY(FrameHostMsg_LoadFont, OnLoadFont)
#elif defined(OS_WIN)
    IPC_MESSAGE_HANDLER(FrameHostMsg_PreCacheFontCharacters,
                        OnPreCacheFontCharacters)
#endif
#if defined(ENABLE_PLUGINS)
    IPC_MESSAGE_HANDLER_DELAY_REPLY(FrameHostMsg_GetPlugins, OnGetPlugins)
    IPC_MESSAGE_HANDLER(FrameHostMsg_GetPluginInfo, OnGetPluginInfo)
    IPC_MESSAGE_HANDLER_DELAY_REPLY(FrameHostMsg_OpenChannelToPlugin,
                                    OnOpenChannelToPlugin)
    IPC_MESSAGE_HANDLER_DELAY_REPLY(FrameHostMsg_OpenChannelToPepperPlugin,
                                    OnOpenChannelToPepperPlugin)
    IPC_MESSAGE_HANDLER(FrameHostMsg_DidCreateOutOfProcessPepperInstance,
                        OnDidCreateOutOfProcessPepperInstance)
    IPC_MESSAGE_HANDLER(FrameHostMsg_DidDeleteOutOfProcessPepperInstance,
                        OnDidDeleteOutOfProcessPepperInstance)
    IPC_MESSAGE_HANDLER(FrameHostMsg_OpenChannelToPpapiBroker,
                        OnOpenChannelToPpapiBroker)
    IPC_MESSAGE_HANDLER(FrameHostMsg_PluginInstanceThrottleStateChange,
                        OnPluginInstanceThrottleStateChange)
#endif  // ENABLE_PLUGINS
    IPC_MESSAGE_UNHANDLED(handled = false)
  IPC_END_MESSAGE_MAP()

  return handled;
}

void RenderFrameMessageFilter::OnCreateChildFrame(
    int parent_routing_id,
    blink::WebTreeScopeType scope,
    const std::string& frame_name,
    blink::WebSandboxFlags sandbox_flags,
    int* new_routing_id) {
  *new_routing_id = render_widget_helper_->GetNextRoutingID();
  BrowserThread::PostTask(
      BrowserThread::UI, FROM_HERE,
      base::Bind(&CreateChildFrameOnUI, render_process_id_, parent_routing_id,
                 scope, frame_name, sandbox_flags, *new_routing_id));
}

void RenderFrameMessageFilter::OnSetCookie(int render_frame_id,
                                           const GURL& url,
                                           const GURL& first_party_for_cookies,
                                           const std::string& cookie) {
  ChildProcessSecurityPolicyImpl* policy =
      ChildProcessSecurityPolicyImpl::GetInstance();
  if (!policy->CanAccessDataForOrigin(render_process_id_, url)) {
    bad_message::ReceivedBadMessage(this,
                                    bad_message::RFMF_SET_COOKIE_BAD_ORIGIN);
    return;
  }

  net::CookieOptions options;
  if (GetContentClient()->browser()->AllowSetCookie(
          url, first_party_for_cookies, cookie, resource_context_,
          render_process_id_, render_frame_id, &options)) {
    net::URLRequestContext* context = GetRequestContextForURL(url);
    // Pass a null callback since we don't care about when the 'set' completes.
    context->cookie_store()->SetCookieWithOptionsAsync(
        url, cookie, options, net::CookieStore::SetCookiesCallback());
  }
}

void RenderFrameMessageFilter::OnGetCookies(int render_frame_id,
                                            const GURL& url,
                                            const GURL& first_party_for_cookies,
                                            IPC::Message* reply_msg) {
  ChildProcessSecurityPolicyImpl* policy =
      ChildProcessSecurityPolicyImpl::GetInstance();
  if (!policy->CanAccessDataForOrigin(render_process_id_, url)) {
    bad_message::ReceivedBadMessage(this,
                                    bad_message::RFMF_GET_COOKIES_BAD_ORIGIN);
    delete reply_msg;
    return;
  }

  // If we crash here, figure out what URL the renderer was requesting.
  // http://crbug.com/99242
  char url_buf[128];
  base::strlcpy(url_buf, url.spec().c_str(), arraysize(url_buf));
  base::debug::Alias(url_buf);

  net::URLRequestContext* context = GetRequestContextForURL(url);
  context->cookie_store()->GetAllCookiesForURLAsync(
      url, base::Bind(&RenderFrameMessageFilter::CheckPolicyForCookies, this,
                      render_frame_id, url, first_party_for_cookies,
                      reply_msg));
}

void RenderFrameMessageFilter::OnCookiesEnabled(
    int render_frame_id,
    const GURL& url,
    const GURL& first_party_for_cookies,
    bool* cookies_enabled) {
  // TODO(ananta): If this render frame is associated with an automation
  // channel, aka ChromeFrame then we need to retrieve cookie settings from the
  // external host.
  *cookies_enabled = GetContentClient()->browser()->AllowGetCookie(
      url, first_party_for_cookies, net::CookieList(), resource_context_,
      render_process_id_, render_frame_id);
}

void RenderFrameMessageFilter::CheckPolicyForCookies(
    int render_frame_id,
    const GURL& url,
    const GURL& first_party_for_cookies,
    IPC::Message* reply_msg,
    const net::CookieList& cookie_list) {
  net::URLRequestContext* context = GetRequestContextForURL(url);
  // Check the policy for get cookies, and pass cookie_list to the
  // TabSpecificContentSetting for logging purpose.
  if (GetContentClient()->browser()->AllowGetCookie(
          url, first_party_for_cookies, cookie_list, resource_context_,
          render_process_id_, render_frame_id)) {
    // Gets the cookies from cookie store if allowed.
    context->cookie_store()->GetCookiesWithOptionsAsync(
        url, net::CookieOptions(),
        base::Bind(&RenderFrameMessageFilter::SendGetCookiesResponse,
                   this, reply_msg));
  } else {
    SendGetCookiesResponse(reply_msg, std::string());
  }
}

void RenderFrameMessageFilter::SendGetCookiesResponse(
    IPC::Message* reply_msg,
    const std::string& cookies) {
  FrameHostMsg_GetCookies::WriteReplyParams(reply_msg, cookies);
  Send(reply_msg);
}

void RenderFrameMessageFilter::OnAre3DAPIsBlocked(int render_frame_id,
                                                  const GURL& top_origin_url,
                                                  ThreeDAPIType requester,
                                                  bool* blocked) {
  *blocked = GpuDataManagerImpl::GetInstance()->Are3DAPIsBlocked(
      top_origin_url, render_process_id_, render_frame_id, requester);
}

void RenderFrameMessageFilter::OnDidLose3DContext(
    const GURL& top_origin_url,
    ThreeDAPIType /* unused */,
    int arb_robustness_status_code) {
  GpuDataManagerImpl::DomainGuilt guilt;
  switch (arb_robustness_status_code) {
    case GL_GUILTY_CONTEXT_RESET_ARB:
      guilt = GpuDataManagerImpl::DOMAIN_GUILT_KNOWN;
      break;
    case GL_UNKNOWN_CONTEXT_RESET_ARB:
      guilt = GpuDataManagerImpl::DOMAIN_GUILT_UNKNOWN;
      break;
    default:
      // Ignore lost contexts known to be innocent.
      return;
  }

  GpuDataManagerImpl::GetInstance()->BlockDomainFrom3DAPIs(
      top_origin_url, guilt);
}

void RenderFrameMessageFilter::OnKeygen(uint32 key_size_index,
                                        const std::string& challenge_string,
                                        const GURL& url,
                                        IPC::Message* reply_msg) {
  // Map displayed strings indicating level of keysecurity in the <keygen>
  // menu to the key size in bits. (See SSLKeyGeneratorChromium.cpp in WebCore.)
  int key_size_in_bits;
  switch (key_size_index) {
    case 0:
      key_size_in_bits = 2048;
      break;
    case 1:
      key_size_in_bits = 1024;
      break;
    default:
      DCHECK(false) << "Illegal key_size_index " << key_size_index;
      FrameHostMsg_Keygen::WriteReplyParams(reply_msg, std::string());
      Send(reply_msg);
      return;
  }

  resource_context_->CreateKeygenHandler(
      key_size_in_bits,
      challenge_string,
      url,
      base::Bind(&RenderFrameMessageFilter::PostKeygenToWorkerThread,
                 this, reply_msg));
}

void RenderFrameMessageFilter::PostKeygenToWorkerThread(
    IPC::Message* reply_msg,
    scoped_ptr<net::KeygenHandler> keygen_handler) {
  VLOG(1) << "Dispatching keygen task to worker pool.";
  // Dispatch to worker pool, so we do not block the IO thread.
  if (!base::WorkerPool::PostTask(
           FROM_HERE,
           base::Bind(&RenderFrameMessageFilter::OnKeygenOnWorkerThread,
                      this,
                      base::Passed(&keygen_handler),
                      reply_msg),
           true)) {
    NOTREACHED() << "Failed to dispatch keygen task to worker pool";
    FrameHostMsg_Keygen::WriteReplyParams(reply_msg, std::string());
    Send(reply_msg);
  }
}

void RenderFrameMessageFilter::OnKeygenOnWorkerThread(
    scoped_ptr<net::KeygenHandler> keygen_handler,
    IPC::Message* reply_msg) {
  DCHECK(reply_msg);

  // Generate a signed public key and challenge, then send it back.
  FrameHostMsg_Keygen::WriteReplyParams(
      reply_msg,
      keygen_handler->GenKeyAndSignChallenge());
  Send(reply_msg);
}

#if defined(OS_MACOSX)

void RenderFrameMessageFilter::OnLoadFont(const FontDescriptor& font,
                                          IPC::Message* reply_msg) {
  FontLoader::Result* result = new FontLoader::Result;

  BrowserThread::PostTaskAndReply(
      BrowserThread::FILE, FROM_HERE,
      base::Bind(&FontLoader::LoadFont, font, result),
      base::Bind(&RenderFrameMessageFilter::SendLoadFontReply, this, reply_msg,
                 base::Owned(result)));
}

void RenderFrameMessageFilter::SendLoadFontReply(IPC::Message* reply,
                                                 FontLoader::Result* result) {
  base::SharedMemoryHandle handle;
  if (result->font_data_size == 0 || result->font_id == 0) {
    result->font_data_size = 0;
    result->font_id = 0;
    handle = base::SharedMemory::NULLHandle();
  } else {
    result->font_data.GiveToProcess(base::GetCurrentProcessHandle(), &handle);
  }
  FrameHostMsg_LoadFont::WriteReplyParams(
      reply, result->font_data_size, handle, result->font_id);
  Send(reply);
}

#elif defined(OS_WIN)

void RenderFrameMessageFilter::OnPreCacheFontCharacters(
    const LOGFONT& font,
    const base::string16& str) {
  // TODO(scottmg): pdf/ppapi still require the renderer to be able to precache
  // GDI fonts (http://crbug.com/383227), even when using DirectWrite.
  // Eventually this shouldn't be added and should be moved to
  // FontCacheDispatcher too. http://crbug.com/356346.

  // First, comments from FontCacheDispatcher::OnPreCacheFont do apply here too.
  // Except that for True Type fonts,
  // GetTextMetrics will not load the font in memory.
  // The only way windows seem to load properly, it is to create a similar
  // device (like the one in which we print), then do an ExtTextOut,
  // as we do in the printing thread, which is sandboxed.
  HDC hdc = CreateEnhMetaFile(NULL, NULL, NULL, NULL);
  HFONT font_handle = CreateFontIndirect(&font);
  DCHECK(NULL != font_handle);

  HGDIOBJ old_font = SelectObject(hdc, font_handle);
  DCHECK(NULL != old_font);

  ExtTextOut(hdc, 0, 0, ETO_GLYPH_INDEX, 0, str.c_str(), str.length(), NULL);

  SelectObject(hdc, old_font);
  DeleteObject(font_handle);

  HENHMETAFILE metafile = CloseEnhMetaFile(hdc);

  if (metafile)
    DeleteEnhMetaFile(metafile);
}

#endif  // OS_MACOSX

#if defined(ENABLE_PLUGINS)

void RenderFrameMessageFilter::OnGetPlugins(
    bool refresh,
    IPC::Message* reply_msg) {
  // Don't refresh if the specified threshold has not been passed.  Note that
  // this check is performed before off-loading to the file thread.  The reason
  // we do this is that some pages tend to request that the list of plugins be
  // refreshed at an excessive rate.  This instigates disk scanning, as the list
  // is accumulated by doing multiple reads from disk.  This effect is
  // multiplied when we have several pages requesting this operation.
  if (refresh) {
    const base::TimeDelta threshold = base::TimeDelta::FromSeconds(
        kPluginsRefreshThresholdInSeconds);
    const base::TimeTicks now = base::TimeTicks::Now();
    if (now - last_plugin_refresh_time_ >= threshold) {
      // Only refresh if the threshold hasn't been exceeded yet.
      PluginServiceImpl::GetInstance()->RefreshPlugins();
      last_plugin_refresh_time_ = now;
    }
  }

  PluginServiceImpl::GetInstance()->GetPlugins(base::Bind(
      &RenderFrameMessageFilter::GetPluginsCallback, this, reply_msg));
}

void RenderFrameMessageFilter::GetPluginsCallback(
    IPC::Message* reply_msg,
    const std::vector<WebPluginInfo>& all_plugins) {
  // Filter the plugin list.
  PluginServiceFilter* filter = PluginServiceImpl::GetInstance()->GetFilter();
  std::vector<WebPluginInfo> plugins;

  int child_process_id = -1;
  int routing_id = MSG_ROUTING_NONE;
  // In this loop, copy the WebPluginInfo (and do not use a reference) because
  // the filter might mutate it.
  for (WebPluginInfo plugin : all_plugins) {
    if (!filter || filter->IsPluginAvailable(child_process_id,
                                             routing_id,
                                             resource_context_,
                                             GURL(),
                                             GURL(),
                                             &plugin)) {
      plugins.push_back(plugin);
    }
  }

  FrameHostMsg_GetPlugins::WriteReplyParams(reply_msg, plugins);
  Send(reply_msg);
}

void RenderFrameMessageFilter::OnGetPluginInfo(
    int render_frame_id,
    const GURL& url,
    const GURL& page_url,
    const std::string& mime_type,
    bool* found,
    WebPluginInfo* info,
    std::string* actual_mime_type) {
  bool allow_wildcard = true;
  *found = plugin_service_->GetPluginInfo(
      render_process_id_, render_frame_id, resource_context_,
      url, page_url, mime_type, allow_wildcard,
      nullptr, info, actual_mime_type);
}

void RenderFrameMessageFilter::OnOpenChannelToPlugin(
    int render_frame_id,
    const GURL& url,
    const GURL& policy_url,
    const std::string& mime_type,
    IPC::Message* reply_msg) {
  OpenChannelToNpapiPluginCallback* client =
      new OpenChannelToNpapiPluginCallback(this, resource_context_, reply_msg);
  DCHECK(!ContainsKey(plugin_host_clients_, client));
  plugin_host_clients_.insert(client);
  plugin_service_->OpenChannelToNpapiPlugin(
      render_process_id_, render_frame_id,
      url, policy_url, mime_type, client);
}

void RenderFrameMessageFilter::OnCompletedOpenChannelToNpapiPlugin(
    OpenChannelToNpapiPluginCallback* client) {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);
  DCHECK(ContainsKey(plugin_host_clients_, client));
  plugin_host_clients_.erase(client);
}

void RenderFrameMessageFilter::OnOpenChannelToPepperPlugin(
    const base::FilePath& path,
    IPC::Message* reply_msg) {
  plugin_service_->OpenChannelToPpapiPlugin(
      render_process_id_,
      path,
      profile_data_directory_,
      new OpenChannelToPpapiPluginCallback(this, resource_context_, reply_msg));
}

void RenderFrameMessageFilter::OnDidCreateOutOfProcessPepperInstance(
    int plugin_child_id,
    int32 pp_instance,
    PepperRendererInstanceData instance_data,
    bool is_external) {
  // It's important that we supply the render process ID ourselves based on the
  // channel the message arrived on. We use the
  //   PP_Instance -> (process id, frame id)
  // mapping to decide how to handle messages received from the (untrusted)
  // plugin, so an exploited renderer must not be able to insert fake mappings
  // that may allow it access to other render processes.
  DCHECK_EQ(0, instance_data.render_process_id);
  instance_data.render_process_id = render_process_id_;
  if (is_external) {
    // We provide the BrowserPpapiHost to the embedder, so it's safe to cast.
    BrowserPpapiHostImpl* host = static_cast<BrowserPpapiHostImpl*>(
        GetContentClient()->browser()->GetExternalBrowserPpapiHost(
            plugin_child_id));
    if (host)
      host->AddInstance(pp_instance, instance_data);
  } else {
    PpapiPluginProcessHost::DidCreateOutOfProcessInstance(
        plugin_child_id, pp_instance, instance_data);
  }
}

void RenderFrameMessageFilter::OnDidDeleteOutOfProcessPepperInstance(
    int plugin_child_id,
    int32 pp_instance,
    bool is_external) {
  if (is_external) {
    // We provide the BrowserPpapiHost to the embedder, so it's safe to cast.
    BrowserPpapiHostImpl* host = static_cast<BrowserPpapiHostImpl*>(
        GetContentClient()->browser()->GetExternalBrowserPpapiHost(
            plugin_child_id));
    if (host)
      host->DeleteInstance(pp_instance);
  } else {
    PpapiPluginProcessHost::DidDeleteOutOfProcessInstance(
        plugin_child_id, pp_instance);
  }
}

void RenderFrameMessageFilter::OnOpenChannelToPpapiBroker(
    int routing_id,
    const base::FilePath& path) {
  plugin_service_->OpenChannelToPpapiBroker(
      render_process_id_,
      path,
      new OpenChannelToPpapiBrokerCallback(this, routing_id));
}

void RenderFrameMessageFilter::OnPluginInstanceThrottleStateChange(
    int plugin_child_id,
    int32 pp_instance,
    bool is_throttled) {
  // Feature is only implemented for non-external Plugins.
  PpapiPluginProcessHost::OnPluginInstanceThrottleStateChange(
      plugin_child_id, pp_instance, is_throttled);
}

#endif  // ENABLE_PLUGINS

net::URLRequestContext* RenderFrameMessageFilter::GetRequestContextForURL(
    const GURL& url) {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);

  net::URLRequestContext* context =
      GetContentClient()->browser()->OverrideRequestContextForURL(
          url, resource_context_);
  if (!context)
    context = request_context_->GetURLRequestContext();

  return context;
}

}  // namespace content
