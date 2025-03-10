// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/web_contents/web_contents_impl.h"

#include <utility>

#include "base/command_line.h"
#include "base/lazy_instance.h"
#include "base/location.h"
#include "base/logging.h"
#include "base/metrics/histogram.h"
#include "base/process/process.h"
#include "base/profiler/scoped_tracker.h"
#include "base/single_thread_task_runner.h"
#include "base/strings/string16.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/thread_task_runner_handle.h"
#include "base/time/time.h"
#include "base/trace_event/trace_event.h"
#include "components/mime_util/mime_util.h"
#include "components/url_formatter/url_formatter.h"
#include "content/browser/accessibility/accessibility_mode_helper.h"
#include "content/browser/accessibility/browser_accessibility_state_impl.h"
#include "content/browser/bad_message.h"
#include "content/browser/browser_plugin/browser_plugin_embedder.h"
#include "content/browser/browser_plugin/browser_plugin_guest.h"
#include "content/browser/child_process_security_policy_impl.h"
#include "content/browser/dom_storage/dom_storage_context_wrapper.h"
#include "content/browser/dom_storage/session_storage_namespace_impl.h"
#include "content/browser/download/download_stats.h"
#include "content/browser/download/mhtml_generation_manager.h"
#include "content/browser/download/save_package.h"
#include "content/browser/frame_host/cross_process_frame_connector.h"
#include "content/browser/frame_host/interstitial_page_impl.h"
#include "content/browser/frame_host/navigation_entry_impl.h"
#include "content/browser/frame_host/navigation_handle_impl.h"
#include "content/browser/frame_host/navigator_impl.h"
#include "content/browser/frame_host/render_frame_host_impl.h"
#include "content/browser/frame_host/render_widget_host_view_child_frame.h"
#include "content/browser/geolocation/geolocation_service_context.h"
#include "content/browser/host_zoom_map_impl.h"
#include "content/browser/loader/resource_dispatcher_host_impl.h"
#include "content/browser/manifest/manifest_manager_host.h"
#include "content/browser/media/audio_stream_monitor.h"
#include "content/browser/media/capture/web_contents_audio_muter.h"
#include "content/browser/message_port_message_filter.h"
#include "content/browser/plugin_content_origin_whitelist.h"
#include "content/browser/power_save_blocker_impl.h"
#include "content/browser/renderer_host/render_process_host_impl.h"
#include "content/browser/renderer_host/render_view_host_delegate_view.h"
#include "content/browser/renderer_host/render_view_host_impl.h"
#include "content/browser/renderer_host/render_widget_host_impl.h"
#include "content/browser/renderer_host/render_widget_host_input_event_router.h"
#include "content/browser/renderer_host/render_widget_host_view_base.h"
#include "content/browser/screen_orientation/screen_orientation_dispatcher_host_impl.h"
#include "content/browser/site_instance_impl.h"
#include "content/browser/wake_lock/wake_lock_service_context.h"
#include "content/browser/web_contents/web_contents_view_guest.h"
#include "content/browser/webui/generic_handler.h"
#include "content/browser/webui/web_ui_controller_factory_registry.h"
#include "content/browser/webui/web_ui_impl.h"
#include "content/common/browser_plugin/browser_plugin_constants.h"
#include "content/common/browser_plugin/browser_plugin_messages.h"
#include "content/common/frame_messages.h"
#include "content/common/input_messages.h"
#include "content/common/site_isolation_policy.h"
#include "content/common/ssl_status_serialization.h"
#include "content/common/view_messages.h"
#include "content/public/browser/ax_event_notification_details.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/browser_plugin_guest_manager.h"
#include "content/public/browser/content_browser_client.h"
#include "content/public/browser/devtools_agent_host.h"
#include "content/public/browser/download_manager.h"
#include "content/public/browser/download_url_parameters.h"
#include "content/public/browser/invalidate_type.h"
#include "content/public/browser/javascript_dialog_manager.h"
#include "content/public/browser/load_from_memory_cache_details.h"
#include "content/public/browser/load_notification_details.h"
#include "content/public/browser/navigation_details.h"
#include "content/public/browser/notification_details.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/render_widget_host_iterator.h"
#include "content/public/browser/resource_request_details.h"
#include "content/public/browser/screen_orientation_dispatcher_host.h"
#include "content/public/browser/security_style_explanations.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/user_metrics.h"
#include "content/public/browser/web_contents_delegate.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/common/bindings_policy.h"
#include "content/public/common/browser_plugin_guest_mode.h"
#include "content/public/common/content_constants.h"
#include "content/public/common/content_switches.h"
#include "content/public/common/page_zoom.h"
#include "content/public/common/result_codes.h"
#include "content/public/common/security_style.h"
#include "content/public/common/url_constants.h"
#include "content/public/common/url_utils.h"
#include "content/public/common/web_preferences.h"
#include "mojo/common/url_type_converters.h"
#include "mojo/converters/geometry/geometry_type_converters.h"
#include "net/http/http_cache.h"
#include "net/http/http_transaction_factory.h"
#include "net/url_request/url_request_context.h"
#include "net/url_request/url_request_context_getter.h"
#include "skia/public/type_converters.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/base/layout.h"
#include "ui/gfx/display.h"
#include "ui/gfx/screen.h"
#include "ui/gl/gl_switches.h"

#if defined(ENABLE_BROWSER_CDMS)
#include "content/browser/media/media_web_contents_observer.h"
#endif

#if defined(OS_ANDROID)
#include "content/browser/android/content_video_view.h"
#include "content/browser/media/android/media_session.h"
#endif  // OS_ANDROID

#if defined(OS_ANDROID) && !defined(USE_AURA)
#include "content/browser/android/date_time_chooser_android.h"
#include "content/browser/web_contents/web_contents_android.h"
#endif  // OS_ANDROID && !USE_AURA

#if defined(OS_MACOSX)
#include "base/mac/foundation_util.h"
#endif

namespace content {
namespace {

const int kMinimumDelayBetweenLoadingUpdatesMS = 100;
const char kDotGoogleDotCom[] = ".google.com";

#if defined(OS_ANDROID)
const char kWebContentsAndroidKey[] = "web_contents_android";
#endif  // OS_ANDROID

base::LazyInstance<std::vector<WebContentsImpl::CreatedCallback> >
g_created_callbacks = LAZY_INSTANCE_INITIALIZER;

static void DidDownloadImage(const WebContents::ImageDownloadCallback& callback,
                             int id,
                             const GURL& image_url,
                             image_downloader::DownloadResultPtr result) {
  DCHECK(result);

  const std::vector<SkBitmap> images =
      result->images.To<std::vector<SkBitmap>>();
  const std::vector<gfx::Size> original_image_sizes =
      result->original_image_sizes.To<std::vector<gfx::Size>>();

  callback.Run(id, result->http_status_code, image_url, images,
               original_image_sizes);
}

void NotifyCacheOnIO(
    scoped_refptr<net::URLRequestContextGetter> request_context,
    const GURL& url,
    const std::string& http_method) {
  net::HttpCache* cache = request_context->GetURLRequestContext()->
      http_transaction_factory()->GetCache();
  if (cache)
    cache->OnExternalCacheHit(url, http_method);
}

bool FindMatchingProcess(int render_process_id,
                         bool* did_match_process,
                         FrameTreeNode* node) {
  if (node->current_frame_host()->GetProcess()->GetID() == render_process_id) {
    *did_match_process = true;
    return false;
  }
  return true;
}

bool ForEachFrameInternal(
    const base::Callback<void(RenderFrameHost*)>& on_frame,
    FrameTreeNode* node) {
  on_frame.Run(node->current_frame_host());
  return true;
}

bool ForEachPendingFrameInternal(
    const base::Callback<void(RenderFrameHost*)>& on_frame,
    FrameTreeNode* node) {
  RenderFrameHost* pending_frame_host =
      node->render_manager()->pending_frame_host();
  if (pending_frame_host)
    on_frame.Run(pending_frame_host);
  return true;
}

void SendToAllFramesInternal(IPC::Message* message, RenderFrameHost* rfh) {
  IPC::Message* message_copy = new IPC::Message(*message);
  message_copy->set_routing_id(rfh->GetRoutingID());
  rfh->Send(message_copy);
}

void AddRenderWidgetHostViewToSet(std::set<RenderWidgetHostView*>* set,
                                  RenderFrameHost* rfh) {
  RenderWidgetHostView* rwhv = static_cast<RenderFrameHostImpl*>(rfh)
                                   ->frame_tree_node()
                                   ->render_manager()
                                   ->GetRenderWidgetHostView();
  set->insert(rwhv);
}

void SetAccessibilityModeOnFrame(AccessibilityMode mode,
                                 RenderFrameHost* frame_host) {
  static_cast<RenderFrameHostImpl*>(frame_host)->SetAccessibilityMode(mode);
}

void ResetAccessibility(RenderFrameHost* rfh) {
  static_cast<RenderFrameHostImpl*>(rfh)->AccessibilityReset();
}

}  // namespace

WebContents* WebContents::Create(const WebContents::CreateParams& params) {
  FrameTreeNode* opener_node = nullptr;
  if (params.opener_render_frame_id != MSG_ROUTING_NONE) {
    RenderFrameHostImpl* opener_rfh = RenderFrameHostImpl::FromID(
        params.opener_render_process_id, params.opener_render_frame_id);
    if (opener_rfh)
      opener_node = opener_rfh->frame_tree_node();
  }
  return WebContentsImpl::CreateWithOpener(params, opener_node);
}

WebContents* WebContents::CreateWithSessionStorage(
    const WebContents::CreateParams& params,
    const SessionStorageNamespaceMap& session_storage_namespace_map) {
  WebContentsImpl* new_contents = new WebContentsImpl(params.browser_context);

  for (SessionStorageNamespaceMap::const_iterator it =
           session_storage_namespace_map.begin();
       it != session_storage_namespace_map.end();
       ++it) {
    new_contents->GetController()
        .SetSessionStorageNamespace(it->first, it->second.get());
  }

  new_contents->Init(params);
  return new_contents;
}

void WebContentsImpl::FriendZone::AddCreatedCallbackForTesting(
    const CreatedCallback& callback) {
  g_created_callbacks.Get().push_back(callback);
}

void WebContentsImpl::FriendZone::RemoveCreatedCallbackForTesting(
    const CreatedCallback& callback) {
  for (size_t i = 0; i < g_created_callbacks.Get().size(); ++i) {
    if (g_created_callbacks.Get().at(i).Equals(callback)) {
      g_created_callbacks.Get().erase(g_created_callbacks.Get().begin() + i);
      return;
    }
  }
}

WebContents* WebContents::FromRenderViewHost(RenderViewHost* rvh) {
  if (!rvh)
    return nullptr;
  return rvh->GetDelegate()->GetAsWebContents();
}

WebContents* WebContents::FromRenderFrameHost(RenderFrameHost* rfh) {
  if (!rfh)
    return nullptr;
  return static_cast<RenderFrameHostImpl*>(rfh)->delegate()->GetAsWebContents();
}

// WebContentsImpl::DestructionObserver ----------------------------------------

class WebContentsImpl::DestructionObserver : public WebContentsObserver {
 public:
  DestructionObserver(WebContentsImpl* owner, WebContents* watched_contents)
      : WebContentsObserver(watched_contents),
        owner_(owner) {
  }

  // WebContentsObserver:
  void WebContentsDestroyed() override {
    owner_->OnWebContentsDestroyed(
        static_cast<WebContentsImpl*>(web_contents()));
  }

 private:
  WebContentsImpl* owner_;

  DISALLOW_COPY_AND_ASSIGN(DestructionObserver);
};

WebContentsImpl::ColorChooserInfo::ColorChooserInfo(int render_process_id,
                                                    int render_frame_id,
                                                    ColorChooser* chooser,
                                                    int identifier)
    : render_process_id(render_process_id),
      render_frame_id(render_frame_id),
      chooser(chooser),
      identifier(identifier) {
}

WebContentsImpl::ColorChooserInfo::~ColorChooserInfo() {
}

// WebContentsImpl::WebContentsTreeNode ----------------------------------------
WebContentsImpl::WebContentsTreeNode::WebContentsTreeNode()
    : outer_web_contents_(nullptr),
      outer_contents_frame_tree_node_id_(
          FrameTreeNode::kFrameTreeNodeInvalidID) {
}

WebContentsImpl::WebContentsTreeNode::~WebContentsTreeNode() {
  // Remove child pointer from our parent.
  if (outer_web_contents_) {
    ChildrenSet& child_ptrs_in_parent =
        outer_web_contents_->node_->inner_web_contents_tree_nodes_;
    ChildrenSet::iterator iter = child_ptrs_in_parent.find(this);
    DCHECK(iter != child_ptrs_in_parent.end());
    child_ptrs_in_parent.erase(this);
  }

  // Remove parent pointers from our children.
  // TODO(lazyboy): We should destroy the children WebContentses too. If the
  // children do not manage their own lifetime, then we would leak their
  // WebContentses.
  for (WebContentsTreeNode* child : inner_web_contents_tree_nodes_)
    child->outer_web_contents_ = nullptr;
}

void WebContentsImpl::WebContentsTreeNode::ConnectToOuterWebContents(
    WebContentsImpl* outer_web_contents,
    RenderFrameHostImpl* outer_contents_frame) {
  outer_web_contents_ = outer_web_contents;
  outer_contents_frame_tree_node_id_ =
      outer_contents_frame->frame_tree_node()->frame_tree_node_id();

  if (!outer_web_contents_->node_)
    outer_web_contents_->node_.reset(new WebContentsTreeNode());

  outer_web_contents_->node_->inner_web_contents_tree_nodes_.insert(this);
}

// WebContentsImpl -------------------------------------------------------------

WebContentsImpl::WebContentsImpl(BrowserContext* browser_context)
    : delegate_(NULL),
      controller_(this, browser_context),
      render_view_host_delegate_view_(NULL),
      created_with_opener_(false),
#if defined(OS_WIN)
      accessible_parent_(NULL),
#endif
      frame_tree_(new NavigatorImpl(&controller_, this),
                  this,
                  this,
                  this,
                  this),
      is_loading_(false),
      is_load_to_different_document_(false),
      crashed_status_(base::TERMINATION_STATUS_STILL_RUNNING),
      crashed_error_code_(0),
      waiting_for_response_(false),
      load_state_(net::LOAD_STATE_IDLE, base::string16()),
      upload_size_(0),
      upload_position_(0),
      is_resume_pending_(false),
      displayed_insecure_content_(false),
      has_accessed_initial_document_(false),
      theme_color_(SK_ColorTRANSPARENT),
      last_sent_theme_color_(SK_ColorTRANSPARENT),
      did_first_visually_non_empty_paint_(false),
      capturer_count_(0),
      should_normally_be_visible_(true),
      is_being_destroyed_(false),
      notify_disconnection_(false),
      dialog_manager_(NULL),
      is_showing_before_unload_dialog_(false),
      last_active_time_(base::TimeTicks::Now()),
      closed_by_user_gesture_(false),
      minimum_zoom_percent_(static_cast<int>(kMinimumZoomFactor * 100)),
      maximum_zoom_percent_(static_cast<int>(kMaximumZoomFactor * 100)),
      render_view_message_source_(NULL),
      render_frame_message_source_(NULL),
      fullscreen_widget_routing_id_(MSG_ROUTING_NONE),
      fullscreen_widget_had_focus_at_shutdown_(false),
      is_subframe_(false),
      force_disable_overscroll_content_(false),
      last_dialog_suppressed_(false),
      geolocation_service_context_(new GeolocationServiceContext()),
      accessibility_mode_(
          BrowserAccessibilityStateImpl::GetInstance()->accessibility_mode()),
      audio_stream_monitor_(this),
      virtual_keyboard_requested_(false),
      page_scale_factor_is_one_(true),
      loading_weak_factory_(this) {
  frame_tree_.SetFrameRemoveListener(
      base::Bind(&WebContentsImpl::OnFrameRemoved,
                 base::Unretained(this)));
#if defined(ENABLE_BROWSER_CDMS)
  media_web_contents_observer_.reset(new MediaWebContentsObserver(this));
#endif

  wake_lock_service_context_.reset(new WakeLockServiceContext(this));
}

WebContentsImpl::~WebContentsImpl() {
  is_being_destroyed_ = true;

  rwh_input_event_router_.reset();

  // Delete all RFH pending shutdown, which will lead the corresponding RVH to
  // shutdown and be deleted as well.
  frame_tree_.ForEach(
      base::Bind(&RenderFrameHostManager::ClearRFHsPendingShutdown));

  ClearAllPowerSaveBlockers();

  for (std::set<RenderWidgetHostImpl*>::iterator iter =
           created_widgets_.begin(); iter != created_widgets_.end(); ++iter) {
    (*iter)->DetachDelegate();
  }
  created_widgets_.clear();

  // Clear out any JavaScript state.
  if (dialog_manager_)
    dialog_manager_->ResetDialogState(this);

  if (color_chooser_info_.get())
    color_chooser_info_->chooser->End();

  NotifyDisconnected();

  // Notify any observer that have a reference on this WebContents.
  NotificationService::current()->Notify(
      NOTIFICATION_WEB_CONTENTS_DESTROYED,
      Source<WebContents>(this),
      NotificationService::NoDetails());

  // Destroy all frame tree nodes except for the root; this notifies observers.
  frame_tree_.root()->ResetForNewProcess();
  GetRenderManager()->ResetProxyHosts();

  // Manually call the observer methods for the root frame tree node. It is
  // necessary to manually delete all objects tracking navigations
  // (NavigationHandle, NavigationRequest) for observers to be properly
  // notified of these navigations stopping before the WebContents is
  // destroyed.
  RenderFrameHostManager* root = GetRenderManager();

  if (root->pending_frame_host()) {
    root->pending_frame_host()->SetRenderFrameCreated(false);
    root->pending_frame_host()->SetNavigationHandle(
        scoped_ptr<NavigationHandleImpl>());
  }
  root->current_frame_host()->SetRenderFrameCreated(false);
  root->current_frame_host()->SetNavigationHandle(
      scoped_ptr<NavigationHandleImpl>());

  // PlzNavigate: clear up state specific to browser-side navigation.
  if (base::CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kEnableBrowserSideNavigation)) {
    frame_tree_.root()->ResetNavigationRequest(false);
    if (root->speculative_frame_host()) {
      root->speculative_frame_host()->SetRenderFrameCreated(false);
      root->speculative_frame_host()->SetNavigationHandle(
          scoped_ptr<NavigationHandleImpl>());
    }
  }

  FOR_EACH_OBSERVER(WebContentsObserver, observers_,
                    FrameDeleted(root->current_frame_host()));

  if (root->pending_render_view_host()) {
    FOR_EACH_OBSERVER(WebContentsObserver,
                      observers_,
                      RenderViewDeleted(root->pending_render_view_host()));
  }

  FOR_EACH_OBSERVER(WebContentsObserver,
                    observers_,
                    RenderViewDeleted(root->current_host()));

  FOR_EACH_OBSERVER(WebContentsObserver,
                    observers_,
                    WebContentsDestroyed());

  FOR_EACH_OBSERVER(WebContentsObserver,
                    observers_,
                    ResetWebContents());

  SetDelegate(NULL);

  STLDeleteContainerPairSecondPointers(destruction_observers_.begin(),
                                       destruction_observers_.end());
}

WebContentsImpl* WebContentsImpl::CreateWithOpener(
    const WebContents::CreateParams& params,
    FrameTreeNode* opener) {
  TRACE_EVENT0("browser", "WebContentsImpl::CreateWithOpener");
  WebContentsImpl* new_contents = new WebContentsImpl(params.browser_context);

  if (!params.opener_suppressed && opener) {
    new_contents->GetFrameTree()->root()->SetOpener(opener);
    new_contents->created_with_opener_ = true;
  }

  // This may be true even when opener is null, such as when opening blocked
  // popups.
  if (params.created_with_opener)
    new_contents->created_with_opener_ = true;

  if (params.guest_delegate) {
    // This makes |new_contents| act as a guest.
    // For more info, see comment above class BrowserPluginGuest.
    BrowserPluginGuest::Create(new_contents, params.guest_delegate);
    // We are instantiating a WebContents for browser plugin. Set its subframe
    // bit to true.
    new_contents->is_subframe_ = true;
  }
  new_contents->Init(params);
  return new_contents;
}

// static
std::vector<WebContentsImpl*> WebContentsImpl::GetAllWebContents() {
  std::vector<WebContentsImpl*> result;
  scoped_ptr<RenderWidgetHostIterator> widgets(
      RenderWidgetHostImpl::GetRenderWidgetHosts());
  while (RenderWidgetHost* rwh = widgets->GetNextHost()) {
    RenderViewHost* rvh = RenderViewHost::From(rwh);
    if (!rvh)
      continue;
    WebContents* web_contents = WebContents::FromRenderViewHost(rvh);
    if (!web_contents)
      continue;
    if (web_contents->GetRenderViewHost() != rvh)
      continue;
    // Because a WebContents can only have one current RVH at a time, there will
    // be no duplicate WebContents here.
    result.push_back(static_cast<WebContentsImpl*>(web_contents));
  }
  return result;
}

// static
WebContentsImpl* WebContentsImpl::FromFrameTreeNode(
    FrameTreeNode* frame_tree_node) {
  return static_cast<WebContentsImpl*>(
      WebContents::FromRenderFrameHost(frame_tree_node->current_frame_host()));
}

RenderFrameHostManager* WebContentsImpl::GetRenderManagerForTesting() {
  return GetRenderManager();
}

bool WebContentsImpl::OnMessageReceived(RenderViewHost* render_view_host,
                                        const IPC::Message& message) {
  return OnMessageReceived(render_view_host, NULL, message);
}

bool WebContentsImpl::OnMessageReceived(RenderViewHost* render_view_host,
                                        RenderFrameHost* render_frame_host,
                                        const IPC::Message& message) {
  DCHECK(render_view_host || render_frame_host);
  if (GetWebUI() &&
      static_cast<WebUIImpl*>(GetWebUI())->OnMessageReceived(message)) {
    return true;
  }

  base::ObserverListBase<WebContentsObserver>::Iterator it(&observers_);
  WebContentsObserver* observer;
  if (render_frame_host) {
    while ((observer = it.GetNext()) != NULL)
      if (observer->OnMessageReceived(message, render_frame_host))
        return true;
  } else {
    while ((observer = it.GetNext()) != NULL)
      if (observer->OnMessageReceived(message))
        return true;
  }

  // Message handlers should be aware of which
  // RenderViewHost/RenderFrameHost sent the message, which is temporarily
  // stored in render_(view|frame)_message_source_.
  if (render_frame_host)
    render_frame_message_source_ = render_frame_host;
  else
    render_view_message_source_ = render_view_host;

  bool handled = true;
  IPC_BEGIN_MESSAGE_MAP(WebContentsImpl, message)
    IPC_MESSAGE_HANDLER(FrameHostMsg_DomOperationResponse,
                        OnDomOperationResponse)
    IPC_MESSAGE_HANDLER(FrameHostMsg_DidChangeThemeColor,
                        OnThemeColorChanged)
    IPC_MESSAGE_HANDLER(FrameHostMsg_DidFinishDocumentLoad,
                        OnDocumentLoadedInFrame)
    IPC_MESSAGE_HANDLER(FrameHostMsg_DidFinishLoad, OnDidFinishLoad)
    IPC_MESSAGE_HANDLER(FrameHostMsg_OpenColorChooser, OnOpenColorChooser)
    IPC_MESSAGE_HANDLER(FrameHostMsg_EndColorChooser, OnEndColorChooser)
    IPC_MESSAGE_HANDLER(FrameHostMsg_SetSelectedColorInColorChooser,
                        OnSetSelectedColorInColorChooser)
    IPC_MESSAGE_HANDLER(FrameHostMsg_MediaPlayingNotification,
                        OnMediaPlayingNotification)
    IPC_MESSAGE_HANDLER(FrameHostMsg_MediaPausedNotification,
                        OnMediaPausedNotification)
    IPC_MESSAGE_HANDLER(ViewHostMsg_DidFirstVisuallyNonEmptyPaint,
                        OnFirstVisuallyNonEmptyPaint)
    IPC_MESSAGE_HANDLER(FrameHostMsg_DidLoadResourceFromMemoryCache,
                        OnDidLoadResourceFromMemoryCache)
    IPC_MESSAGE_HANDLER(FrameHostMsg_DidDisplayInsecureContent,
                        OnDidDisplayInsecureContent)
    IPC_MESSAGE_HANDLER(FrameHostMsg_DidRunInsecureContent,
                        OnDidRunInsecureContent)
    IPC_MESSAGE_HANDLER(ViewHostMsg_GoToEntryAtOffset, OnGoToEntryAtOffset)
    IPC_MESSAGE_HANDLER(ViewHostMsg_UpdateZoomLimits, OnUpdateZoomLimits)
    IPC_MESSAGE_HANDLER(ViewHostMsg_PageScaleFactorChanged,
                        OnPageScaleFactorChanged)
    IPC_MESSAGE_HANDLER(ViewHostMsg_EnumerateDirectory, OnEnumerateDirectory)
    IPC_MESSAGE_HANDLER(FrameHostMsg_RegisterProtocolHandler,
                        OnRegisterProtocolHandler)
    IPC_MESSAGE_HANDLER(FrameHostMsg_UnregisterProtocolHandler,
                        OnUnregisterProtocolHandler)
    IPC_MESSAGE_HANDLER(FrameHostMsg_UpdatePageImportanceSignals,
                        OnUpdatePageImportanceSignals)
    IPC_MESSAGE_HANDLER(ViewHostMsg_Find_Reply, OnFindReply)
    IPC_MESSAGE_HANDLER(ViewHostMsg_AppCacheAccessed, OnAppCacheAccessed)
    IPC_MESSAGE_HANDLER(ViewHostMsg_WebUISend, OnWebUISend)
#if defined(ENABLE_PLUGINS)
    IPC_MESSAGE_HANDLER(FrameHostMsg_PepperInstanceCreated,
                        OnPepperInstanceCreated)
    IPC_MESSAGE_HANDLER(FrameHostMsg_PepperInstanceDeleted,
                        OnPepperInstanceDeleted)
    IPC_MESSAGE_HANDLER(FrameHostMsg_PepperPluginHung, OnPepperPluginHung)
    IPC_MESSAGE_HANDLER(FrameHostMsg_PluginCrashed, OnPluginCrashed)
    IPC_MESSAGE_HANDLER(ViewHostMsg_RequestPpapiBrokerPermission,
                        OnRequestPpapiBrokerPermission)
    IPC_MESSAGE_HANDLER_GENERIC(BrowserPluginHostMsg_Attach,
                                OnBrowserPluginMessage(render_frame_host,
                                                       message))
#endif
    IPC_MESSAGE_HANDLER(ViewHostMsg_UpdateFaviconURL, OnUpdateFaviconURL)
    IPC_MESSAGE_HANDLER(ViewHostMsg_ShowValidationMessage,
                        OnShowValidationMessage)
    IPC_MESSAGE_HANDLER(ViewHostMsg_HideValidationMessage,
                        OnHideValidationMessage)
    IPC_MESSAGE_HANDLER(ViewHostMsg_MoveValidationMessage,
                        OnMoveValidationMessage)
#if defined(OS_ANDROID) && !defined(USE_AURA)
    IPC_MESSAGE_HANDLER(ViewHostMsg_FindMatchRects_Reply,
                        OnFindMatchRectsReply)
    IPC_MESSAGE_HANDLER(ViewHostMsg_OpenDateTimeDialog,
                        OnOpenDateTimeDialog)
#endif
    IPC_MESSAGE_UNHANDLED(handled = false)
  IPC_END_MESSAGE_MAP()
  render_view_message_source_ = NULL;
  render_frame_message_source_ = NULL;

  return handled;
}

bool WebContentsImpl::HasValidFrameSource() {
  if (!render_frame_message_source_) {
    DCHECK(render_view_message_source_);
    bad_message::ReceivedBadMessage(GetRenderProcessHost(),
                                    bad_message::WC_INVALID_FRAME_SOURCE);
    return false;
  }

  return true;
}

void WebContentsImpl::RunFileChooser(
    RenderViewHost* render_view_host,
    const FileChooserParams& params) {
  if (delegate_)
    delegate_->RunFileChooser(this, params);
}

NavigationControllerImpl& WebContentsImpl::GetController() {
  return controller_;
}

const NavigationControllerImpl& WebContentsImpl::GetController() const {
  return controller_;
}

BrowserContext* WebContentsImpl::GetBrowserContext() const {
  return controller_.GetBrowserContext();
}

const GURL& WebContentsImpl::GetURL() const {
  // We may not have a navigation entry yet.
  NavigationEntry* entry = controller_.GetVisibleEntry();
  return entry ? entry->GetVirtualURL() : GURL::EmptyGURL();
}

const GURL& WebContentsImpl::GetVisibleURL() const {
  // We may not have a navigation entry yet.
  NavigationEntry* entry = controller_.GetVisibleEntry();
  return entry ? entry->GetVirtualURL() : GURL::EmptyGURL();
}

const GURL& WebContentsImpl::GetLastCommittedURL() const {
  // We may not have a navigation entry yet.
  NavigationEntry* entry = controller_.GetLastCommittedEntry();
  return entry ? entry->GetVirtualURL() : GURL::EmptyGURL();
}

ScreenOrientationDispatcherHost* WebContentsImpl::GetScreenOrientationDispatcherHost() {
  return screen_orientation_dispatcher_host_.get();
}

WebContentsDelegate* WebContentsImpl::GetDelegate() {
  return delegate_;
}

void WebContentsImpl::SetDelegate(WebContentsDelegate* delegate) {
  // TODO(cbentzel): remove this debugging code?
  if (delegate == delegate_)
    return;
  if (delegate_)
    delegate_->Detach(this);
  delegate_ = delegate;
  if (delegate_) {
    delegate_->Attach(this);
    // Ensure the visible RVH reflects the new delegate's preferences.
    if (view_)
      view_->SetOverscrollControllerEnabled(CanOverscrollContent());
  }
}

RenderProcessHost* WebContentsImpl::GetRenderProcessHost() const {
  RenderViewHostImpl* host = GetRenderManager()->current_host();
  return host ? host->GetProcess() : NULL;
}

RenderFrameHostImpl* WebContentsImpl::GetMainFrame() {
  return frame_tree_.root()->current_frame_host();
}

RenderFrameHostImpl* WebContentsImpl::GetFocusedFrame() {
  FrameTreeNode* focused_node = frame_tree_.GetFocusedFrame();
  if (!focused_node)
    return nullptr;
  return focused_node->current_frame_host();
}

void WebContentsImpl::ForEachFrame(
    const base::Callback<void(RenderFrameHost*)>& on_frame) {
  frame_tree_.ForEach(base::Bind(&ForEachFrameInternal, on_frame));
}

void WebContentsImpl::SendToAllFrames(IPC::Message* message) {
  ForEachFrame(base::Bind(&SendToAllFramesInternal, message));
  delete message;
}

RenderViewHostImpl* WebContentsImpl::GetRenderViewHost() const {
  return GetRenderManager()->current_host();
}

int WebContentsImpl::GetRoutingID() const {
  if (!GetRenderViewHost())
    return MSG_ROUTING_NONE;

  return GetRenderViewHost()->GetRoutingID();
}

void WebContentsImpl::CancelActiveAndPendingDialogs() {
  if (dialog_manager_)
    dialog_manager_->CancelActiveAndPendingDialogs(this);
  if (browser_plugin_embedder_)
    browser_plugin_embedder_->CancelGuestDialogs();
}

int WebContentsImpl::GetFullscreenWidgetRoutingID() const {
  return fullscreen_widget_routing_id_;
}

void WebContentsImpl::ClosePage() {
  GetRenderViewHost()->ClosePage();
}

RenderWidgetHostView* WebContentsImpl::GetRenderWidgetHostView() const {
  return GetRenderManager()->GetRenderWidgetHostView();
}

RenderWidgetHostView* WebContentsImpl::GetFullscreenRenderWidgetHostView()
    const {
  RenderWidgetHost* const widget_host =
      RenderWidgetHostImpl::FromID(GetRenderProcessHost()->GetID(),
                                   GetFullscreenWidgetRoutingID());
  return widget_host ? widget_host->GetView() : NULL;
}

WebContentsView* WebContentsImpl::GetView() const {
  return view_.get();
}

SkColor WebContentsImpl::GetThemeColor() const {
  return theme_color_;
}

void WebContentsImpl::SetAccessibilityMode(AccessibilityMode mode) {
  if (mode == accessibility_mode_)
    return;

  accessibility_mode_ = mode;
  frame_tree_.ForEach(
      base::Bind(&ForEachFrameInternal,
                 base::Bind(&SetAccessibilityModeOnFrame, mode)));
  frame_tree_.ForEach(
      base::Bind(&ForEachPendingFrameInternal,
                 base::Bind(&SetAccessibilityModeOnFrame, mode)));
}

void WebContentsImpl::AddAccessibilityMode(AccessibilityMode mode) {
  SetAccessibilityMode(AddAccessibilityModeTo(accessibility_mode_, mode));
}

void WebContentsImpl::RemoveAccessibilityMode(AccessibilityMode mode) {
  SetAccessibilityMode(RemoveAccessibilityModeFrom(accessibility_mode_, mode));
}

void WebContentsImpl::RequestAXTreeSnapshot(AXTreeSnapshotCallback callback) {
  // TODO(dmazzoni): http://crbug.com/475608 This only returns the
  // accessibility tree from the main frame and everything in the
  // same site instance.
  GetMainFrame()->RequestAXTreeSnapshot(callback);
}

WebUI* WebContentsImpl::CreateSubframeWebUI(const GURL& url,
                                            const std::string& frame_name) {
  DCHECK(!frame_name.empty());
  return CreateWebUI(url, frame_name);
}

WebUI* WebContentsImpl::GetWebUI() const {
  return GetRenderManager()->web_ui() ? GetRenderManager()->web_ui()
      : GetRenderManager()->pending_web_ui();
}

WebUI* WebContentsImpl::GetCommittedWebUI() const {
  return GetRenderManager()->web_ui();
}

void WebContentsImpl::SetUserAgentOverride(const std::string& override) {
  if (GetUserAgentOverride() == override)
    return;

  renderer_preferences_.user_agent_override = override;

  // Send the new override string to the renderer.
  RenderViewHost* host = GetRenderViewHost();
  if (host)
    host->SyncRendererPrefs();

  // Reload the page if a load is currently in progress to avoid having
  // different parts of the page loaded using different user agents.
  NavigationEntry* entry = controller_.GetVisibleEntry();
  if (is_loading_ && entry != NULL && entry->GetIsOverridingUserAgent())
    controller_.ReloadIgnoringCache(true);

  FOR_EACH_OBSERVER(WebContentsObserver, observers_,
                    UserAgentOverrideSet(override));
}

const std::string& WebContentsImpl::GetUserAgentOverride() const {
  return renderer_preferences_.user_agent_override;
}

void WebContentsImpl::EnableTreeOnlyAccessibilityMode() {
  if (GetAccessibilityMode() == AccessibilityModeTreeOnly)
    ForEachFrame(base::Bind(&ResetAccessibility));
  else
    AddAccessibilityMode(AccessibilityModeTreeOnly);
}

bool WebContentsImpl::IsTreeOnlyAccessibilityModeForTesting() const {
  return accessibility_mode_ == AccessibilityModeTreeOnly;
}

bool WebContentsImpl::IsFullAccessibilityModeForTesting() const {
  return accessibility_mode_ == AccessibilityModeComplete;
}

#if defined(OS_WIN)
void WebContentsImpl::SetParentNativeViewAccessible(
gfx::NativeViewAccessible accessible_parent) {
  accessible_parent_ = accessible_parent;
  RenderFrameHostImpl* rfh = GetMainFrame();
  if (rfh)
    rfh->SetParentNativeViewAccessible(accessible_parent);
}
#endif

const PageImportanceSignals& WebContentsImpl::GetPageImportanceSignals() const {
  return page_importance_signals_;
}

const base::string16& WebContentsImpl::GetTitle() const {
  // Transient entries take precedence. They are used for interstitial pages
  // that are shown on top of existing pages.
  NavigationEntry* entry = controller_.GetTransientEntry();
  std::string accept_languages =
      GetContentClient()->browser()->GetAcceptLangs(
          GetBrowserContext());
  if (entry) {
    return entry->GetTitleForDisplay(accept_languages);
  }
  WebUI* our_web_ui = GetRenderManager()->pending_web_ui() ?
      GetRenderManager()->pending_web_ui() : GetRenderManager()->web_ui();
  if (our_web_ui) {
    // Don't override the title in view source mode.
    entry = controller_.GetVisibleEntry();
    if (!(entry && entry->IsViewSourceMode())) {
      // Give the Web UI the chance to override our title.
      const base::string16& title = our_web_ui->GetOverriddenTitle();
      if (!title.empty())
        return title;
    }
  }

  // We use the title for the last committed entry rather than a pending
  // navigation entry. For example, when the user types in a URL, we want to
  // keep the old page's title until the new load has committed and we get a new
  // title.
  entry = controller_.GetLastCommittedEntry();

  // We make an exception for initial navigations. We only want to use the title
  // from the visible entry if:
  // 1. The pending entry has been explicitly assigned a title to display.
  // 2. The user is doing a history navigation in a new tab (e.g., Ctrl+Back),
  //    which case there is a pending entry index other than -1.
  //
  // Otherwise, we want to stick with the last committed entry's title during
  // new navigations, which have pending entries at index -1 with no title.
  if (controller_.IsInitialNavigation() &&
      ((controller_.GetVisibleEntry() &&
        !controller_.GetVisibleEntry()->GetTitle().empty()) ||
       controller_.GetPendingEntryIndex() != -1)) {
    entry = controller_.GetVisibleEntry();
  }

  if (entry) {
    return entry->GetTitleForDisplay(accept_languages);
  }

  // |page_title_when_no_navigation_entry_| is finally used
  // if no title cannot be retrieved.
  return page_title_when_no_navigation_entry_;
}

int32 WebContentsImpl::GetMaxPageID() {
  return GetMaxPageIDForSiteInstance(GetSiteInstance());
}

int32 WebContentsImpl::GetMaxPageIDForSiteInstance(
    SiteInstance* site_instance) {
  if (max_page_ids_.find(site_instance->GetId()) == max_page_ids_.end())
    max_page_ids_[site_instance->GetId()] = -1;

  return max_page_ids_[site_instance->GetId()];
}

void WebContentsImpl::UpdateMaxPageID(int32 page_id) {
  UpdateMaxPageIDForSiteInstance(GetSiteInstance(), page_id);
}

void WebContentsImpl::UpdateMaxPageIDForSiteInstance(
    SiteInstance* site_instance, int32 page_id) {
  if (GetMaxPageIDForSiteInstance(site_instance) < page_id)
    max_page_ids_[site_instance->GetId()] = page_id;
}

void WebContentsImpl::CopyMaxPageIDsFrom(WebContents* web_contents) {
  WebContentsImpl* contents = static_cast<WebContentsImpl*>(web_contents);
  max_page_ids_ = contents->max_page_ids_;
}

SiteInstanceImpl* WebContentsImpl::GetSiteInstance() const {
  return GetRenderManager()->current_host()->GetSiteInstance();
}

SiteInstanceImpl* WebContentsImpl::GetPendingSiteInstance() const {
  RenderViewHostImpl* dest_rvh =
      GetRenderManager()->pending_render_view_host() ?
          GetRenderManager()->pending_render_view_host() :
          GetRenderManager()->current_host();
  return dest_rvh->GetSiteInstance();
}

bool WebContentsImpl::IsLoading() const {
  return is_loading_;
}

bool WebContentsImpl::IsLoadingToDifferentDocument() const {
  return is_loading_ && is_load_to_different_document_;
}

bool WebContentsImpl::IsWaitingForResponse() const {
  return waiting_for_response_ && is_load_to_different_document_;
}

const net::LoadStateWithParam& WebContentsImpl::GetLoadState() const {
  return load_state_;
}

const base::string16& WebContentsImpl::GetLoadStateHost() const {
  return load_state_host_;
}

uint64 WebContentsImpl::GetUploadSize() const {
  return upload_size_;
}

uint64 WebContentsImpl::GetUploadPosition() const {
  return upload_position_;
}

const std::string& WebContentsImpl::GetEncoding() const {
  return canonical_encoding_;
}

bool WebContentsImpl::DisplayedInsecureContent() const {
  return displayed_insecure_content_;
}

void WebContentsImpl::IncrementCapturerCount(const gfx::Size& capture_size) {
  DCHECK(!is_being_destroyed_);
  ++capturer_count_;
  DVLOG(1) << "There are now " << capturer_count_
           << " capturing(s) of WebContentsImpl@" << this;

  // Note: This provides a hint to upstream code to size the views optimally
  // for quality (e.g., to avoid scaling).
  if (!capture_size.IsEmpty() && preferred_size_for_capture_.IsEmpty()) {
    preferred_size_for_capture_ = capture_size;
    OnPreferredSizeChanged(preferred_size_);
  }

  // Ensure that all views are un-occluded before capture begins.
  WasUnOccluded();
}

void WebContentsImpl::DecrementCapturerCount() {
  --capturer_count_;
  DVLOG(1) << "There are now " << capturer_count_
           << " capturing(s) of WebContentsImpl@" << this;
  DCHECK_LE(0, capturer_count_);

  if (is_being_destroyed_)
    return;

  if (capturer_count_ == 0) {
    const gfx::Size old_size = preferred_size_for_capture_;
    preferred_size_for_capture_ = gfx::Size();
    OnPreferredSizeChanged(old_size);
  }

  if (IsHidden()) {
    DVLOG(1) << "Executing delayed WasHidden().";
    WasHidden();
  }
}

int WebContentsImpl::GetCapturerCount() const {
  return capturer_count_;
}

bool WebContentsImpl::IsAudioMuted() const {
  return audio_muter_.get() && audio_muter_->is_muting();
}

void WebContentsImpl::SetAudioMuted(bool mute) {
  DVLOG(1) << "SetAudioMuted(mute=" << mute << "), was " << IsAudioMuted()
           << " for WebContentsImpl@" << this;

  if (mute == IsAudioMuted())
    return;

  if (mute) {
    if (!audio_muter_)
      audio_muter_.reset(new WebContentsAudioMuter(this));
    audio_muter_->StartMuting();
  } else {
    DCHECK(audio_muter_);
    audio_muter_->StopMuting();
  }

  // Notification for UI updates in response to the changed muting state.
  NotifyNavigationStateChanged(INVALIDATE_TYPE_TAB);
}

bool WebContentsImpl::IsCrashed() const {
  return (crashed_status_ == base::TERMINATION_STATUS_PROCESS_CRASHED ||
          crashed_status_ == base::TERMINATION_STATUS_ABNORMAL_TERMINATION ||
          crashed_status_ == base::TERMINATION_STATUS_PROCESS_WAS_KILLED ||
#if defined(OS_CHROMEOS)
          crashed_status_ ==
              base::TERMINATION_STATUS_PROCESS_WAS_KILLED_BY_OOM ||
#endif
          crashed_status_ == base::TERMINATION_STATUS_LAUNCH_FAILED
          );
}

void WebContentsImpl::SetIsCrashed(base::TerminationStatus status,
                                   int error_code) {
  if (status == crashed_status_)
    return;

  crashed_status_ = status;
  crashed_error_code_ = error_code;
  NotifyNavigationStateChanged(INVALIDATE_TYPE_TAB);
}

base::TerminationStatus WebContentsImpl::GetCrashedStatus() const {
  return crashed_status_;
}

bool WebContentsImpl::IsBeingDestroyed() const {
  return is_being_destroyed_;
}

void WebContentsImpl::NotifyNavigationStateChanged(
    InvalidateTypes changed_flags) {
  // TODO(erikchen): Remove ScopedTracker below once http://crbug.com/466285
  // is fixed.
  tracked_objects::ScopedTracker tracking_profile(
      FROM_HERE_WITH_EXPLICIT_FUNCTION(
          "466285 WebContentsImpl::NotifyNavigationStateChanged"));
  // Create and release the audio power save blocker depending on whether the
  // tab is actively producing audio or not.
  if ((changed_flags & INVALIDATE_TYPE_TAB) &&
      AudioStreamMonitor::monitoring_available()) {
    if (WasRecentlyAudible()) {
      if (!audio_power_save_blocker_)
        CreateAudioPowerSaveBlocker();
    } else {
      audio_power_save_blocker_.reset();
    }
  }

  if (delegate_)
    delegate_->NavigationStateChanged(this, changed_flags);
}

base::TimeTicks WebContentsImpl::GetLastActiveTime() const {
  return last_active_time_;
}

void WebContentsImpl::SetLastActiveTime(base::TimeTicks last_active_time) {
  last_active_time_ = last_active_time;
}

void WebContentsImpl::WasShown() {
  controller_.SetActive(true);

  for (RenderWidgetHostView* view : GetRenderWidgetHostViewsInTree()) {
    if (view) {
      view->Show();
#if defined(OS_MACOSX)
      view->SetActive(true);
#endif
    }
  }

  last_active_time_ = base::TimeTicks::Now();

  // The resize rect might have changed while this was inactive -- send the new
  // one to make sure it's up to date.
  RenderViewHostImpl* rvh = GetRenderViewHost();
  if (rvh) {
    rvh->GetWidget()->ResizeRectChanged(
        GetRootWindowResizerRect(rvh->GetWidget()));
  }

  // Restore power save blocker if there are active video players running.
  if (!active_video_players_.empty() && !video_power_save_blocker_)
    CreateVideoPowerSaveBlocker();

  FOR_EACH_OBSERVER(WebContentsObserver, observers_, WasShown());

  should_normally_be_visible_ = true;
}

void WebContentsImpl::WasHidden() {
  // If there are entities capturing screenshots or video (e.g., mirroring),
  // don't activate the "disable rendering" optimization.
  if (capturer_count_ == 0) {
    // |GetRenderViewHost()| can be NULL if the user middle clicks a link to
    // open a tab in the background, then closes the tab before selecting it.
    // This is because closing the tab calls WebContentsImpl::Destroy(), which
    // removes the |GetRenderViewHost()|; then when we actually destroy the
    // window, OnWindowPosChanged() notices and calls WasHidden() (which
    // calls us).
    for (RenderWidgetHostView* view : GetRenderWidgetHostViewsInTree()) {
      if (view)
        view->Hide();
    }

    // Release any video power save blockers held as video is not visible.
    video_power_save_blocker_.reset();
  }

  FOR_EACH_OBSERVER(WebContentsObserver, observers_, WasHidden());

  should_normally_be_visible_ = false;
}

void WebContentsImpl::WasOccluded() {
  if (capturer_count_ > 0)
    return;

  for (RenderWidgetHostView* view : GetRenderWidgetHostViewsInTree()) {
    if (view)
      view->WasOccluded();
  }
}

void WebContentsImpl::WasUnOccluded() {
  for (RenderWidgetHostView* view : GetRenderWidgetHostViewsInTree()) {
    if (view)
      view->WasUnOccluded();
  }
}

bool WebContentsImpl::NeedToFireBeforeUnload() {
  // TODO(creis): Should we fire even for interstitial pages?
  return WillNotifyDisconnection() && !ShowingInterstitialPage() &&
         !GetRenderViewHost()->SuddenTerminationAllowed();
}

void WebContentsImpl::DispatchBeforeUnload(bool for_cross_site_transition) {
  GetMainFrame()->DispatchBeforeUnload(for_cross_site_transition);
}

void WebContentsImpl::AttachToOuterWebContentsFrame(
    WebContents* outer_web_contents,
    RenderFrameHost* outer_contents_frame) {
  CHECK(BrowserPluginGuestMode::UseCrossProcessFramesForGuests());
  // Create a link to our outer WebContents.
  node_.reset(new WebContentsTreeNode());
  node_->ConnectToOuterWebContents(
      static_cast<WebContentsImpl*>(outer_web_contents),
      static_cast<RenderFrameHostImpl*>(outer_contents_frame));

  DCHECK(outer_contents_frame);

  // Create a proxy in top-level RenderFrameHostManager, pointing to the
  // SiteInstance of the outer WebContents. The proxy will be used to send
  // postMessage to the inner WebContents.
  GetRenderManager()->CreateOuterDelegateProxy(
      outer_contents_frame->GetSiteInstance(),
      static_cast<RenderFrameHostImpl*>(outer_contents_frame));

  GetRenderManager()->SetRWHViewForInnerContents(
      GetRenderManager()->GetRenderWidgetHostView());
}

void WebContentsImpl::Stop() {
  frame_tree_.ForEach(base::Bind(&FrameTreeNode::StopLoading));
  FOR_EACH_OBSERVER(WebContentsObserver, observers_, NavigationStopped());
}

WebContents* WebContentsImpl::Clone() {
  // We use our current SiteInstance since the cloned entry will use it anyway.
  // We pass our own opener so that the cloned page can access it if it was set
  // before.
  CreateParams create_params(GetBrowserContext(), GetSiteInstance());
  create_params.initial_size = GetContainerBounds().size();
  WebContentsImpl* tc =
      CreateWithOpener(create_params, frame_tree_.root()->opener());
  tc->GetController().CopyStateFrom(controller_);
  FOR_EACH_OBSERVER(WebContentsObserver,
                    observers_,
                    DidCloneToNewWebContents(this, tc));
  return tc;
}

void WebContentsImpl::Observe(int type,
                              const NotificationSource& source,
                              const NotificationDetails& details) {
  switch (type) {
    case NOTIFICATION_RENDER_WIDGET_HOST_DESTROYED: {
      RenderWidgetHost* host = Source<RenderWidgetHost>(source).ptr();
      RenderWidgetHostView* view = host->GetView();
      if (view == GetFullscreenRenderWidgetHostView()) {
        // We cannot just call view_->RestoreFocus() here.  On some platforms,
        // attempting to focus the currently-invisible WebContentsView will be
        // flat-out ignored.  Therefore, this boolean is used to track whether
        // we will request focus after the fullscreen widget has been
        // destroyed.
        fullscreen_widget_had_focus_at_shutdown_ = (view && view->HasFocus());
      } else {
        for (PendingWidgetViews::iterator i = pending_widget_views_.begin();
             i != pending_widget_views_.end(); ++i) {
          if (host->GetView() == i->second) {
            pending_widget_views_.erase(i);
            break;
          }
        }
      }
      break;
    }
    default:
      NOTREACHED();
  }
}

WebContents* WebContentsImpl::GetWebContents() {
  return this;
}

void WebContentsImpl::Init(const WebContents::CreateParams& params) {
  // This is set before initializing the render manager since
  // RenderFrameHostManager::Init calls back into us via its delegate to ask if
  // it should be hidden.
  should_normally_be_visible_ = !params.initially_hidden;

  // The routing ids must either all be set or all be unset.
  DCHECK((params.routing_id == MSG_ROUTING_NONE &&
          params.main_frame_routing_id == MSG_ROUTING_NONE &&
          params.main_frame_widget_routing_id == MSG_ROUTING_NONE) ||
         (params.routing_id != MSG_ROUTING_NONE &&
          params.main_frame_routing_id != MSG_ROUTING_NONE &&
          params.main_frame_widget_routing_id != MSG_ROUTING_NONE));

  scoped_refptr<SiteInstance> site_instance = params.site_instance;
  if (!site_instance)
    site_instance = SiteInstance::Create(params.browser_context);

  // A main RenderFrameHost always has a RenderWidgetHost, since it is always a
  // local root by definition.
  // TODO(avi): Once RenderViewHostImpl has-a RenderWidgetHostImpl, it will no
  // longer be necessary to eagerly grab a routing ID for the view.
  // https://crbug.com/545684
  int32_t view_routing_id = params.routing_id;
  int32_t main_frame_widget_routing_id = params.main_frame_widget_routing_id;
  if (main_frame_widget_routing_id == MSG_ROUTING_NONE) {
    view_routing_id = main_frame_widget_routing_id =
        site_instance->GetProcess()->GetNextRoutingID();
  }

  GetRenderManager()->Init(site_instance.get(), view_routing_id,
                           params.main_frame_routing_id,
                           main_frame_widget_routing_id);
  frame_tree_.root()->SetFrameName(params.main_frame_name);

  WebContentsViewDelegate* delegate =
      GetContentClient()->browser()->GetWebContentsViewDelegate(this);

  if (browser_plugin_guest_ &&
      !BrowserPluginGuestMode::UseCrossProcessFramesForGuests()) {
    scoped_ptr<WebContentsView> platform_view(CreateWebContentsView(
        this, delegate, &render_view_host_delegate_view_));

    WebContentsViewGuest* rv = new WebContentsViewGuest(
        this, browser_plugin_guest_.get(), platform_view.Pass(),
        render_view_host_delegate_view_);
    render_view_host_delegate_view_ = rv;
    view_.reset(rv);
  } else {
    // Regular WebContentsView.
    view_.reset(CreateWebContentsView(
        this, delegate, &render_view_host_delegate_view_));
  }
  CHECK(render_view_host_delegate_view_);
  CHECK(view_.get());

  gfx::Size initial_size = params.initial_size;
  view_->CreateView(initial_size, params.context);

#if defined(ENABLE_PLUGINS)
  plugin_content_origin_whitelist_.reset(
      new PluginContentOriginWhitelist(this));
#endif

  registrar_.Add(this,
                 NOTIFICATION_RENDER_WIDGET_HOST_DESTROYED,
                 NotificationService::AllBrowserContextsAndSources());

  screen_orientation_dispatcher_host_.reset(
      new ScreenOrientationDispatcherHostImpl(this));

  manifest_manager_host_.reset(new ManifestManagerHost(this));

#if defined(OS_ANDROID) && !defined(USE_AURA)
  date_time_chooser_.reset(new DateTimeChooserAndroid());
#endif

  // BrowserPluginGuest::Init needs to be called after this WebContents has
  // a RenderWidgetHostViewGuest. That is, |view_->CreateView| above.
  if (browser_plugin_guest_)
    browser_plugin_guest_->Init();

  for (size_t i = 0; i < g_created_callbacks.Get().size(); i++)
    g_created_callbacks.Get().at(i).Run(this);

  // If the WebContents creation was renderer-initiated, it means that the
  // corresponding RenderView and main RenderFrame have already been created.
  // Ensure observers are notified about this.
  if (params.renderer_initiated_creation) {
    GetRenderViewHost()->GetWidget()->set_renderer_initialized(true);
    RenderViewCreated(GetRenderViewHost());
    GetRenderManager()->current_frame_host()->SetRenderFrameCreated(true);
  }

  // Ensure that observers are notified of the creation of this WebContents's
  // main RenderFrameHost. It must be done here for main frames, since the
  // NotifySwappedFromRenderManager expects view_ to already be created and that
  // happens after RenderFrameHostManager::Init.
  NotifySwappedFromRenderManager(
      nullptr, GetRenderManager()->current_frame_host(), true);
}

void WebContentsImpl::OnWebContentsDestroyed(WebContentsImpl* web_contents) {
  RemoveDestructionObserver(web_contents);

  // Clear a pending contents that has been closed before being shown.
  for (PendingContents::iterator iter = pending_contents_.begin();
       iter != pending_contents_.end();
       ++iter) {
    if (iter->second != web_contents)
      continue;
    pending_contents_.erase(iter);
    return;
  }
  NOTREACHED();
}

void WebContentsImpl::AddDestructionObserver(WebContentsImpl* web_contents) {
  if (!ContainsKey(destruction_observers_, web_contents)) {
    destruction_observers_[web_contents] =
        new DestructionObserver(this, web_contents);
  }
}

void WebContentsImpl::RemoveDestructionObserver(WebContentsImpl* web_contents) {
  DestructionObservers::iterator iter =
      destruction_observers_.find(web_contents);
  if (iter != destruction_observers_.end()) {
    delete destruction_observers_[web_contents];
    destruction_observers_.erase(iter);
  }
}

void WebContentsImpl::AddObserver(WebContentsObserver* observer) {
  observers_.AddObserver(observer);
}

void WebContentsImpl::RemoveObserver(WebContentsObserver* observer) {
  observers_.RemoveObserver(observer);
}

std::set<RenderWidgetHostView*>
WebContentsImpl::GetRenderWidgetHostViewsInTree() {
  std::set<RenderWidgetHostView*> set;
  if (ShowingInterstitialPage()) {
    set.insert(GetRenderWidgetHostView());
  } else {
    ForEachFrame(
        base::Bind(&AddRenderWidgetHostViewToSet, base::Unretained(&set)));
  }
  return set;
}

void WebContentsImpl::Activate() {
  if (delegate_)
    delegate_->ActivateContents(this);
}

void WebContentsImpl::LostCapture(RenderWidgetHostImpl* render_widget_host) {
  if (!RenderViewHostImpl::From(render_widget_host))
    return;

  if (delegate_)
    delegate_->LostCapture();
}

void WebContentsImpl::RenderWidgetDeleted(
    RenderWidgetHostImpl* render_widget_host) {
  if (is_being_destroyed_) {
    // |created_widgets_| might have been destroyed.
    return;
  }

  std::set<RenderWidgetHostImpl*>::iterator iter =
      created_widgets_.find(render_widget_host);
  if (iter != created_widgets_.end())
    created_widgets_.erase(iter);

  if (render_widget_host &&
      render_widget_host->GetRoutingID() == fullscreen_widget_routing_id_) {
    if (delegate_ && delegate_->EmbedsFullscreenWidget())
      delegate_->ExitFullscreenModeForTab(this);
    FOR_EACH_OBSERVER(WebContentsObserver,
                      observers_,
                      DidDestroyFullscreenWidget(
                          fullscreen_widget_routing_id_));
    fullscreen_widget_routing_id_ = MSG_ROUTING_NONE;
    if (fullscreen_widget_had_focus_at_shutdown_)
      view_->RestoreFocus();
  }
}

void WebContentsImpl::RenderWidgetGotFocus(
    RenderWidgetHostImpl* render_widget_host) {
  // Notify the observers if an embedded fullscreen widget was focused.
  if (delegate_ && render_widget_host && delegate_->EmbedsFullscreenWidget() &&
      render_widget_host->GetView() == GetFullscreenRenderWidgetHostView()) {
    NotifyWebContentsFocused();
  }
}

void WebContentsImpl::RenderWidgetWasResized(
    RenderWidgetHostImpl* render_widget_host,
    bool width_changed) {
  RenderFrameHostImpl* rfh = GetMainFrame();
  if (!rfh || render_widget_host != rfh->GetRenderWidgetHost())
    return;

  FOR_EACH_OBSERVER(WebContentsObserver, observers_,
                    MainFrameWasResized(width_changed));
}

void WebContentsImpl::ScreenInfoChanged() {
  if (browser_plugin_embedder_)
    browser_plugin_embedder_->ScreenInfoChanged();
}

bool WebContentsImpl::PreHandleKeyboardEvent(
    const NativeWebKeyboardEvent& event,
    bool* is_keyboard_shortcut) {
  return delegate_ &&
      delegate_->PreHandleKeyboardEvent(this, event, is_keyboard_shortcut);
}

void WebContentsImpl::HandleKeyboardEvent(const NativeWebKeyboardEvent& event) {
  if (browser_plugin_embedder_ &&
      browser_plugin_embedder_->HandleKeyboardEvent(event)) {
    return;
  }
  if (delegate_)
    delegate_->HandleKeyboardEvent(this, event);
}

bool WebContentsImpl::HandleWheelEvent(
    const blink::WebMouseWheelEvent& event) {
#if !defined(OS_MACOSX)
  // On platforms other than Mac, control+mousewheel may change zoom. On Mac,
  // this isn't done for two reasons:
  //   -the OS already has a gesture to do this through pinch-zoom
  //   -if a user starts an inertial scroll, let's go, and presses control
  //      (i.e. control+tab) then the OS's buffered scroll events will come in
  //      with control key set which isn't what the user wants
  if (delegate_ && event.wheelTicksY &&
      (event.modifiers & blink::WebInputEvent::ControlKey) &&
      !event.canScroll) {
    delegate_->ContentsZoomChange(event.wheelTicksY > 0);
    return true;
  }
#endif
  return false;
}

bool WebContentsImpl::PreHandleGestureEvent(
    const blink::WebGestureEvent& event) {
  return delegate_ && delegate_->PreHandleGestureEvent(this, event);
}

RenderWidgetHostInputEventRouter* WebContentsImpl::GetInputEventRouter() {
  // Currently only supported in site per process mode (--site-per-process).
  if (!rwh_input_event_router_.get() && !is_being_destroyed_ &&
      SiteIsolationPolicy::AreCrossProcessFramesPossible())
    rwh_input_event_router_.reset(new RenderWidgetHostInputEventRouter);
  return rwh_input_event_router_.get();
}

void WebContentsImpl::ReplicatePageFocus(bool is_focused) {
  // Focus loss may occur while this WebContents is being destroyed.  Don't
  // send the message in this case, as the main frame's RenderFrameHost and
  // other state has already been cleared.
  if (is_being_destroyed_)
    return;

  frame_tree_.ReplicatePageFocus(is_focused);
}

RenderWidgetHostImpl* WebContentsImpl::GetFocusedRenderWidgetHost(
    RenderWidgetHostImpl* receiving_widget) {
  if (!SiteIsolationPolicy::AreCrossProcessFramesPossible())
    return receiving_widget;

  // Events for widgets other than the main frame (e.g., popup menus) should be
  // forwarded directly to the widget they arrived on.
  if (receiving_widget != GetMainFrame()->GetRenderWidgetHost())
    return receiving_widget;

  FrameTreeNode* focused_frame = frame_tree_.GetFocusedFrame();
  if (!focused_frame)
    return receiving_widget;

  return RenderWidgetHostImpl::From(
      focused_frame->current_frame_host()->GetView()->GetRenderWidgetHost());
}

void WebContentsImpl::EnterFullscreenMode(const GURL& origin) {
  // This method is being called to enter renderer-initiated fullscreen mode.
  // Make sure any existing fullscreen widget is shut down first.
  RenderWidgetHostView* const widget_view = GetFullscreenRenderWidgetHostView();
  if (widget_view)
    RenderWidgetHostImpl::From(widget_view->GetRenderWidgetHost())->Shutdown();

  if (delegate_)
    delegate_->EnterFullscreenModeForTab(this, origin);

  FOR_EACH_OBSERVER(WebContentsObserver, observers_,
                    DidToggleFullscreenModeForTab(IsFullscreenForCurrentTab(
                        GetRenderViewHost()->GetWidget())));
}

void WebContentsImpl::ExitFullscreenMode() {
  // This method is being called to leave renderer-initiated fullscreen mode.
  // Make sure any existing fullscreen widget is shut down first.
  RenderWidgetHostView* const widget_view = GetFullscreenRenderWidgetHostView();
  if (widget_view)
    RenderWidgetHostImpl::From(widget_view->GetRenderWidgetHost())->Shutdown();

#if defined(OS_ANDROID)
  ContentVideoView* video_view = ContentVideoView::GetInstance();
  if (video_view != NULL)
    video_view->OnExitFullscreen();
#endif

  if (delegate_)
    delegate_->ExitFullscreenModeForTab(this);

  // Ensure web contents exit fullscreen state by sending a resize message,
  // which includes the fullscreen state. This is required for the situation
  // of the browser moving the view into a fullscreen state "browser fullscreen"
  // and then the contents entering "tab fullscreen". Exiting the contents
  // "tab fullscreen" then won't have the side effect of the view resizing,
  // hence the explicit call here is required.
  if (RenderWidgetHostView* rwh_view = GetRenderWidgetHostView()) {
    if (RenderWidgetHost* render_widget_host = rwh_view->GetRenderWidgetHost())
      render_widget_host->WasResized();
  }

  FOR_EACH_OBSERVER(WebContentsObserver, observers_,
                    DidToggleFullscreenModeForTab(IsFullscreenForCurrentTab(
                        GetRenderViewHost()->GetWidget())));
}

bool WebContentsImpl::IsFullscreenForCurrentTab(
    RenderWidgetHostImpl* render_widget_host) const {
  if (!RenderViewHostImpl::From(render_widget_host))
    return false;

  return delegate_ ? delegate_->IsFullscreenForTabOrPending(this) : false;
}

blink::WebDisplayMode WebContentsImpl::GetDisplayMode(
    RenderWidgetHostImpl* render_widget_host) const {
  if (!RenderViewHostImpl::From(render_widget_host))
    return blink::WebDisplayModeBrowser;

  return delegate_ ? delegate_->GetDisplayMode(this)
                   : blink::WebDisplayModeBrowser;
}

void WebContentsImpl::RequestToLockMouse(
    RenderWidgetHostImpl* render_widget_host,
    bool user_gesture,
    bool last_unlocked_by_target) {
  if (render_widget_host != GetRenderViewHost()->GetWidget()) {
    render_widget_host->GotResponseToLockMouseRequest(false);
    return;
  }

  if (delegate_)
    delegate_->RequestToLockMouse(this, user_gesture, last_unlocked_by_target);
  else
    GotResponseToLockMouseRequest(false);
}

void WebContentsImpl::LostMouseLock(RenderWidgetHostImpl* render_widget_host) {
  if (!RenderViewHostImpl::From(render_widget_host))
    return;

  if (delegate_)
    delegate_->LostMouseLock();
}

void WebContentsImpl::CreateNewWindow(
    SiteInstance* source_site_instance,
    int32_t route_id,
    int32_t main_frame_route_id,
    int32_t main_frame_widget_route_id,
    const ViewHostMsg_CreateWindow_Params& params,
    SessionStorageNamespace* session_storage_namespace) {
  // We usually create the new window in the same BrowsingInstance (group of
  // script-related windows), by passing in the current SiteInstance.  However,
  // if the opener is being suppressed (in a non-guest), we create a new
  // SiteInstance in its own BrowsingInstance.
  bool is_guest = BrowserPluginGuest::IsGuest(this);

  if (is_guest && BrowserPluginGuestMode::UseCrossProcessFramesForGuests()) {
    // TODO(lazyboy): CreateNewWindow doesn't work for OOPIF-based <webview>
    // yet.
    NOTREACHED();
  }

  // If the opener is to be suppressed, the new window can be in any process.
  // Since routing ids are process specific, we must not have one passed in
  // as argument here.
  DCHECK(!params.opener_suppressed || route_id == MSG_ROUTING_NONE);

  scoped_refptr<SiteInstance> site_instance =
      params.opener_suppressed && !is_guest
          ? SiteInstance::CreateForURL(GetBrowserContext(), params.target_url)
          : source_site_instance;

  // A message to create a new window can only come from a process for a frame
  // in this WebContents' FrameTree. If any other process sends the request, it
  // is invalid and the process must be terminated.
  int render_process_id = source_site_instance->GetProcess()->GetID();
  bool did_match_process = false;
  frame_tree_.ForEach(
      base::Bind(&FindMatchingProcess, render_process_id, &did_match_process));
  if (!did_match_process) {
    RenderProcessHost* rph = source_site_instance->GetProcess();
    base::ProcessHandle process_handle = rph->GetHandle();
    if (process_handle != base::kNullProcessHandle) {
      RecordAction(
          base::UserMetricsAction("Terminate_ProcessMismatch_CreateNewWindow"));
      rph->Shutdown(RESULT_CODE_KILLED, false);
    }
    return;
  }

  // We must assign the SessionStorageNamespace before calling Init().
  //
  // http://crbug.com/142685
  const std::string& partition_id =
      GetContentClient()->browser()->
          GetStoragePartitionIdForSite(GetBrowserContext(),
                                       site_instance->GetSiteURL());
  StoragePartition* partition = BrowserContext::GetStoragePartition(
      GetBrowserContext(), site_instance.get());
  DOMStorageContextWrapper* dom_storage_context =
      static_cast<DOMStorageContextWrapper*>(partition->GetDOMStorageContext());
  SessionStorageNamespaceImpl* session_storage_namespace_impl =
      static_cast<SessionStorageNamespaceImpl*>(session_storage_namespace);
  CHECK(session_storage_namespace_impl->IsFromContext(dom_storage_context));

  if (delegate_ &&
      !delegate_->ShouldCreateWebContents(
          this, route_id, main_frame_route_id, main_frame_widget_route_id,
          params.window_container_type, params.frame_name, params.target_url,
          partition_id, session_storage_namespace)) {
    if (route_id != MSG_ROUTING_NONE &&
        !RenderViewHost::FromID(render_process_id, route_id)) {
      // If the embedder didn't create a WebContents for this route, we need to
      // delete the RenderView that had already been created.
      Send(new ViewMsg_Close(route_id));
    }
    GetRenderViewHost()->GetProcess()->ResumeRequestsForView(route_id);
    GetRenderViewHost()->GetProcess()->ResumeRequestsForView(
        main_frame_route_id);
    GetRenderViewHost()->GetProcess()->ResumeRequestsForView(
        main_frame_widget_route_id);
    return;
  }

  // Create the new web contents. This will automatically create the new
  // WebContentsView. In the future, we may want to create the view separately.
  CreateParams create_params(GetBrowserContext(), site_instance.get());
  create_params.routing_id = route_id;
  create_params.main_frame_routing_id = main_frame_route_id;
  create_params.main_frame_widget_routing_id = main_frame_widget_route_id;
  create_params.main_frame_name = params.frame_name;
  create_params.opener_render_process_id = render_process_id;
  create_params.opener_render_frame_id = params.opener_render_frame_id;
  create_params.opener_suppressed = params.opener_suppressed;
  if (params.disposition == NEW_BACKGROUND_TAB)
    create_params.initially_hidden = true;
  create_params.renderer_initiated_creation =
      main_frame_route_id != MSG_ROUTING_NONE;

  WebContentsImpl* new_contents = NULL;
  if (!is_guest) {
    create_params.context = view_->GetNativeView();
    create_params.initial_size = GetContainerBounds().size();
    new_contents = static_cast<WebContentsImpl*>(
        WebContents::Create(create_params));
  }  else {
    new_contents = GetBrowserPluginGuest()->CreateNewGuestWindow(create_params);
  }
  new_contents->GetController().SetSessionStorageNamespace(
      partition_id,
      session_storage_namespace);

  // If the new frame has a name, make sure any SiteInstances that can find
  // this named frame have proxies for it.  Must be called after
  // SetSessionStorageNamespace, since this calls CreateRenderView, which uses
  // GetSessionStorageNamespace.
  if (!params.frame_name.empty())
    new_contents->GetRenderManager()->CreateProxiesForNewNamedFrame();

  // Save the window for later if we're not suppressing the opener (since it
  // will be shown immediately).
  if (!params.opener_suppressed) {
    if (!is_guest) {
      WebContentsView* new_view = new_contents->view_.get();

      // TODO(brettw): It seems bogus that we have to call this function on the
      // newly created object and give it one of its own member variables.
      new_view->CreateViewForWidget(
          new_contents->GetRenderViewHost()->GetWidget(), false);
    }
    // Save the created window associated with the route so we can show it
    // later.
    DCHECK_NE(MSG_ROUTING_NONE, route_id);
    pending_contents_[route_id] = new_contents;
    AddDestructionObserver(new_contents);
  }

  if (delegate_) {
    delegate_->WebContentsCreated(
        this, params.opener_render_frame_id, params.frame_name,
        params.target_url, new_contents);
  }

  if (params.opener_suppressed) {
    // When the opener is suppressed, the original renderer cannot access the
    // new window.  As a result, we need to show and navigate the window here.
    bool was_blocked = false;
    if (delegate_) {
      gfx::Rect initial_rect;
      delegate_->AddNewContents(
          this, new_contents, params.disposition, initial_rect,
          params.user_gesture, &was_blocked);
    }
    if (!was_blocked) {
      OpenURLParams open_params(params.target_url,
                                Referrer(),
                                CURRENT_TAB,
                                ui::PAGE_TRANSITION_LINK,
                                true /* is_renderer_initiated */);
      open_params.user_gesture = params.user_gesture;

      if (delegate_ && !is_guest &&
          !delegate_->ShouldResumeRequestsForCreatedWindow()) {
        // We are in asynchronous add new contents path, delay opening url
        new_contents->delayed_open_url_params_.reset(
            new OpenURLParams(open_params));
      } else {
        new_contents->OpenURL(open_params);
      }
    }
  }
}

void WebContentsImpl::CreateNewWidget(int32 render_process_id,
                                      int32 route_id,
                                      blink::WebPopupType popup_type) {
  CreateNewWidget(render_process_id, route_id, false, popup_type);
}

void WebContentsImpl::CreateNewFullscreenWidget(int32 render_process_id,
                                                int32 route_id) {
  CreateNewWidget(render_process_id, route_id, true, blink::WebPopupTypeNone);
}

void WebContentsImpl::CreateNewWidget(int32 render_process_id,
                                      int32 route_id,
                                      bool is_fullscreen,
                                      blink::WebPopupType popup_type) {
  RenderProcessHost* process = GetRenderProcessHost();
  // A message to create a new widget can only come from the active process for
  // this WebContentsImpl instance. If any other process sends the request,
  // it is invalid and the process must be terminated.
  if (process->GetID() != render_process_id) {
    RenderProcessHost* rph = RenderProcessHost::FromID(render_process_id);
    base::ProcessHandle process_handle = rph->GetHandle();
    if (process_handle != base::kNullProcessHandle) {
      RecordAction(
          base::UserMetricsAction("Terminate_ProcessMismatch_CreateNewWidget"));
      rph->Shutdown(RESULT_CODE_KILLED, false);
    }
    return;
  }

  RenderWidgetHostImpl* widget_host =
      new RenderWidgetHostImpl(this, process, route_id, IsHidden());
  created_widgets_.insert(widget_host);

  RenderWidgetHostViewBase* widget_view =
      static_cast<RenderWidgetHostViewBase*>(
          view_->CreateViewForPopupWidget(widget_host));
  if (!widget_view)
    return;
  if (!is_fullscreen) {
    // Popups should not get activated.
    widget_view->SetPopupType(popup_type);
  }
  // Save the created widget associated with the route so we can show it later.
  pending_widget_views_[route_id] = widget_view;

#if defined(OS_MACOSX)
  // A RenderWidgetHostViewMac has lifetime scoped to the view. We'll retain it
  // to allow it to survive the trip without being hosted.
  base::mac::NSObjectRetain(widget_view->GetNativeView());
#endif
}

void WebContentsImpl::ShowCreatedWindow(int route_id,
                                        WindowOpenDisposition disposition,
                                        const gfx::Rect& initial_rect,
                                        bool user_gesture) {
  WebContentsImpl* contents = GetCreatedWindow(route_id);
  if (contents) {
    WebContentsDelegate* delegate = GetDelegate();
    contents->is_resume_pending_ = true;
    if (!delegate || delegate->ShouldResumeRequestsForCreatedWindow())
      contents->ResumeLoadingCreatedWebContents();

    if (delegate) {
      delegate->AddNewContents(
          this, contents, disposition, initial_rect, user_gesture, NULL);
    }
  }
}

void WebContentsImpl::ShowCreatedWidget(int route_id,
                                        const gfx::Rect& initial_rect) {
  ShowCreatedWidget(route_id, false, initial_rect);
}

void WebContentsImpl::ShowCreatedFullscreenWidget(int route_id) {
  ShowCreatedWidget(route_id, true, gfx::Rect());
}

void WebContentsImpl::ShowCreatedWidget(int route_id,
                                        bool is_fullscreen,
                                        const gfx::Rect& initial_rect) {
  RenderWidgetHostViewBase* widget_host_view =
      static_cast<RenderWidgetHostViewBase*>(GetCreatedWidget(route_id));
  if (!widget_host_view)
    return;

  RenderWidgetHostView* view = NULL;
  BrowserPluginGuest* guest = GetBrowserPluginGuest();
  if (guest && guest->embedder_web_contents()) {
    view = guest->embedder_web_contents()->GetRenderWidgetHostView();
  } else {
    view = GetRenderWidgetHostView();
  }

  if (is_fullscreen) {
    DCHECK_EQ(MSG_ROUTING_NONE, fullscreen_widget_routing_id_);
    view_->StoreFocus();
    fullscreen_widget_routing_id_ = route_id;
    if (delegate_ && delegate_->EmbedsFullscreenWidget()) {
      widget_host_view->InitAsChild(GetRenderWidgetHostView()->GetNativeView());
      delegate_->EnterFullscreenModeForTab(this, GURL());
    } else {
      widget_host_view->InitAsFullscreen(view);
    }
    FOR_EACH_OBSERVER(WebContentsObserver,
                      observers_,
                      DidShowFullscreenWidget(route_id));
    if (!widget_host_view->HasFocus())
      widget_host_view->Focus();
  } else {
    widget_host_view->InitAsPopup(view, initial_rect);
  }

  RenderWidgetHostImpl* render_widget_host_impl =
      RenderWidgetHostImpl::From(widget_host_view->GetRenderWidgetHost());
  render_widget_host_impl->Init();
  // Only allow privileged mouse lock for fullscreen render widget, which is
  // used to implement Pepper Flash fullscreen.
  render_widget_host_impl->set_allow_privileged_mouse_lock(is_fullscreen);

#if defined(OS_MACOSX)
  // A RenderWidgetHostViewMac has lifetime scoped to the view. Now that it's
  // properly embedded (or purposefully ignored) we can release the retain we
  // took in CreateNewWidget().
  base::mac::NSObjectRelease(widget_host_view->GetNativeView());
#endif
}

WebContentsImpl* WebContentsImpl::GetCreatedWindow(int route_id) {
  PendingContents::iterator iter = pending_contents_.find(route_id);

  // Certain systems can block the creation of new windows. If we didn't succeed
  // in creating one, just return NULL.
  if (iter == pending_contents_.end()) {
    return NULL;
  }

  WebContentsImpl* new_contents = iter->second;
  pending_contents_.erase(route_id);
  RemoveDestructionObserver(new_contents);

  // Don't initialize the guest WebContents immediately.
  if (BrowserPluginGuest::IsGuest(new_contents))
    return new_contents;

  if (!new_contents->GetRenderProcessHost()->HasConnection() ||
      !new_contents->GetRenderViewHost()->GetWidget()->GetView())
    return NULL;

  return new_contents;
}

RenderWidgetHostView* WebContentsImpl::GetCreatedWidget(int route_id) {
  PendingWidgetViews::iterator iter = pending_widget_views_.find(route_id);
  if (iter == pending_widget_views_.end()) {
    DCHECK(false);
    return NULL;
  }

  RenderWidgetHostView* widget_host_view = iter->second;
  pending_widget_views_.erase(route_id);

  RenderWidgetHost* widget_host = widget_host_view->GetRenderWidgetHost();
  if (!widget_host->GetProcess()->HasConnection()) {
    // The view has gone away or the renderer crashed. Nothing to do.
    return NULL;
  }

  return widget_host_view;
}

void WebContentsImpl::RequestMediaAccessPermission(
    const MediaStreamRequest& request,
    const MediaResponseCallback& callback) {
  if (delegate_) {
    delegate_->RequestMediaAccessPermission(this, request, callback);
  } else {
    callback.Run(MediaStreamDevices(),
                 MEDIA_DEVICE_FAILED_DUE_TO_SHUTDOWN,
                 scoped_ptr<MediaStreamUI>());
  }
}

bool WebContentsImpl::CheckMediaAccessPermission(const GURL& security_origin,
                                                 MediaStreamType type) {
  DCHECK(type == MEDIA_DEVICE_AUDIO_CAPTURE ||
         type == MEDIA_DEVICE_VIDEO_CAPTURE);
  return delegate_ &&
         delegate_->CheckMediaAccessPermission(this, security_origin, type);
}

SessionStorageNamespace* WebContentsImpl::GetSessionStorageNamespace(
    SiteInstance* instance) {
  return controller_.GetSessionStorageNamespace(instance);
}

SessionStorageNamespaceMap WebContentsImpl::GetSessionStorageNamespaceMap() {
  return controller_.GetSessionStorageNamespaceMap();
}

FrameTree* WebContentsImpl::GetFrameTree() {
  return &frame_tree_;
}

void WebContentsImpl::SetIsVirtualKeyboardRequested(bool requested) {
  virtual_keyboard_requested_ = requested;
}

bool WebContentsImpl::IsVirtualKeyboardRequested() {
  return virtual_keyboard_requested_;
}

AccessibilityMode WebContentsImpl::GetAccessibilityMode() const {
  return accessibility_mode_;
}

void WebContentsImpl::AccessibilityEventReceived(
    const std::vector<AXEventNotificationDetails>& details) {
  FOR_EACH_OBSERVER(
      WebContentsObserver, observers_, AccessibilityEventReceived(details));
}

RenderFrameHost* WebContentsImpl::GetGuestByInstanceID(
    RenderFrameHost* render_frame_host,
    int browser_plugin_instance_id) {
  BrowserPluginGuestManager* guest_manager =
      GetBrowserContext()->GetGuestManager();
  if (!guest_manager)
    return nullptr;

  WebContents* guest = guest_manager->GetGuestByInstanceID(
      render_frame_host->GetProcess()->GetID(), browser_plugin_instance_id);
  if (!guest)
    return nullptr;

  return guest->GetMainFrame();
}

GeolocationServiceContext* WebContentsImpl::GetGeolocationServiceContext() {
  return geolocation_service_context_.get();
}

WakeLockServiceContext* WebContentsImpl::GetWakeLockServiceContext() {
  return wake_lock_service_context_.get();
}

void WebContentsImpl::OnShowValidationMessage(
    const gfx::Rect& anchor_in_root_view,
    const base::string16& main_text,
    const base::string16& sub_text) {
  if (delegate_)
    delegate_->ShowValidationMessage(
        this, anchor_in_root_view, main_text, sub_text);
}

void WebContentsImpl::OnHideValidationMessage() {
  if (delegate_)
    delegate_->HideValidationMessage(this);
}

void WebContentsImpl::OnMoveValidationMessage(
    const gfx::Rect& anchor_in_root_view) {
  if (delegate_)
    delegate_->MoveValidationMessage(this, anchor_in_root_view);
}

void WebContentsImpl::DidSendScreenRects(RenderWidgetHostImpl* rwh) {
  if (browser_plugin_embedder_)
    browser_plugin_embedder_->DidSendScreenRects();
}

BrowserAccessibilityManager*
    WebContentsImpl::GetRootBrowserAccessibilityManager() {
  RenderFrameHostImpl* rfh = GetMainFrame();
  return rfh ? rfh->browser_accessibility_manager() : nullptr;
}

BrowserAccessibilityManager*
    WebContentsImpl::GetOrCreateRootBrowserAccessibilityManager() {
  RenderFrameHostImpl* rfh = GetMainFrame();
  return rfh ? rfh->GetOrCreateBrowserAccessibilityManager() : nullptr;
}

void WebContentsImpl::MoveRangeSelectionExtent(const gfx::Point& extent) {
  RenderFrameHost* focused_frame = GetFocusedFrame();
  if (!focused_frame)
    return;

  focused_frame->Send(new InputMsg_MoveRangeSelectionExtent(
      focused_frame->GetRoutingID(), extent));
}

void WebContentsImpl::SelectRange(const gfx::Point& base,
                                  const gfx::Point& extent) {
  RenderFrameHost* focused_frame = GetFocusedFrame();
  if (!focused_frame)
    return;

  focused_frame->Send(
      new InputMsg_SelectRange(focused_frame->GetRoutingID(), base, extent));
}

void WebContentsImpl::AdjustSelectionByCharacterOffset(int start_adjust,
                                                       int end_adjust) {
  RenderFrameHost* focused_frame = GetFocusedFrame();
  if (!focused_frame)
    return;

  focused_frame->Send(new InputMsg_AdjustSelectionByCharacterOffset(
      focused_frame->GetRoutingID(), start_adjust, end_adjust));
}

void WebContentsImpl::UpdatePreferredSize(const gfx::Size& pref_size) {
  const gfx::Size old_size = GetPreferredSize();
  preferred_size_ = pref_size;
  OnPreferredSizeChanged(old_size);
}

void WebContentsImpl::ResizeDueToAutoResize(
    RenderWidgetHostImpl* render_widget_host,
    const gfx::Size& new_size) {
  if (render_widget_host != GetRenderViewHost()->GetWidget())
    return;

  if (delegate_)
    delegate_->ResizeDueToAutoResize(this, new_size);
}

WebContents* WebContentsImpl::OpenURL(const OpenURLParams& params) {
  if (!delegate_)
    return NULL;

  WebContents* new_contents = delegate_->OpenURLFromTab(this, params);
  return new_contents;
}

bool WebContentsImpl::Send(IPC::Message* message) {
  if (!GetRenderViewHost()) {
    delete message;
    return false;
  }

  return GetRenderViewHost()->Send(message);
}

void WebContentsImpl::RenderFrameForInterstitialPageCreated(
    RenderFrameHost* render_frame_host) {
  FOR_EACH_OBSERVER(WebContentsObserver, observers_,
                    RenderFrameForInterstitialPageCreated(render_frame_host));
}

void WebContentsImpl::AttachInterstitialPage(
    InterstitialPageImpl* interstitial_page) {
  DCHECK(interstitial_page);
  GetRenderManager()->set_interstitial_page(interstitial_page);

  // Cancel any visible dialogs so that they don't interfere with the
  // interstitial.
  CancelActiveAndPendingDialogs();

  FOR_EACH_OBSERVER(WebContentsObserver, observers_,
                    DidAttachInterstitialPage());
}

void WebContentsImpl::DetachInterstitialPage() {
  if (ShowingInterstitialPage())
    GetRenderManager()->remove_interstitial_page();
  FOR_EACH_OBSERVER(WebContentsObserver, observers_,
                    DidDetachInterstitialPage());
}

void WebContentsImpl::SetHistoryOffsetAndLength(int history_offset,
                                                int history_length) {
  SetHistoryOffsetAndLengthForView(
      GetRenderViewHost(), history_offset, history_length);
}

void WebContentsImpl::SetHistoryOffsetAndLengthForView(
    RenderViewHost* render_view_host,
    int history_offset,
    int history_length) {
  render_view_host->Send(new ViewMsg_SetHistoryOffsetAndLength(
      render_view_host->GetRoutingID(), history_offset, history_length));
}

void WebContentsImpl::ReloadFocusedFrame(bool ignore_cache) {
  RenderFrameHost* focused_frame = GetFocusedFrame();
  if (!focused_frame)
    return;

  focused_frame->Send(new FrameMsg_Reload(
      focused_frame->GetRoutingID(), ignore_cache));
}

void WebContentsImpl::Undo() {
  RenderFrameHost* focused_frame = GetFocusedFrame();
  if (!focused_frame)
    return;

  focused_frame->Send(new InputMsg_Undo(focused_frame->GetRoutingID()));
  RecordAction(base::UserMetricsAction("Undo"));
}

void WebContentsImpl::Redo() {
  RenderFrameHost* focused_frame = GetFocusedFrame();
  if (!focused_frame)
    return;
  focused_frame->Send(new InputMsg_Redo(focused_frame->GetRoutingID()));
  RecordAction(base::UserMetricsAction("Redo"));
}

void WebContentsImpl::Cut() {
  RenderFrameHost* focused_frame = GetFocusedFrame();
  if (!focused_frame)
    return;

  focused_frame->Send(new InputMsg_Cut(focused_frame->GetRoutingID()));
  RecordAction(base::UserMetricsAction("Cut"));
}

void WebContentsImpl::Copy() {
  RenderFrameHost* focused_frame = GetFocusedFrame();
  if (!focused_frame)
    return;

  focused_frame->Send(new InputMsg_Copy(focused_frame->GetRoutingID()));
  RecordAction(base::UserMetricsAction("Copy"));
}

void WebContentsImpl::CopyToFindPboard() {
#if defined(OS_MACOSX)
  RenderFrameHost* focused_frame = GetFocusedFrame();
  if (!focused_frame)
    return;

  // Windows/Linux don't have the concept of a find pasteboard.
  focused_frame->Send(
      new InputMsg_CopyToFindPboard(focused_frame->GetRoutingID()));
  RecordAction(base::UserMetricsAction("CopyToFindPboard"));
#endif
}

void WebContentsImpl::Paste() {
  RenderFrameHost* focused_frame = GetFocusedFrame();
  if (!focused_frame)
    return;

  focused_frame->Send(new InputMsg_Paste(focused_frame->GetRoutingID()));
  RecordAction(base::UserMetricsAction("Paste"));
}

void WebContentsImpl::PasteAndMatchStyle() {
  RenderFrameHost* focused_frame = GetFocusedFrame();
  if (!focused_frame)
    return;

  focused_frame->Send(new InputMsg_PasteAndMatchStyle(
      focused_frame->GetRoutingID()));
  RecordAction(base::UserMetricsAction("PasteAndMatchStyle"));
}

void WebContentsImpl::Delete() {
  RenderFrameHost* focused_frame = GetFocusedFrame();
  if (!focused_frame)
    return;

  focused_frame->Send(new InputMsg_Delete(focused_frame->GetRoutingID()));
  RecordAction(base::UserMetricsAction("DeleteSelection"));
}

void WebContentsImpl::SelectAll() {
  RenderFrameHost* focused_frame = GetFocusedFrame();
  if (!focused_frame)
    return;

  focused_frame->Send(new InputMsg_SelectAll(focused_frame->GetRoutingID()));
  RecordAction(base::UserMetricsAction("SelectAll"));
}

void WebContentsImpl::Unselect() {
  RenderFrameHost* focused_frame = GetFocusedFrame();
  if (!focused_frame)
    return;

  focused_frame->Send(new InputMsg_Unselect(focused_frame->GetRoutingID()));
  RecordAction(base::UserMetricsAction("Unselect"));
}

void WebContentsImpl::Replace(const base::string16& word) {
  RenderFrameHost* focused_frame = GetFocusedFrame();
  if (!focused_frame)
    return;

  focused_frame->Send(new InputMsg_Replace(
      focused_frame->GetRoutingID(), word));
}

void WebContentsImpl::ReplaceMisspelling(const base::string16& word) {
  RenderFrameHost* focused_frame = GetFocusedFrame();
  if (!focused_frame)
    return;

  focused_frame->Send(new InputMsg_ReplaceMisspelling(
      focused_frame->GetRoutingID(), word));
}

void WebContentsImpl::NotifyContextMenuClosed(
    const CustomContextMenuContext& context) {
  RenderFrameHost* focused_frame = GetFocusedFrame();
  if (!focused_frame)
    return;

  focused_frame->Send(new FrameMsg_ContextMenuClosed(
      focused_frame->GetRoutingID(), context));
}

void WebContentsImpl::ExecuteCustomContextMenuCommand(
    int action, const CustomContextMenuContext& context) {
  RenderFrameHost* focused_frame = GetFocusedFrame();
  if (!focused_frame)
    return;

  focused_frame->Send(new FrameMsg_CustomContextMenuAction(
      focused_frame->GetRoutingID(), context, action));
}

gfx::NativeView WebContentsImpl::GetNativeView() {
  return view_->GetNativeView();
}

gfx::NativeView WebContentsImpl::GetContentNativeView() {
  return view_->GetContentNativeView();
}

gfx::NativeWindow WebContentsImpl::GetTopLevelNativeWindow() {
  return view_->GetTopLevelNativeWindow();
}

gfx::Rect WebContentsImpl::GetViewBounds() {
  return view_->GetViewBounds();
}

gfx::Rect WebContentsImpl::GetContainerBounds() {
  gfx::Rect rv;
  view_->GetContainerBounds(&rv);
  return rv;
}

DropData* WebContentsImpl::GetDropData() {
  return view_->GetDropData();
}

void WebContentsImpl::Focus() {
  view_->Focus();
}

void WebContentsImpl::SetInitialFocus() {
  view_->SetInitialFocus();
}

void WebContentsImpl::StoreFocus() {
  view_->StoreFocus();
}

void WebContentsImpl::RestoreFocus() {
  view_->RestoreFocus();
}

void WebContentsImpl::FocusThroughTabTraversal(bool reverse) {
  if (ShowingInterstitialPage()) {
    GetRenderManager()->interstitial_page()->FocusThroughTabTraversal(reverse);
    return;
  }
  RenderWidgetHostView* const fullscreen_view =
      GetFullscreenRenderWidgetHostView();
  if (fullscreen_view) {
    fullscreen_view->Focus();
    return;
  }
  GetRenderViewHost()->SetInitialFocus(reverse);
}

bool WebContentsImpl::ShowingInterstitialPage() const {
  return GetRenderManager()->interstitial_page() != NULL;
}

InterstitialPage* WebContentsImpl::GetInterstitialPage() const {
  return GetRenderManager()->interstitial_page();
}

bool WebContentsImpl::IsSavable() {
  // WebKit creates Document object when MIME type is application/xhtml+xml,
  // so we also support this MIME type.
  return contents_mime_type_ == "text/html" ||
         contents_mime_type_ == "text/xml" ||
         contents_mime_type_ == "application/xhtml+xml" ||
         contents_mime_type_ == "text/plain" ||
         contents_mime_type_ == "text/css" ||
         mime_util::IsSupportedJavascriptMimeType(contents_mime_type_);
}

void WebContentsImpl::OnSavePage() {
  // If we can not save the page, try to download it.
  if (!IsSavable()) {
    RecordDownloadSource(INITIATED_BY_SAVE_PACKAGE_ON_NON_HTML);
    SaveFrame(GetLastCommittedURL(), Referrer());
    return;
  }

  Stop();

  // Create the save package and possibly prompt the user for the name to save
  // the page as. The user prompt is an asynchronous operation that runs on
  // another thread.
  save_package_ = new SavePackage(this);
  save_package_->GetSaveInfo();
}

// Used in automated testing to bypass prompting the user for file names.
// Instead, the names and paths are hard coded rather than running them through
// file name sanitation and extension / mime checking.
bool WebContentsImpl::SavePage(const base::FilePath& main_file,
                               const base::FilePath& dir_path,
                               SavePageType save_type) {
  // Stop the page from navigating.
  Stop();

  save_package_ = new SavePackage(this, save_type, main_file, dir_path);
  return save_package_->Init(SavePackageDownloadCreatedCallback());
}

void WebContentsImpl::SaveFrame(const GURL& url,
                                const Referrer& referrer) {
  SaveFrameWithHeaders(url, referrer, std::string());
}

void WebContentsImpl::SaveFrameWithHeaders(const GURL& url,
                                           const Referrer& referrer,
                                           const std::string& headers) {
  if (!GetLastCommittedURL().is_valid())
    return;
  if (delegate_ && delegate_->SaveFrame(url, referrer))
    return;

  // TODO(nasko): This check for main frame is incorrect and should be fixed
  // by explicitly passing in which frame this method should target.
  bool is_main_frame = (url == GetLastCommittedURL());

  DownloadManager* dlm =
      BrowserContext::GetDownloadManager(GetBrowserContext());
  if (!dlm)
    return;
  int64 post_id = -1;
  if (is_main_frame) {
    const NavigationEntry* entry = controller_.GetLastCommittedEntry();
    if (entry)
      post_id = entry->GetPostID();
  }
  scoped_ptr<DownloadUrlParameters> params(
      DownloadUrlParameters::FromWebContents(this, url));
  params->set_referrer(referrer);
  params->set_post_id(post_id);
  if (post_id >= 0)
    params->set_method("POST");
  params->set_prompt(true);

  if (headers.empty()) {
    params->set_prefer_cache(true);
  } else {
    for (const base::StringPiece& key_value :
         base::SplitStringPiece(
             headers, "\n", base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL)) {
      std::vector<std::string> pair = base::SplitString(
          key_value, ":", base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL);
      DCHECK_EQ(2ul, pair.size());
      params->add_request_header(pair[0], pair[1]);
    }
  }
  dlm->DownloadUrl(params.Pass());
}

void WebContentsImpl::GenerateMHTML(
    const base::FilePath& file,
    const base::Callback<void(int64)>& callback) {
  MHTMLGenerationManager::GetInstance()->SaveMHTML(this, file, callback);
}

const std::string& WebContentsImpl::GetContentsMimeType() const {
  return contents_mime_type_;
}

bool WebContentsImpl::WillNotifyDisconnection() const {
  return notify_disconnection_;
}

void WebContentsImpl::SetOverrideEncoding(const std::string& encoding) {
  SetEncoding(encoding);
  Send(new ViewMsg_SetPageEncoding(GetRoutingID(), encoding));
}

void WebContentsImpl::ResetOverrideEncoding() {
  canonical_encoding_.clear();
  Send(new ViewMsg_ResetPageEncodingToDefault(GetRoutingID()));
}

RendererPreferences* WebContentsImpl::GetMutableRendererPrefs() {
  return &renderer_preferences_;
}

void WebContentsImpl::Close() {
  Close(GetRenderViewHost());
}

void WebContentsImpl::DragSourceEndedAt(int client_x, int client_y,
    int screen_x, int screen_y, blink::WebDragOperation operation) {
  if (browser_plugin_embedder_.get())
    browser_plugin_embedder_->DragSourceEndedAt(client_x, client_y,
        screen_x, screen_y, operation);
  if (GetRenderViewHost())
    GetRenderViewHost()->DragSourceEndedAt(client_x, client_y, screen_x,
                                           screen_y, operation);
}

void WebContentsImpl::DidGetResourceResponseStart(
  const ResourceRequestDetails& details) {
  controller_.ssl_manager()->DidStartResourceResponse(details);

  FOR_EACH_OBSERVER(WebContentsObserver, observers_,
                    DidGetResourceResponseStart(details));
}

void WebContentsImpl::DidGetRedirectForResourceRequest(
  RenderFrameHost* render_frame_host,
  const ResourceRedirectDetails& details) {
  controller_.ssl_manager()->DidReceiveResourceRedirect(details);

  FOR_EACH_OBSERVER(
      WebContentsObserver,
      observers_,
      DidGetRedirectForResourceRequest(render_frame_host, details));

  // TODO(avi): Remove. http://crbug.com/170921
  NotificationService::current()->Notify(
      NOTIFICATION_RESOURCE_RECEIVED_REDIRECT,
      Source<WebContents>(this),
      Details<const ResourceRedirectDetails>(&details));

  if (IsResourceTypeFrame(details.resource_type)) {
    NavigationHandleImpl* navigation_handle =
        static_cast<RenderFrameHostImpl*>(render_frame_host)
            ->navigation_handle();
    if (navigation_handle)
      navigation_handle->DidRedirectNavigation(details.new_url);
  }
}

void WebContentsImpl::NotifyWebContentsFocused() {
  FOR_EACH_OBSERVER(WebContentsObserver, observers_, OnWebContentsFocused());
}

void WebContentsImpl::SystemDragEnded() {
  if (GetRenderViewHost())
    GetRenderViewHost()->DragSourceSystemDragEnded();
  if (browser_plugin_embedder_.get())
    browser_plugin_embedder_->SystemDragEnded();
}

void WebContentsImpl::UserGestureDone() {
  OnUserGesture(GetRenderViewHost()->GetWidget());
}

void WebContentsImpl::SetClosedByUserGesture(bool value) {
  closed_by_user_gesture_ = value;
}

bool WebContentsImpl::GetClosedByUserGesture() const {
  return closed_by_user_gesture_;
}

void WebContentsImpl::ViewSource() {
  if (!delegate_)
    return;

  NavigationEntry* entry = GetController().GetLastCommittedEntry();
  if (!entry)
    return;

  delegate_->ViewSourceForTab(this, entry->GetURL());
}

void WebContentsImpl::ViewFrameSource(const GURL& url,
                                      const PageState& page_state) {
  if (!delegate_)
    return;

  delegate_->ViewSourceForFrame(this, url, page_state);
}

int WebContentsImpl::GetMinimumZoomPercent() const {
  return minimum_zoom_percent_;
}

int WebContentsImpl::GetMaximumZoomPercent() const {
  return maximum_zoom_percent_;
}

void WebContentsImpl::SetPageScale(float page_scale_factor) {
  Send(new ViewMsg_SetPageScale(GetRoutingID(), page_scale_factor));
}

gfx::Size WebContentsImpl::GetPreferredSize() const {
  return capturer_count_ == 0 ? preferred_size_ : preferred_size_for_capture_;
}

bool WebContentsImpl::GotResponseToLockMouseRequest(bool allowed) {
  if (GetBrowserPluginGuest())
    return GetBrowserPluginGuest()->LockMouse(allowed);

  return GetRenderViewHost()
             ? GetRenderViewHost()->GetWidget()->GotResponseToLockMouseRequest(
                   allowed)
             : false;
}

bool WebContentsImpl::HasOpener() const {
  return GetOpener() != NULL;
}

WebContentsImpl* WebContentsImpl::GetOpener() const {
  FrameTreeNode* opener_ftn = frame_tree_.root()->opener();
  return opener_ftn ? FromFrameTreeNode(opener_ftn) : nullptr;
}

void WebContentsImpl::DidChooseColorInColorChooser(SkColor color) {
  if (!color_chooser_info_.get())
    return;
  RenderFrameHost* rfh = RenderFrameHost::FromID(
      color_chooser_info_->render_process_id,
      color_chooser_info_->render_frame_id);
  if (!rfh)
    return;

  rfh->Send(new FrameMsg_DidChooseColorResponse(
      rfh->GetRoutingID(), color_chooser_info_->identifier, color));
}

void WebContentsImpl::DidEndColorChooser() {
  if (!color_chooser_info_.get())
    return;
  RenderFrameHost* rfh = RenderFrameHost::FromID(
      color_chooser_info_->render_process_id,
      color_chooser_info_->render_frame_id);
  if (!rfh)
    return;

  rfh->Send(new FrameMsg_DidEndColorChooser(
      rfh->GetRoutingID(), color_chooser_info_->identifier));
  color_chooser_info_.reset();
}

int WebContentsImpl::DownloadImage(
    const GURL& url,
    bool is_favicon,
    uint32_t max_bitmap_size,
    bool bypass_cache,
    const WebContents::ImageDownloadCallback& callback) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  static int next_image_download_id = 0;
  const image_downloader::ImageDownloaderPtr& mojo_image_downloader =
      GetMainFrame()->GetMojoImageDownloader();
  const int download_id = ++next_image_download_id;
  if (!mojo_image_downloader) {
    // If the renderer process is dead (i.e. crash, or memory pressure on
    // Android), the downloader service will be invalid. Pre-Mojo, this would
    // hang the callback indefinetly since the IPC would be dropped. Now,
    // respond with a 400 HTTP error code to indicate that something went wrong.
    BrowserThread::PostTask(
        BrowserThread::UI, FROM_HERE,
        base::Bind(&WebContents::ImageDownloadCallback::Run,
                   base::Owned(new ImageDownloadCallback(callback)),
                   download_id, 400, url, std::vector<SkBitmap>(),
                   std::vector<gfx::Size>()));
    return download_id;
  }

  image_downloader::DownloadRequestPtr req =
      image_downloader::DownloadRequest::New();

  req->url = mojo::String::From(url);
  req->is_favicon = is_favicon;
  req->max_bitmap_size = max_bitmap_size;
  req->bypass_cache = bypass_cache;

  mojo_image_downloader->DownloadImage(
      req.Pass(),
      base::Bind(&DidDownloadImage, callback, download_id, url));
  return download_id;
}

bool WebContentsImpl::IsSubframe() const {
  return is_subframe_;
}

void WebContentsImpl::Find(int request_id,
                           const base::string16& search_text,
                           const blink::WebFindOptions& options) {
  // Cowardly refuse to search for no text.
  if (search_text.empty()) {
    NOTREACHED();
    return;
  }

  // See if a top level browser plugin handles the find request first.
  if (browser_plugin_embedder_ &&
      browser_plugin_embedder_->Find(request_id, search_text, options)) {
    return;
  }
  Send(new ViewMsg_Find(GetRoutingID(), request_id, search_text, options));
}

void WebContentsImpl::StopFinding(StopFindAction action) {
  // See if a top level browser plugin handles the stop finding request first.
  if (browser_plugin_embedder_ &&
      browser_plugin_embedder_->StopFinding(action)) {
    return;
  }
  Send(new ViewMsg_StopFinding(GetRoutingID(), action));
}

void WebContentsImpl::InsertCSS(const std::string& css) {
  GetMainFrame()->Send(new FrameMsg_CSSInsertRequest(
      GetMainFrame()->GetRoutingID(), css));
}

bool WebContentsImpl::WasRecentlyAudible() {
  return audio_stream_monitor_.WasRecentlyAudible();
}

void WebContentsImpl::GetManifest(const GetManifestCallback& callback) {
  manifest_manager_host_->GetManifest(GetMainFrame(), callback);
}

void WebContentsImpl::HasManifest(const HasManifestCallback& callback) {
  manifest_manager_host_->HasManifest(GetMainFrame(), callback);
}

void WebContentsImpl::ExitFullscreen() {
  // Clean up related state and initiate the fullscreen exit.
  GetRenderViewHost()->GetWidget()->RejectMouseLockOrUnlockIfNecessary();
  ExitFullscreenMode();
}

void WebContentsImpl::ResumeLoadingCreatedWebContents() {
  if (delayed_open_url_params_.get()) {
    OpenURL(*delayed_open_url_params_.get());
    delayed_open_url_params_.reset(nullptr);
    return;
  }

  // Resume blocked requests for both the RenderViewHost and RenderFrameHost.
  // TODO(brettw): It seems bogus to reach into here and initialize the host.
  if (is_resume_pending_) {
    is_resume_pending_ = false;
    GetRenderViewHost()->Init();
    GetMainFrame()->Init();
  }
}

bool WebContentsImpl::FocusLocationBarByDefault() {
  NavigationEntry* entry = controller_.GetVisibleEntry();
  if (entry && entry->GetURL() == GURL(url::kAboutBlankURL))
    return true;
  return delegate_ && delegate_->ShouldFocusLocationBarByDefault(this);
}

void WebContentsImpl::SetFocusToLocationBar(bool select_all) {
  if (delegate_)
    delegate_->SetFocusToLocationBar(select_all);
}

void WebContentsImpl::DidStartNavigation(NavigationHandle* navigation_handle) {
  FOR_EACH_OBSERVER(WebContentsObserver, observers_,
                    DidStartNavigation(navigation_handle));
}

void WebContentsImpl::DidRedirectNavigation(
    NavigationHandle* navigation_handle) {
  FOR_EACH_OBSERVER(WebContentsObserver, observers_,
                    DidRedirectNavigation(navigation_handle));
}

void WebContentsImpl::ReadyToCommitNavigation(
    NavigationHandle* navigation_handle) {
  FOR_EACH_OBSERVER(WebContentsObserver, observers_,
                    ReadyToCommitNavigation(navigation_handle));
}

void WebContentsImpl::DidFinishNavigation(NavigationHandle* navigation_handle) {
  FOR_EACH_OBSERVER(WebContentsObserver, observers_,
                    DidFinishNavigation(navigation_handle));
}

void WebContentsImpl::DidStartProvisionalLoad(
    RenderFrameHostImpl* render_frame_host,
    const GURL& validated_url,
    bool is_error_page,
    bool is_iframe_srcdoc) {
  // Notify observers about the start of the provisional load.
  FOR_EACH_OBSERVER(
      WebContentsObserver,
      observers_,
      DidStartProvisionalLoadForFrame(
          render_frame_host, validated_url, is_error_page, is_iframe_srcdoc));

  // Notify accessibility if this is a reload.
  NavigationEntry* entry = controller_.GetVisibleEntry();
  if (entry && ui::PageTransitionCoreTypeIs(
          entry->GetTransitionType(), ui::PAGE_TRANSITION_RELOAD)) {
    FrameTreeNode* ftn = render_frame_host->frame_tree_node();
    BrowserAccessibilityManager* manager =
        ftn->current_frame_host()->browser_accessibility_manager();
    if (manager)
      manager->UserIsReloading();
  }
}

void WebContentsImpl::DidFailProvisionalLoadWithError(
    RenderFrameHostImpl* render_frame_host,
    const FrameHostMsg_DidFailProvisionalLoadWithError_Params& params) {
  GURL validated_url(params.url);
  FOR_EACH_OBSERVER(WebContentsObserver,
                    observers_,
                    DidFailProvisionalLoad(render_frame_host,
                                           validated_url,
                                           params.error_code,
                                           params.error_description,
                                           params.was_ignored_by_handler));

  FrameTreeNode* ftn = render_frame_host->frame_tree_node();
  BrowserAccessibilityManager* manager =
      ftn->current_frame_host()->browser_accessibility_manager();
  if (manager)
    manager->NavigationFailed();
}

void WebContentsImpl::DidFailLoadWithError(
    RenderFrameHostImpl* render_frame_host,
    const GURL& url,
    int error_code,
    const base::string16& error_description,
    bool was_ignored_by_handler) {
  FOR_EACH_OBSERVER(
      WebContentsObserver,
      observers_,
      DidFailLoad(render_frame_host, url, error_code, error_description,
        was_ignored_by_handler));
}

void WebContentsImpl::NotifyChangedNavigationState(
    InvalidateTypes changed_flags) {
  NotifyNavigationStateChanged(changed_flags);
}

void WebContentsImpl::DidStartNavigationToPendingEntry(
      const GURL& url,
      NavigationController::ReloadType reload_type) {
  // Notify observers about navigation.
  FOR_EACH_OBSERVER(
      WebContentsObserver,
      observers_,
      DidStartNavigationToPendingEntry(url, reload_type));
}

void WebContentsImpl::RequestOpenURL(RenderFrameHostImpl* render_frame_host,
                                     const OpenURLParams& params) {
  // OpenURL can blow away the source RFH. Use the process/frame routing ID as a
  // weak pointer of sorts.
  const int32_t process_id = render_frame_host->GetProcess()->GetID();
  const int32_t frame_id = render_frame_host->GetRoutingID();

  WebContents* new_contents = OpenURL(params);

  if (new_contents && RenderFrameHost::FromID(process_id, frame_id)) {
    // Notify observers.
    FOR_EACH_OBSERVER(WebContentsObserver, observers_,
                      DidOpenRequestedURL(new_contents,
                                          render_frame_host,
                                          params.url,
                                          params.referrer,
                                          params.disposition,
                                          params.transition));
  }
}

bool WebContentsImpl::ShouldPreserveAbortedURLs() {
  if (!delegate_)
    return false;
  return delegate_->ShouldPreserveAbortedURLs(this);
}

void WebContentsImpl::DidCommitProvisionalLoad(
    RenderFrameHostImpl* render_frame_host,
    const GURL& url,
    ui::PageTransition transition_type) {
  // Notify observers about the commit of the provisional load.
  FOR_EACH_OBSERVER(WebContentsObserver,
                    observers_,
                    DidCommitProvisionalLoadForFrame(
                        render_frame_host, url, transition_type));

  BrowserAccessibilityManager* manager =
      render_frame_host->browser_accessibility_manager();
  if (manager)
    manager->NavigationSucceeded();
}

void WebContentsImpl::DidNavigateMainFramePreCommit(
    bool navigation_is_within_page) {
  // Ensure fullscreen mode is exited before committing the navigation to a
  // different page.  The next page will not start out assuming it is in
  // fullscreen mode.
  if (navigation_is_within_page) {
    // No page change?  Then, the renderer and browser can remain in fullscreen.
    return;
  }
  if (IsFullscreenForCurrentTab(GetRenderViewHost()->GetWidget()))
    ExitFullscreen();
  DCHECK(!IsFullscreenForCurrentTab(GetRenderViewHost()->GetWidget()));
}

void WebContentsImpl::DidNavigateMainFramePostCommit(
    RenderFrameHostImpl* render_frame_host,
    const LoadCommittedDetails& details,
    const FrameHostMsg_DidCommitProvisionalLoad_Params& params) {
  if (details.is_navigation_to_different_page()) {
    // Clear the status bubble. This is a workaround for a bug where WebKit
    // doesn't let us know that the cursor left an element during a
    // transition (this is also why the mouse cursor remains as a hand after
    // clicking on a link); see bugs 1184641 and 980803. We don't want to
    // clear the bubble when a user navigates to a named anchor in the same
    // page.
    UpdateTargetURL(render_frame_host->GetRenderViewHost(), GURL());

    RenderWidgetHostViewBase* rwhvb =
        static_cast<RenderWidgetHostViewBase*>(GetRenderWidgetHostView());
    if (rwhvb)
      rwhvb->OnDidNavigateMainFrameToNewPage();

    did_first_visually_non_empty_paint_ = false;

    // Reset theme color on navigation to new page.
    theme_color_ = SK_ColorTRANSPARENT;
  }

  if (!details.is_in_page) {
    // Once the main frame is navigated, we're no longer considered to have
    // displayed insecure content.
    displayed_insecure_content_ = false;
    SSLManager::NotifySSLInternalStateChanged(
        GetController().GetBrowserContext());
  }

  // Notify observers about navigation.
  FOR_EACH_OBSERVER(WebContentsObserver, observers_,
                    DidNavigateMainFrame(details, params));

  if (delegate_)
    delegate_->DidNavigateMainFramePostCommit(this);
  view_->SetOverscrollControllerEnabled(CanOverscrollContent());
}

void WebContentsImpl::DidNavigateAnyFramePostCommit(
    RenderFrameHostImpl* render_frame_host,
    const LoadCommittedDetails& details,
    const FrameHostMsg_DidCommitProvisionalLoad_Params& params) {
  // Now that something has committed, we don't need to track whether the
  // initial page has been accessed.
  has_accessed_initial_document_ = false;

  // If we navigate off the page, close all JavaScript dialogs.
  if (!details.is_in_page)
    CancelActiveAndPendingDialogs();

  // Notify observers about navigation.
  FOR_EACH_OBSERVER(WebContentsObserver, observers_,
                    DidNavigateAnyFrame(render_frame_host, details, params));
}

void WebContentsImpl::SetMainFrameMimeType(const std::string& mime_type) {
  contents_mime_type_ = mime_type;
}

bool WebContentsImpl::CanOverscrollContent() const {
  // Disable overscroll when touch emulation is on. See crbug.com/369938.
  if (force_disable_overscroll_content_)
    return false;

  if (delegate_)
    return delegate_->CanOverscrollContent();

  return false;
}

void WebContentsImpl::OnThemeColorChanged(SkColor theme_color) {
  // Update the theme color. This is to be published to observers after the
  // first visually non-empty paint.
  theme_color_ = theme_color;

  if (did_first_visually_non_empty_paint_ &&
      last_sent_theme_color_ != theme_color_) {
    FOR_EACH_OBSERVER(WebContentsObserver, observers_,
                      DidChangeThemeColor(theme_color_));
    last_sent_theme_color_ = theme_color_;
  }
}

void WebContentsImpl::OnDidLoadResourceFromMemoryCache(
    const GURL& url,
    const std::string& security_info,
    const std::string& http_method,
    const std::string& mime_type,
    ResourceType resource_type) {
  SSLStatus status;
  if (!DeserializeSecurityInfo(security_info, &status)) {
    bad_message::ReceivedBadMessage(
        GetRenderProcessHost(),
        bad_message::WC_MEMORY_CACHE_RESOURCE_BAD_SECURITY_INFO);
    return;
  }

  // Send out a notification that we loaded a resource from our memory cache.
  // TODO(alcutter,eranm): Pass signed_certificate_timestamp_ids into details.
  LoadFromMemoryCacheDetails details(
      url, GetRenderProcessHost()->GetID(), status.cert_id, status.cert_status,
      http_method, mime_type, resource_type);

  controller_.ssl_manager()->DidLoadFromMemoryCache(details);

  FOR_EACH_OBSERVER(WebContentsObserver, observers_,
                    DidLoadResourceFromMemoryCache(details));

  if (url.is_valid() && url.SchemeIsHTTPOrHTTPS()) {
    scoped_refptr<net::URLRequestContextGetter> request_context(
        resource_type == RESOURCE_TYPE_MEDIA ?
            GetBrowserContext()->GetMediaRequestContextForRenderProcess(
                GetRenderProcessHost()->GetID()) :
            GetBrowserContext()->GetRequestContextForRenderProcess(
                GetRenderProcessHost()->GetID()));
    BrowserThread::PostTask(
        BrowserThread::IO,
        FROM_HERE,
        base::Bind(&NotifyCacheOnIO, request_context, url, http_method));
  }
}

void WebContentsImpl::OnDidDisplayInsecureContent() {
  RecordAction(base::UserMetricsAction("SSL.DisplayedInsecureContent"));
  displayed_insecure_content_ = true;
  SSLManager::NotifySSLInternalStateChanged(
      GetController().GetBrowserContext());
}

void WebContentsImpl::OnDidRunInsecureContent(
    const std::string& security_origin, const GURL& target_url) {
  LOG(WARNING) << security_origin << " ran insecure content from "
               << target_url.possibly_invalid_spec();
  RecordAction(base::UserMetricsAction("SSL.RanInsecureContent"));
  if (base::EndsWith(security_origin, kDotGoogleDotCom,
                     base::CompareCase::INSENSITIVE_ASCII))
    RecordAction(base::UserMetricsAction("SSL.RanInsecureContentGoogle"));
  controller_.ssl_manager()->DidRunInsecureContent(security_origin);
  SSLManager::NotifySSLInternalStateChanged(
      GetController().GetBrowserContext());
}

void WebContentsImpl::OnDocumentLoadedInFrame() {
  if (!HasValidFrameSource())
    return;

  RenderFrameHostImpl* rfh =
      static_cast<RenderFrameHostImpl*>(render_frame_message_source_);
  FOR_EACH_OBSERVER(
      WebContentsObserver, observers_, DocumentLoadedInFrame(rfh));
}

void WebContentsImpl::OnDidFinishLoad(const GURL& url) {
  if (!HasValidFrameSource())
    return;

  GURL validated_url(url);
  RenderProcessHost* render_process_host =
      render_frame_message_source_->GetProcess();
  render_process_host->FilterURL(false, &validated_url);

  RenderFrameHostImpl* rfh =
      static_cast<RenderFrameHostImpl*>(render_frame_message_source_);
  FOR_EACH_OBSERVER(
      WebContentsObserver, observers_, DidFinishLoad(rfh, validated_url));
}

void WebContentsImpl::OnGoToEntryAtOffset(int offset) {
  if (!delegate_ || delegate_->OnGoToEntryOffset(offset))
    controller_.GoToOffset(offset);
}

void WebContentsImpl::OnUpdateZoomLimits(int minimum_percent,
                                         int maximum_percent) {
  minimum_zoom_percent_ = minimum_percent;
  maximum_zoom_percent_ = maximum_percent;
}

void WebContentsImpl::OnPageScaleFactorChanged(float page_scale_factor) {
  bool is_one = page_scale_factor == 1.f;
  if (is_one != page_scale_factor_is_one_) {
    page_scale_factor_is_one_ = is_one;

    HostZoomMapImpl* host_zoom_map =
        static_cast<HostZoomMapImpl*>(HostZoomMap::GetForWebContents(this));

    if (host_zoom_map && GetRenderProcessHost()) {
      host_zoom_map->SetPageScaleFactorIsOneForView(
          GetRenderProcessHost()->GetID(), GetRoutingID(),
          page_scale_factor_is_one_);
    }
  }

  FOR_EACH_OBSERVER(WebContentsObserver,
                    observers_,
                    OnPageScaleFactorChanged(page_scale_factor));
}

void WebContentsImpl::OnEnumerateDirectory(int request_id,
                                           const base::FilePath& path) {
  if (!delegate_)
    return;

  ChildProcessSecurityPolicyImpl* policy =
      ChildProcessSecurityPolicyImpl::GetInstance();
  if (policy->CanReadFile(GetRenderProcessHost()->GetID(), path))
    delegate_->EnumerateDirectory(this, request_id, path);
}

void WebContentsImpl::OnRegisterProtocolHandler(const std::string& protocol,
                                                const GURL& url,
                                                const base::string16& title,
                                                bool user_gesture) {
  if (!delegate_)
    return;

  ChildProcessSecurityPolicyImpl* policy =
      ChildProcessSecurityPolicyImpl::GetInstance();
  if (policy->IsPseudoScheme(protocol))
    return;

  delegate_->RegisterProtocolHandler(this, protocol, url, user_gesture);
}

void WebContentsImpl::OnUnregisterProtocolHandler(const std::string& protocol,
                                                  const GURL& url,
                                                  bool user_gesture) {
  if (!delegate_)
    return;

  ChildProcessSecurityPolicyImpl* policy =
      ChildProcessSecurityPolicyImpl::GetInstance();
  if (policy->IsPseudoScheme(protocol))
    return;

  delegate_->UnregisterProtocolHandler(this, protocol, url, user_gesture);
}

void WebContentsImpl::OnUpdatePageImportanceSignals(
    const PageImportanceSignals& signals) {
  page_importance_signals_ = signals;
}

void WebContentsImpl::OnFindReply(int request_id,
                                  int number_of_matches,
                                  const gfx::Rect& selection_rect,
                                  int active_match_ordinal,
                                  bool final_update) {
  if (delegate_) {
    delegate_->FindReply(this, request_id, number_of_matches, selection_rect,
                         active_match_ordinal, final_update);
  }
}

#if defined(OS_ANDROID) && !defined(USE_AURA)
void WebContentsImpl::OnFindMatchRectsReply(
    int version,
    const std::vector<gfx::RectF>& rects,
    const gfx::RectF& active_rect) {
  if (delegate_)
    delegate_->FindMatchRectsReply(this, version, rects, active_rect);
}

void WebContentsImpl::OnOpenDateTimeDialog(
    const ViewHostMsg_DateTimeDialogValue_Params& value) {
  date_time_chooser_->ShowDialog(GetTopLevelNativeWindow(),
                                 GetRenderViewHost(),
                                 value.dialog_type,
                                 value.dialog_value,
                                 value.minimum,
                                 value.maximum,
                                 value.step,
                                 value.suggestions);
}
#endif

void WebContentsImpl::OnDomOperationResponse(const std::string& json_string) {
  std::string json = json_string;
  NotificationService::current()->Notify(NOTIFICATION_DOM_OPERATION_RESPONSE,
                                         Source<WebContents>(this),
                                         Details<std::string>(&json));
}

void WebContentsImpl::OnAppCacheAccessed(const GURL& manifest_url,
                                         bool blocked_by_policy) {
  // Notify observers about navigation.
  FOR_EACH_OBSERVER(WebContentsObserver, observers_,
                    AppCacheAccessed(manifest_url, blocked_by_policy));
}

void WebContentsImpl::OnOpenColorChooser(
    int color_chooser_id,
    SkColor color,
    const std::vector<ColorSuggestion>& suggestions) {
  if (!HasValidFrameSource())
    return;

  ColorChooser* new_color_chooser = delegate_ ?
      delegate_->OpenColorChooser(this, color, suggestions) :
      NULL;
  if (!new_color_chooser)
    return;
  if (color_chooser_info_.get())
    color_chooser_info_->chooser->End();

  color_chooser_info_.reset(new ColorChooserInfo(
      render_frame_message_source_->GetProcess()->GetID(),
      render_frame_message_source_->GetRoutingID(),
      new_color_chooser,
      color_chooser_id));
}

void WebContentsImpl::OnEndColorChooser(int color_chooser_id) {
  if (color_chooser_info_ &&
      color_chooser_id == color_chooser_info_->identifier)
    color_chooser_info_->chooser->End();
}

void WebContentsImpl::OnSetSelectedColorInColorChooser(int color_chooser_id,
                                                       SkColor color) {
  if (color_chooser_info_ &&
      color_chooser_id == color_chooser_info_->identifier)
    color_chooser_info_->chooser->SetSelectedColor(color);
}

// This exists for render views that don't have a WebUI, but do have WebUI
// bindings enabled.
void WebContentsImpl::OnWebUISend(const GURL& source_url,
                                  const std::string& name,
                                  const base::ListValue& args) {
  if (delegate_)
    delegate_->WebUISend(this, source_url, name, args);
}

#if defined(ENABLE_PLUGINS)
void WebContentsImpl::OnPepperInstanceCreated() {
  FOR_EACH_OBSERVER(WebContentsObserver, observers_, PepperInstanceCreated());
}

void WebContentsImpl::OnPepperInstanceDeleted() {
  FOR_EACH_OBSERVER(WebContentsObserver, observers_, PepperInstanceDeleted());
}

void WebContentsImpl::OnPepperPluginHung(int plugin_child_id,
                                         const base::FilePath& path,
                                         bool is_hung) {
  UMA_HISTOGRAM_COUNTS("Pepper.PluginHung", 1);

  FOR_EACH_OBSERVER(WebContentsObserver, observers_,
                    PluginHungStatusChanged(plugin_child_id, path, is_hung));
}

void WebContentsImpl::OnPluginCrashed(const base::FilePath& plugin_path,
                                      base::ProcessId plugin_pid) {
  FOR_EACH_OBSERVER(WebContentsObserver, observers_,
                    PluginCrashed(plugin_path, plugin_pid));
}

void WebContentsImpl::OnRequestPpapiBrokerPermission(
    int routing_id,
    const GURL& url,
    const base::FilePath& plugin_path) {
  if (!delegate_) {
    OnPpapiBrokerPermissionResult(routing_id, false);
    return;
  }

  if (!delegate_->RequestPpapiBrokerPermission(
      this, url, plugin_path,
      base::Bind(&WebContentsImpl::OnPpapiBrokerPermissionResult,
                 base::Unretained(this), routing_id))) {
    NOTIMPLEMENTED();
    OnPpapiBrokerPermissionResult(routing_id, false);
  }
}

void WebContentsImpl::OnPpapiBrokerPermissionResult(int routing_id,
                                                    bool result) {
  Send(new ViewMsg_PpapiBrokerPermissionResult(routing_id, result));
}

void WebContentsImpl::OnBrowserPluginMessage(RenderFrameHost* render_frame_host,
                                             const IPC::Message& message) {
  CHECK(!browser_plugin_embedder_.get());
  CreateBrowserPluginEmbedderIfNecessary();
  browser_plugin_embedder_->OnMessageReceived(message, render_frame_host);
}
#endif  // defined(ENABLE_PLUGINS)

void WebContentsImpl::OnUpdateFaviconURL(
    const std::vector<FaviconURL>& candidates) {
  // We get updated favicon URLs after the page stops loading. If a cross-site
  // navigation occurs while a page is still loading, the initial page
  // may stop loading and send us updated favicon URLs after the navigation
  // for the new page has committed.
  RenderViewHostImpl* rvhi =
      static_cast<RenderViewHostImpl*>(render_view_message_source_);
  if (!rvhi->is_active())
    return;

  FOR_EACH_OBSERVER(WebContentsObserver, observers_,
                    DidUpdateFaviconURL(candidates));
}

void WebContentsImpl::CreateAudioPowerSaveBlocker() {
  DCHECK(!audio_power_save_blocker_);
  audio_power_save_blocker_ = PowerSaveBlocker::Create(
      PowerSaveBlocker::kPowerSaveBlockPreventAppSuspension,
      PowerSaveBlocker::kReasonAudioPlayback, "Playing audio");
}

void WebContentsImpl::CreateVideoPowerSaveBlocker() {
  DCHECK(!video_power_save_blocker_);
  DCHECK(!active_video_players_.empty());
  video_power_save_blocker_ = PowerSaveBlocker::Create(
      PowerSaveBlocker::kPowerSaveBlockPreventDisplaySleep,
      PowerSaveBlocker::kReasonVideoPlayback, "Playing video");
  //TODO(mfomitchev): Support PowerSaveBlocker on Aura - crbug.com/546718.
#if defined(OS_ANDROID) && !defined(USE_AURA)
static_cast<PowerSaveBlockerImpl*>(video_power_save_blocker_.get())
      ->InitDisplaySleepBlocker(this);
#endif
}

void WebContentsImpl::MaybeReleasePowerSaveBlockers() {
  // If there are no more audio players and we don't have audio stream
  // monitoring, release the audio power save blocker here instead of during
  // NotifyNavigationStateChanged().
  if (active_audio_players_.empty() &&
      !AudioStreamMonitor::monitoring_available()) {
    audio_power_save_blocker_.reset();
  }

  // If there are no more video players, clear the video power save blocker.
  if (active_video_players_.empty())
    video_power_save_blocker_.reset();
}

void WebContentsImpl::OnMediaPlayingNotification(int64 player_cookie,
                                                 bool has_video,
                                                 bool has_audio,
                                                 bool is_remote) {
  // Ignore the videos playing remotely and don't hold the wake lock for the
  // screen.
  if (is_remote) return;

  if (has_audio) {
    AddMediaPlayerEntry(player_cookie, &active_audio_players_);

    // If we don't have audio stream monitoring, allocate the audio power save
    // blocker here instead of during NotifyNavigationStateChanged().
    if (!audio_power_save_blocker_ &&
        !AudioStreamMonitor::monitoring_available()) {
      CreateAudioPowerSaveBlocker();
    }
  }

  if (has_video) {
    AddMediaPlayerEntry(player_cookie, &active_video_players_);

    // If we're not hidden and have just created a player, create a blocker.
    if (!video_power_save_blocker_ && !IsHidden())
      CreateVideoPowerSaveBlocker();
  }

  FOR_EACH_OBSERVER(WebContentsObserver, observers_, MediaStartedPlaying());
}

void WebContentsImpl::OnMediaPausedNotification(int64 player_cookie) {
  RemoveMediaPlayerEntry(player_cookie, &active_audio_players_);
  RemoveMediaPlayerEntry(player_cookie, &active_video_players_);
  MaybeReleasePowerSaveBlockers();

  FOR_EACH_OBSERVER(WebContentsObserver, observers_, MediaPaused());
}

#if defined(OS_ANDROID)

void WebContentsImpl::OnMediaSessionStateChanged() {
  MediaSession* session = MediaSession::Get(this);
  FOR_EACH_OBSERVER(WebContentsObserver, observers_,
                    MediaSessionStateChanged(session->IsControllable(),
                                             session->IsSuspended()));
}

void WebContentsImpl::ResumeMediaSession() {
  MediaSession::Get(this)->Resume();
}

void WebContentsImpl::SuspendMediaSession() {
  MediaSession::Get(this)->Suspend();
}

void WebContentsImpl::StopMediaSession() {
  MediaSession::Get(this)->Stop();
}

#endif  // defined(OS_ANDROID)

void WebContentsImpl::OnFirstVisuallyNonEmptyPaint() {
  FOR_EACH_OBSERVER(WebContentsObserver, observers_,
                    DidFirstVisuallyNonEmptyPaint());

  did_first_visually_non_empty_paint_ = true;

  if (theme_color_ != last_sent_theme_color_) {
    // Theme color should have updated by now if there was one.
    FOR_EACH_OBSERVER(WebContentsObserver, observers_,
                      DidChangeThemeColor(theme_color_));
    last_sent_theme_color_ = theme_color_;
  }
}

void WebContentsImpl::DidChangeVisibleSSLState() {
  if (delegate_) {
    delegate_->VisibleSSLStateChanged(this);

    SecurityStyleExplanations security_style_explanations;
    SecurityStyle security_style =
        delegate_->GetSecurityStyle(this, &security_style_explanations);
    FOR_EACH_OBSERVER(
        WebContentsObserver, observers_,
        SecurityStyleChanged(security_style, security_style_explanations));
  }
}

void WebContentsImpl::NotifyBeforeFormRepostWarningShow() {
  FOR_EACH_OBSERVER(WebContentsObserver, observers_,
                    BeforeFormRepostWarningShow());
}

void WebContentsImpl::ActivateAndShowRepostFormWarningDialog() {
  Activate();
  if (delegate_)
    delegate_->ShowRepostFormWarningDialog(this);
}

bool WebContentsImpl::HasAccessedInitialDocument() {
  return has_accessed_initial_document_;
}

// Notifies the RenderWidgetHost instance about the fact that the page is
// loading, or done loading.
void WebContentsImpl::SetIsLoading(bool is_loading,
                                   bool to_different_document,
                                   LoadNotificationDetails* details) {
  if (is_loading == is_loading_)
    return;

  if (!is_loading) {
    load_state_ = net::LoadStateWithParam(net::LOAD_STATE_IDLE,
                                          base::string16());
    load_state_host_.clear();
    upload_size_ = 0;
    upload_position_ = 0;
  }

  GetRenderManager()->SetIsLoading(is_loading);

  is_loading_ = is_loading;
  waiting_for_response_ = is_loading;
  is_load_to_different_document_ = to_different_document;

  if (delegate_)
    delegate_->LoadingStateChanged(this, to_different_document);
  NotifyNavigationStateChanged(INVALIDATE_TYPE_LOAD);

  std::string url = (details ? details->url.possibly_invalid_spec() : "NULL");
  if (is_loading) {
    TRACE_EVENT_ASYNC_BEGIN1("browser,navigation", "WebContentsImpl Loading",
                             this, "URL", url);
    FOR_EACH_OBSERVER(WebContentsObserver, observers_, DidStartLoading());
  } else {
    TRACE_EVENT_ASYNC_END1("browser,navigation", "WebContentsImpl Loading",
                           this, "URL", url);
    FOR_EACH_OBSERVER(WebContentsObserver, observers_, DidStopLoading());
  }

  // TODO(avi): Remove. http://crbug.com/170921
  int type = is_loading ? NOTIFICATION_LOAD_START : NOTIFICATION_LOAD_STOP;
  NotificationDetails det = NotificationService::NoDetails();
  if (details)
      det = Details<LoadNotificationDetails>(details);
  NotificationService::current()->Notify(
      type, Source<NavigationController>(&controller_), det);
}

void WebContentsImpl::UpdateMaxPageIDIfNecessary(RenderViewHost* rvh) {
  // If we are creating a RVH for a restored controller, then we need to make
  // sure the RenderView starts with a next_page_id_ larger than the number
  // of restored entries.  This must be called before the RenderView starts
  // navigating (to avoid a race between the browser updating max_page_id and
  // the renderer updating next_page_id_).  Because of this, we only call this
  // from CreateRenderView and allow that to notify the RenderView for us.
  int max_restored_page_id = controller_.GetMaxRestoredPageID();
  if (max_restored_page_id >
      GetMaxPageIDForSiteInstance(rvh->GetSiteInstance()))
    UpdateMaxPageIDForSiteInstance(rvh->GetSiteInstance(),
                                   max_restored_page_id);
}

bool WebContentsImpl::UpdateTitleForEntry(NavigationEntryImpl* entry,
                                          const base::string16& title) {
  // For file URLs without a title, use the pathname instead. In the case of a
  // synthesized title, we don't want the update to count toward the "one set
  // per page of the title to history."
  base::string16 final_title;
  bool explicit_set;
  if (entry && entry->GetURL().SchemeIsFile() && title.empty()) {
    final_title = base::UTF8ToUTF16(entry->GetURL().ExtractFileName());
    explicit_set = false;  // Don't count synthetic titles toward the set limit.
  } else {
    base::TrimWhitespace(title, base::TRIM_ALL, &final_title);
    explicit_set = true;
  }

  // If a page is created via window.open and never navigated,
  // there will be no navigation entry. In this situation,
  // |page_title_when_no_navigation_entry_| will be used for page title.
  if (entry) {
    if (final_title == entry->GetTitle())
      return false;  // Nothing changed, don't bother.

    entry->SetTitle(final_title);
  } else {
    if (page_title_when_no_navigation_entry_ == final_title)
      return false;  // Nothing changed, don't bother.

    page_title_when_no_navigation_entry_ = final_title;
  }

  // Lastly, set the title for the view.
  view_->SetPageTitle(final_title);

  FOR_EACH_OBSERVER(WebContentsObserver, observers_,
                    TitleWasSet(entry, explicit_set));

  return true;
}

void WebContentsImpl::SendChangeLoadProgress() {
  loading_last_progress_update_ = base::TimeTicks::Now();
  if (delegate_)
    delegate_->LoadProgressChanged(this, frame_tree_.load_progress());
}

void WebContentsImpl::ResetLoadProgressState() {
  frame_tree_.ResetLoadProgress();
  loading_weak_factory_.InvalidateWeakPtrs();
  loading_last_progress_update_ = base::TimeTicks();
}

void WebContentsImpl::NotifyViewSwapped(RenderViewHost* old_host,
                                        RenderViewHost* new_host) {
  // After sending out a swap notification, we need to send a disconnect
  // notification so that clients that pick up a pointer to |this| can NULL the
  // pointer.  See Bug 1230284.
  notify_disconnection_ = true;
  FOR_EACH_OBSERVER(WebContentsObserver, observers_,
                    RenderViewHostChanged(old_host, new_host));

  // Ensure that the associated embedder gets cleared after a RenderViewHost
  // gets swapped, so we don't reuse the same embedder next time a
  // RenderViewHost is attached to this WebContents.
  RemoveBrowserPluginEmbedder();
}

void WebContentsImpl::NotifyFrameSwapped(RenderFrameHost* old_host,
                                         RenderFrameHost* new_host) {
  FOR_EACH_OBSERVER(WebContentsObserver,
                    observers_,
                    RenderFrameHostChanged(old_host, new_host));
}

// TODO(avi): Remove this entire function because this notification is already
// covered by two observer functions. http://crbug.com/170921
void WebContentsImpl::NotifyDisconnected() {
  if (!notify_disconnection_)
    return;

  notify_disconnection_ = false;
  NotificationService::current()->Notify(
      NOTIFICATION_WEB_CONTENTS_DISCONNECTED,
      Source<WebContents>(this),
      NotificationService::NoDetails());
}

void WebContentsImpl::NotifyNavigationEntryCommitted(
    const LoadCommittedDetails& load_details) {
  FOR_EACH_OBSERVER(
      WebContentsObserver, observers_, NavigationEntryCommitted(load_details));
}

bool WebContentsImpl::OnMessageReceived(RenderFrameHost* render_frame_host,
                                        const IPC::Message& message) {
  return OnMessageReceived(NULL, render_frame_host, message);
}

const GURL& WebContentsImpl::GetMainFrameLastCommittedURL() const {
  return GetLastCommittedURL();
}

void WebContentsImpl::RenderFrameCreated(RenderFrameHost* render_frame_host) {
  FOR_EACH_OBSERVER(WebContentsObserver,
                    observers_,
                    RenderFrameCreated(render_frame_host));
  SetAccessibilityModeOnFrame(accessibility_mode_, render_frame_host);
}

void WebContentsImpl::RenderFrameDeleted(RenderFrameHost* render_frame_host) {
  ClearPowerSaveBlockers(render_frame_host);
  FOR_EACH_OBSERVER(WebContentsObserver,
                    observers_,
                    RenderFrameDeleted(render_frame_host));
}

void WebContentsImpl::ShowContextMenu(RenderFrameHost* render_frame_host,
                                      const ContextMenuParams& params) {
  ContextMenuParams context_menu_params(params);
  // Allow WebContentsDelegates to handle the context menu operation first.
  if (delegate_ && delegate_->HandleContextMenu(context_menu_params))
    return;

  render_view_host_delegate_view_->ShowContextMenu(render_frame_host,
                                                   context_menu_params);
}

void WebContentsImpl::RunJavaScriptMessage(
    RenderFrameHost* render_frame_host,
    const base::string16& message,
    const base::string16& default_prompt,
    const GURL& frame_url,
    JavaScriptMessageType javascript_message_type,
    IPC::Message* reply_msg) {
  // Suppress JavaScript dialogs when requested. Also suppress messages when
  // showing an interstitial as it's shown over the previous page and we don't
  // want the hidden page's dialogs to interfere with the interstitial.
  bool suppress_this_message =
      static_cast<RenderFrameHostImpl*>(render_frame_host)->is_swapped_out() ||
      ShowingInterstitialPage() || !delegate_ ||
      delegate_->ShouldSuppressDialogs(this) ||
      !delegate_->GetJavaScriptDialogManager(this);

  if (!suppress_this_message) {
    std::string accept_lang = GetContentClient()->browser()->
      GetAcceptLangs(GetBrowserContext());
    dialog_manager_ = delegate_->GetJavaScriptDialogManager(this);
    dialog_manager_->RunJavaScriptDialog(
        this, frame_url, accept_lang, javascript_message_type, message,
        default_prompt,
        base::Bind(&WebContentsImpl::OnDialogClosed, base::Unretained(this),
                   render_frame_host->GetProcess()->GetID(),
                   render_frame_host->GetRoutingID(), reply_msg, false),
        &suppress_this_message);
  }

  if (suppress_this_message) {
    // If we are suppressing messages, just reply as if the user immediately
    // pressed "Cancel", passing true to |dialog_was_suppressed|.
    OnDialogClosed(render_frame_host->GetProcess()->GetID(),
                   render_frame_host->GetRoutingID(), reply_msg,
                   true, false, base::string16());
  }

  // OnDialogClosed (two lines up) may have caused deletion of this object (see
  // http://crbug.com/288961 ). The only safe thing to do here is return.
}

void WebContentsImpl::RunBeforeUnloadConfirm(
    RenderFrameHost* render_frame_host,
    const base::string16& message,
    bool is_reload,
    IPC::Message* reply_msg) {
  RenderFrameHostImpl* rfhi =
      static_cast<RenderFrameHostImpl*>(render_frame_host);
  if (delegate_)
    delegate_->WillRunBeforeUnloadConfirm();

  bool suppress_this_message =
      rfhi->rfh_state() != RenderFrameHostImpl::STATE_DEFAULT ||
      ShowingInterstitialPage() || !delegate_ ||
      delegate_->ShouldSuppressDialogs(this) ||
      !delegate_->GetJavaScriptDialogManager(this);
  if (suppress_this_message) {
    rfhi->JavaScriptDialogClosed(reply_msg, true, base::string16(), true);
    return;
  }

  is_showing_before_unload_dialog_ = true;
  dialog_manager_ = delegate_->GetJavaScriptDialogManager(this);
  dialog_manager_->RunBeforeUnloadDialog(
      this, message, is_reload,
      base::Bind(&WebContentsImpl::OnDialogClosed, base::Unretained(this),
                 render_frame_host->GetProcess()->GetID(),
                 render_frame_host->GetRoutingID(), reply_msg,
                 false));
}

WebContents* WebContentsImpl::GetAsWebContents() {
  return this;
}

bool WebContentsImpl::IsNeverVisible() {
  if (!delegate_)
    return false;
  return delegate_->IsNeverVisible(this);
}

#if defined(OS_WIN)
gfx::NativeViewAccessible WebContentsImpl::GetParentNativeViewAccessible() {
  return accessible_parent_;
}
#endif

RenderViewHostDelegateView* WebContentsImpl::GetDelegateView() {
  return render_view_host_delegate_view_;
}

RendererPreferences WebContentsImpl::GetRendererPrefs(
    BrowserContext* browser_context) const {
  return renderer_preferences_;
}

gfx::Rect WebContentsImpl::GetRootWindowResizerRect(
    RenderWidgetHostImpl* render_widget_host) const {
  if (!RenderViewHostImpl::From(render_widget_host))
    return gfx::Rect();

  if (delegate_)
    return delegate_->GetRootWindowResizerRect();
  return gfx::Rect();
}

void WebContentsImpl::RemoveBrowserPluginEmbedder() {
  if (browser_plugin_embedder_)
    browser_plugin_embedder_.reset();
}

void WebContentsImpl::RenderViewCreated(RenderViewHost* render_view_host) {
  // Don't send notifications if we are just creating a swapped-out RVH for
  // the opener chain.  These won't be used for view-source or WebUI, so it's
  // ok to return early.
  if (!static_cast<RenderViewHostImpl*>(render_view_host)->is_active())
    return;

  if (delegate_)
    view_->SetOverscrollControllerEnabled(CanOverscrollContent());

  NotificationService::current()->Notify(
      NOTIFICATION_WEB_CONTENTS_RENDER_VIEW_HOST_CREATED,
      Source<WebContents>(this),
      Details<RenderViewHost>(render_view_host));

  // When we're creating views, we're still doing initial setup, so we always
  // use the pending Web UI rather than any possibly existing committed one.
  if (GetRenderManager()->pending_web_ui())
    GetRenderManager()->pending_web_ui()->RenderViewCreated(render_view_host);

  if (base::CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kEnableBrowserSideNavigation) &&
      GetRenderManager()->speculative_web_ui()) {
    GetRenderManager()->speculative_web_ui()->RenderViewCreated(
        render_view_host);
  }

  NavigationEntry* entry = controller_.GetPendingEntry();
  if (entry && entry->IsViewSourceMode()) {
    // Put the renderer in view source mode.
    render_view_host->Send(
        new ViewMsg_EnableViewSourceMode(render_view_host->GetRoutingID()));
  }

  view_->RenderViewCreated(render_view_host);

  FOR_EACH_OBSERVER(
      WebContentsObserver, observers_, RenderViewCreated(render_view_host));
}

void WebContentsImpl::RenderViewReady(RenderViewHost* rvh) {
  if (rvh != GetRenderViewHost()) {
    // Don't notify the world, since this came from a renderer in the
    // background.
    return;
  }

  notify_disconnection_ = true;
  // TODO(avi): Remove. http://crbug.com/170921
  NotificationService::current()->Notify(
      NOTIFICATION_WEB_CONTENTS_CONNECTED,
      Source<WebContents>(this),
      NotificationService::NoDetails());

  bool was_crashed = IsCrashed();
  SetIsCrashed(base::TERMINATION_STATUS_STILL_RUNNING, 0);

  // Restore the focus to the tab (otherwise the focus will be on the top
  // window).
  if (was_crashed && !FocusLocationBarByDefault() &&
      (!delegate_ || delegate_->ShouldFocusPageAfterCrash())) {
    view_->Focus();
  }

  FOR_EACH_OBSERVER(WebContentsObserver, observers_, RenderViewReady());
}

void WebContentsImpl::RenderViewTerminated(RenderViewHost* rvh,
                                           base::TerminationStatus status,
                                           int error_code) {
  if (rvh != GetRenderViewHost()) {
    // The pending page's RenderViewHost is gone.
    return;
  }

  // Ensure fullscreen mode is exited in the |delegate_| since a crashed
  // renderer may not have made a clean exit.
  if (IsFullscreenForCurrentTab(GetRenderViewHost()->GetWidget()))
    ExitFullscreenMode();

  // Cancel any visible dialogs so they are not left dangling over the sad tab.
  CancelActiveAndPendingDialogs();

  if (delegate_)
    delegate_->HideValidationMessage(this);

  SetIsLoading(false, true, nullptr);
  NotifyDisconnected();
  SetIsCrashed(status, error_code);

  // Reset the loading progress. TODO(avi): What does it mean to have a
  // "renderer crash" when there is more than one renderer process serving a
  // webpage? Once this function is called at a more granular frame level, we
  // probably will need to more granularly reset the state here.
  ResetLoadProgressState();

  FOR_EACH_OBSERVER(WebContentsObserver,
                    observers_,
                    RenderProcessGone(GetCrashedStatus()));
}

void WebContentsImpl::RenderViewDeleted(RenderViewHost* rvh) {
  FOR_EACH_OBSERVER(WebContentsObserver, observers_, RenderViewDeleted(rvh));
}

void WebContentsImpl::UpdateState(RenderViewHost* rvh,
                                  int32 page_id,
                                  const PageState& page_state) {
  DCHECK(!SiteIsolationPolicy::UseSubframeNavigationEntries());

  // Ensure that this state update comes from a RenderViewHost that belongs to
  // this WebContents.
  // TODO(nasko): This should go through RenderFrameHost.
  if (rvh->GetDelegate()->GetAsWebContents() != this)
    return;

  // We must be prepared to handle state updates for any page. They occur
  // when the user is scrolling and entering form data, as well as when we're
  // leaving a page, in which case our state may have already been moved to
  // the next page. The navigation controller will look up the appropriate
  // NavigationEntry and update it when it is notified via the delegate.
  RenderViewHostImpl* rvhi = static_cast<RenderViewHostImpl*>(rvh);
  NavigationEntryImpl* entry = controller_.GetEntryWithPageID(
      rvhi->GetSiteInstance(), page_id);
  if (!entry)
    return;

  NavigationEntryImpl* new_entry = controller_.GetEntryWithUniqueID(
      static_cast<RenderFrameHostImpl*>(rvhi->GetMainFrame())->nav_entry_id());

  DCHECK_EQ(entry, new_entry);

  if (page_state == entry->GetPageState())
    return;  // Nothing to update.
  entry->SetPageState(page_state);
  controller_.NotifyEntryChanged(entry);
}

void WebContentsImpl::UpdateTargetURL(RenderViewHost* render_view_host,
                                      const GURL& url) {
  if (fullscreen_widget_routing_id_ != MSG_ROUTING_NONE) {
    // If we're fullscreen only update the url if it's from the fullscreen
    // renderer.
    RenderWidgetHostView* fs = GetFullscreenRenderWidgetHostView();
    if (fs && fs->GetRenderWidgetHost() != render_view_host->GetWidget())
      return;
  }
  if (delegate_)
    delegate_->UpdateTargetURL(this, url);
}

void WebContentsImpl::Close(RenderViewHost* rvh) {
#if defined(OS_MACOSX)
  // The UI may be in an event-tracking loop, such as between the
  // mouse-down and mouse-up in text selection or a button click.
  // Defer the close until after tracking is complete, so that we
  // don't free objects out from under the UI.
  // TODO(shess): This could get more fine-grained.  For instance,
  // closing a tab in another window while selecting text in the
  // current window's Omnibox should be just fine.
  if (view_->IsEventTracking()) {
    view_->CloseTabAfterEventTracking();
    return;
  }
#endif

  // Ignore this if it comes from a RenderViewHost that we aren't showing.
  if (delegate_ && rvh == GetRenderViewHost())
    delegate_->CloseContents(this);
}

void WebContentsImpl::SwappedOut(RenderFrameHost* rfh) {
  if (delegate_ && rfh->GetRenderViewHost() == GetRenderViewHost())
    delegate_->SwappedOut(this);
}

void WebContentsImpl::RequestMove(const gfx::Rect& new_bounds) {
  if (delegate_ && delegate_->IsPopupOrPanel(this))
    delegate_->MoveContents(this, new_bounds);
}

void WebContentsImpl::DidStartLoading(FrameTreeNode* frame_tree_node,
                                      bool to_different_document) {
  SetIsLoading(true, to_different_document, nullptr);

  // Notify accessibility that the user is navigating away from the
  // current document.
  //
  // TODO(dmazzoni): do this using a WebContentsObserver.
  BrowserAccessibilityManager* manager =
      frame_tree_node->current_frame_host()->browser_accessibility_manager();
  if (manager)
    manager->UserIsNavigatingAway();
}

void WebContentsImpl::DidStopLoading() {
  scoped_ptr<LoadNotificationDetails> details;

  // Use the last committed entry rather than the active one, in case a
  // pending entry has been created.
  NavigationEntry* entry = controller_.GetLastCommittedEntry();
  Navigator* navigator = frame_tree_.root()->navigator();

  // An entry may not exist for a stop when loading an initial blank page or
  // if an iframe injected by script into a blank page finishes loading.
  if (entry) {
    base::TimeDelta elapsed =
        base::TimeTicks::Now() - navigator->GetCurrentLoadStart();

    details.reset(new LoadNotificationDetails(
        entry->GetVirtualURL(),
        entry->GetTransitionType(),
        elapsed,
        &controller_,
        controller_.GetCurrentEntryIndex()));
  }

  SetIsLoading(false, true, details.get());
}

void WebContentsImpl::DidChangeLoadProgress() {
  double load_progress = frame_tree_.load_progress();

  // The delegate is notified immediately for the first and last updates. Also,
  // since the message loop may be pretty busy when a page is loaded, it might
  // not execute a posted task in a timely manner so the progress report is sent
  // immediately if enough time has passed.
  base::TimeDelta min_delay =
      base::TimeDelta::FromMilliseconds(kMinimumDelayBetweenLoadingUpdatesMS);
  bool delay_elapsed = loading_last_progress_update_.is_null() ||
      base::TimeTicks::Now() - loading_last_progress_update_ > min_delay;

  if (load_progress == 0.0 || load_progress == 1.0 || delay_elapsed) {
    // If there is a pending task to send progress, it is now obsolete.
    loading_weak_factory_.InvalidateWeakPtrs();

    // Notify the load progress change.
    SendChangeLoadProgress();

    // Clean-up the states if needed.
    if (load_progress == 1.0)
      ResetLoadProgressState();
    return;
  }

  if (loading_weak_factory_.HasWeakPtrs())
    return;

  base::ThreadTaskRunnerHandle::Get()->PostDelayedTask(
      FROM_HERE, base::Bind(&WebContentsImpl::SendChangeLoadProgress,
                            loading_weak_factory_.GetWeakPtr()),
      min_delay);
}

void WebContentsImpl::DidCancelLoading() {
  controller_.DiscardNonCommittedEntries();

  // Update the URL display.
  NotifyNavigationStateChanged(INVALIDATE_TYPE_URL);
}

void WebContentsImpl::DidAccessInitialDocument() {
  has_accessed_initial_document_ = true;

  // We may have left a failed browser-initiated navigation in the address bar
  // to let the user edit it and try again.  Clear it now that content might
  // show up underneath it.
  if (!IsLoading() && controller_.GetPendingEntry())
    controller_.DiscardPendingEntry(false);

  // Update the URL display.
  NotifyNavigationStateChanged(INVALIDATE_TYPE_URL);
}

void WebContentsImpl::DidChangeName(RenderFrameHost* render_frame_host,
                                    const std::string& name) {
  FOR_EACH_OBSERVER(WebContentsObserver, observers_,
                    FrameNameChanged(render_frame_host, name));
}

void WebContentsImpl::DocumentOnLoadCompleted(
    RenderFrameHost* render_frame_host) {
  FOR_EACH_OBSERVER(WebContentsObserver, observers_,
                    DocumentOnLoadCompletedInMainFrame());

  // TODO(avi): Remove. http://crbug.com/170921
  NotificationService::current()->Notify(
      NOTIFICATION_LOAD_COMPLETED_MAIN_FRAME,
      Source<WebContents>(this),
      NotificationService::NoDetails());
}

void WebContentsImpl::UpdateStateForFrame(RenderFrameHost* render_frame_host,
                                          const PageState& page_state) {
  DCHECK(SiteIsolationPolicy::UseSubframeNavigationEntries());

  // The state update affects the last NavigationEntry associated with the given
  // |render_frame_host|. This may not be the last committed NavigationEntry (as
  // in the case of an UpdateState from a frame being swapped out). We track
  // which entry this is in the RenderFrameHost's nav_entry_id.
  RenderFrameHostImpl* rfhi =
      static_cast<RenderFrameHostImpl*>(render_frame_host);
  NavigationEntryImpl* entry =
      controller_.GetEntryWithUniqueID(rfhi->nav_entry_id());
  if (!entry)
    return;

  FrameNavigationEntry* frame_entry =
      entry->GetFrameEntry(rfhi->frame_tree_node());
  if (!frame_entry)
    return;

  // The SiteInstance might not match if we do a cross-process navigation with
  // replacement (e.g., auto-subframe), in which case the swap out of the old
  // RenderFrameHost runs in the background after the old FrameNavigationEntry
  // has already been replaced and destroyed.
  if (frame_entry->site_instance() != rfhi->GetSiteInstance())
    return;

  if (page_state == frame_entry->page_state())
    return;  // Nothing to update.

  frame_entry->set_page_state(page_state);
  controller_.NotifyEntryChanged(entry);
}

void WebContentsImpl::UpdateTitle(RenderFrameHost* render_frame_host,
                                  int32 page_id,
                                  const base::string16& title,
                                  base::i18n::TextDirection title_direction) {
  // If we have a title, that's a pretty good indication that we've started
  // getting useful data.
  SetNotWaitingForResponse();

  // Try to find the navigation entry, which might not be the current one.
  // For example, it might be from a recently swapped out RFH.
  NavigationEntryImpl* entry = controller_.GetEntryWithPageID(
      render_frame_host->GetSiteInstance(), page_id);

  NavigationEntryImpl* new_entry = controller_.GetEntryWithUniqueID(
      static_cast<RenderFrameHostImpl*>(render_frame_host)->nav_entry_id());
  DCHECK_EQ(entry, new_entry);

  // We can handle title updates when we don't have an entry in
  // UpdateTitleForEntry, but only if the update is from the current RVH.
  // TODO(avi): Change to make decisions based on the RenderFrameHost.
  if (!entry && render_frame_host != GetMainFrame())
    return;

  // TODO(evan): make use of title_direction.
  // http://code.google.com/p/chromium/issues/detail?id=27094
  if (!UpdateTitleForEntry(entry, title))
    return;

  // Broadcast notifications when the UI should be updated.
  if (entry == controller_.GetEntryAtOffset(0))
    NotifyNavigationStateChanged(INVALIDATE_TYPE_TITLE);
}

void WebContentsImpl::UpdateEncoding(RenderFrameHost* render_frame_host,
                                     const std::string& encoding) {
  SetEncoding(encoding);
}

void WebContentsImpl::DocumentAvailableInMainFrame(
    RenderViewHost* render_view_host) {
  FOR_EACH_OBSERVER(WebContentsObserver, observers_,
                    DocumentAvailableInMainFrame());
}

void WebContentsImpl::RouteCloseEvent(RenderViewHost* rvh) {
  // Tell the active RenderViewHost to run unload handlers and close, as long
  // as the request came from a RenderViewHost in the same BrowsingInstance.
  // In most cases, we receive this from a swapped out RenderViewHost.
  // It is possible to receive it from one that has just been swapped in,
  // in which case we might as well deliver the message anyway.
  if (rvh->GetSiteInstance()->IsRelatedSiteInstance(GetSiteInstance()))
    ClosePage();
}

bool WebContentsImpl::ShouldRouteMessageEvent(
    RenderFrameHost* target_rfh,
    SiteInstance* source_site_instance) const {
  // Allow the message if this WebContents is dedicated to a browser plugin
  // guest.
  // Note: This check means that an embedder could theoretically receive a
  // postMessage from anyone (not just its own guests). However, this is
  // probably not a risk for apps since other pages won't have references
  // to App windows.
  return GetBrowserPluginGuest() || GetBrowserPluginEmbedder();
}

void WebContentsImpl::EnsureOpenerProxiesExist(RenderFrameHost* source_rfh) {
  WebContentsImpl* source_web_contents = static_cast<WebContentsImpl*>(
      WebContents::FromRenderFrameHost(source_rfh));

  if (source_web_contents) {
    // If this message is going to outer WebContents from inner WebContents,
    // then we should not create a RenderView. AttachToOuterWebContentsFrame()
    // already created a RenderFrameProxyHost for that purpose.
    if (GetBrowserPluginEmbedder() &&
        BrowserPluginGuestMode::UseCrossProcessFramesForGuests()) {
      return;
    }

    if (GetBrowserPluginGuest()) {
      // We create a swapped out RenderView or RenderFrameProxyHost for the
      // embedder in the guest's render process but we intentionally do not
      // expose the embedder's opener chain to it.
      if (SiteIsolationPolicy::IsSwappedOutStateForbidden()) {
        source_web_contents->GetRenderManager()->CreateRenderFrameProxy(
            GetSiteInstance());
      } else {
        source_web_contents->CreateSwappedOutRenderView(GetSiteInstance());
      }
    } else {
      RenderFrameHostImpl* source_rfhi =
          static_cast<RenderFrameHostImpl*>(source_rfh);
      source_rfhi->frame_tree_node()->render_manager()->CreateOpenerProxies(
          GetSiteInstance(), nullptr);
    }
  }
}

bool WebContentsImpl::AddMessageToConsole(int32 level,
                                          const base::string16& message,
                                          int32 line_no,
                                          const base::string16& source_id) {
  if (!delegate_)
    return false;
  return delegate_->AddMessageToConsole(this, level, message, line_no,
                                        source_id);
}

int WebContentsImpl::CreateSwappedOutRenderView(
    SiteInstance* instance) {
  int render_view_routing_id = MSG_ROUTING_NONE;
  if (SiteIsolationPolicy::IsSwappedOutStateForbidden()) {
    GetRenderManager()->CreateRenderFrameProxy(instance);
  } else {
    GetRenderManager()->CreateRenderFrame(
        instance, nullptr, CREATE_RF_SWAPPED_OUT | CREATE_RF_HIDDEN,
        &render_view_routing_id);
  }
  return render_view_routing_id;
}

void WebContentsImpl::OnUserGesture(RenderWidgetHostImpl* render_widget_host) {
  if (render_widget_host != GetRenderViewHost()->GetWidget())
    return;

  // Notify observers.
  FOR_EACH_OBSERVER(WebContentsObserver, observers_, DidGetUserGesture());

  ResourceDispatcherHostImpl* rdh = ResourceDispatcherHostImpl::Get();
  if (rdh)  // NULL in unittests.
    rdh->OnUserGesture(this);
}

void WebContentsImpl::OnUserInteraction(const blink::WebInputEvent::Type type) {
  FOR_EACH_OBSERVER(WebContentsObserver, observers_,
                    DidGetUserInteraction(type));
}

void WebContentsImpl::OnIgnoredUIEvent() {
  // Notify observers.
  FOR_EACH_OBSERVER(WebContentsObserver, observers_, DidGetIgnoredUIEvent());
}

void WebContentsImpl::RendererUnresponsive(
    RenderWidgetHostImpl* render_widget_host) {
  // Don't show hung renderer dialog for a swapped out RVH.
  if (render_widget_host != GetRenderViewHost()->GetWidget())
    return;

  RenderFrameHostImpl* rfhi =
      static_cast<RenderFrameHostImpl*>(GetRenderViewHost()->GetMainFrame());

  // Ignore renderer unresponsive event if debugger is attached to the tab
  // since the event may be a result of the renderer sitting on a breakpoint.
  // See http://crbug.com/65458
  if (DevToolsAgentHost::IsDebuggerAttached(this))
    return;

  if (rfhi->is_waiting_for_beforeunload_ack() ||
      rfhi->IsWaitingForUnloadACK()) {
    // Hang occurred while firing the beforeunload/unload handler.
    // Pretend the handler fired so tab closing continues as if it had.
    GetRenderViewHost()->set_sudden_termination_allowed(true);

    if (!GetRenderManager()->ShouldCloseTabOnUnresponsiveRenderer())
      return;

    // If the tab hangs in the beforeunload/unload handler there's really
    // nothing we can do to recover. If the hang is in the beforeunload handler,
    // pretend the beforeunload listeners have all fired and allow the delegate
    // to continue closing; the user will not have the option of cancelling the
    // close. Otherwise, pretend the unload listeners have all fired and close
    // the tab.
    bool close = true;
    if (rfhi->is_waiting_for_beforeunload_ack() && delegate_) {
      delegate_->BeforeUnloadFired(this, true, &close);
    }
    if (close)
      Close();
    return;
  }

  if (!GetRenderViewHost() || !GetRenderViewHost()->IsRenderViewLive())
    return;

  if (delegate_)
    delegate_->RendererUnresponsive(this);
}

void WebContentsImpl::RendererResponsive(
    RenderWidgetHostImpl* render_widget_host) {
  if (render_widget_host != GetRenderViewHost()->GetWidget())
    return;

  if (delegate_)
    delegate_->RendererResponsive(this);
}

void WebContentsImpl::LoadStateChanged(
    const GURL& url,
    const net::LoadStateWithParam& load_state,
    uint64 upload_position,
    uint64 upload_size) {
  // TODO(erikchen): Remove ScopedTracker below once http://crbug.com/466285
  // is fixed.
  tracked_objects::ScopedTracker tracking_profile1(
      FROM_HERE_WITH_EXPLICIT_FUNCTION(
          "466285 WebContentsImpl::LoadStateChanged::Start"));
  load_state_ = load_state;
  upload_position_ = upload_position;
  upload_size_ = upload_size;
  load_state_host_ = url_formatter::IDNToUnicode(
      url.host(),
      GetContentClient()->browser()->GetAcceptLangs(GetBrowserContext()));
  if (load_state_.state == net::LOAD_STATE_READING_RESPONSE)
    SetNotWaitingForResponse();
  if (IsLoading()) {
    NotifyNavigationStateChanged(static_cast<InvalidateTypes>(
        INVALIDATE_TYPE_LOAD | INVALIDATE_TYPE_TAB));
  }
}

void WebContentsImpl::BeforeUnloadFiredFromRenderManager(
    bool proceed, const base::TimeTicks& proceed_time,
    bool* proceed_to_fire_unload) {
  FOR_EACH_OBSERVER(WebContentsObserver, observers_,
                    BeforeUnloadFired(proceed_time));
  if (delegate_)
    delegate_->BeforeUnloadFired(this, proceed, proceed_to_fire_unload);
  // Note: |this| might be deleted at this point.
}

void WebContentsImpl::RenderProcessGoneFromRenderManager(
    RenderViewHost* render_view_host) {
  DCHECK(crashed_status_ != base::TERMINATION_STATUS_STILL_RUNNING);
  RenderViewTerminated(render_view_host, crashed_status_, crashed_error_code_);
}

void WebContentsImpl::UpdateRenderViewSizeForRenderManager() {
  // TODO(brettw) this is a hack. See WebContentsView::SizeContents.
  gfx::Size size = GetSizeForNewRenderView();
  // 0x0 isn't a valid window size (minimal window size is 1x1) but it may be
  // here during container initialization and normal window size will be set
  // later. In case of tab duplication this resizing to 0x0 prevents setting
  // normal size later so just ignore it.
  if (!size.IsEmpty())
    view_->SizeContents(size);
}

void WebContentsImpl::CancelModalDialogsForRenderManager() {
  // We need to cancel modal dialogs when doing a process swap, since the load
  // deferrer would prevent us from swapping out. We also clear the state
  // because this is a cross-process navigation, which means that it's a new
  // site that should not have to pay for the sins of its predecessor.
  //
  // Note that we don't bother telling browser_plugin_embedder_ because the
  // cross-process navigation will either destroy the browser plugins or not
  // require their dialogs to close.
  if (dialog_manager_)
    dialog_manager_->ResetDialogState(this);
}

void WebContentsImpl::NotifySwappedFromRenderManager(RenderFrameHost* old_host,
                                                     RenderFrameHost* new_host,
                                                     bool is_main_frame) {
  if (is_main_frame) {
    NotifyViewSwapped(old_host ? old_host->GetRenderViewHost() : nullptr,
                      new_host->GetRenderViewHost());

    // Make sure the visible RVH reflects the new delegate's preferences.
    if (delegate_)
      view_->SetOverscrollControllerEnabled(CanOverscrollContent());

    view_->RenderViewSwappedIn(new_host->GetRenderViewHost());
  }

  NotifyFrameSwapped(old_host, new_host);
}

void WebContentsImpl::NotifyMainFrameSwappedFromRenderManager(
    RenderViewHost* old_host,
    RenderViewHost* new_host) {
  NotifyViewSwapped(old_host, new_host);
}

NavigationControllerImpl& WebContentsImpl::GetControllerForRenderManager() {
  return GetController();
}

scoped_ptr<WebUIImpl> WebContentsImpl::CreateWebUIForRenderManager(
    const GURL& url) {
  return scoped_ptr<WebUIImpl>(static_cast<WebUIImpl*>(CreateWebUI(
      url, std::string())));
}

NavigationEntry*
    WebContentsImpl::GetLastCommittedNavigationEntryForRenderManager() {
  return controller_.GetLastCommittedEntry();
}

void WebContentsImpl::CreateRenderWidgetHostViewForRenderManager(
    RenderViewHost* render_view_host) {
  RenderWidgetHostViewBase* rwh_view = nullptr;
  bool is_guest_in_site_per_process =
      !!browser_plugin_guest_.get() &&
      BrowserPluginGuestMode::UseCrossProcessFramesForGuests();
  if (is_guest_in_site_per_process) {
    RenderWidgetHostViewChildFrame* rwh_view_child =
        new RenderWidgetHostViewChildFrame(render_view_host->GetWidget());
    rwh_view = rwh_view_child;
  } else {
    rwh_view = view_->CreateViewForWidget(render_view_host->GetWidget(), false);
  }

  // Now that the RenderView has been created, we need to tell it its size.
  if (rwh_view)
    rwh_view->SetSize(GetSizeForNewRenderView());
}

bool WebContentsImpl::CreateRenderViewForRenderManager(
    RenderViewHost* render_view_host,
    int opener_frame_routing_id,
    int proxy_routing_id,
    const FrameReplicationState& replicated_frame_state) {
  TRACE_EVENT0("browser,navigation",
               "WebContentsImpl::CreateRenderViewForRenderManager");

  if (proxy_routing_id == MSG_ROUTING_NONE)
    CreateRenderWidgetHostViewForRenderManager(render_view_host);

  // Make sure we use the correct starting page_id in the new RenderView.
  UpdateMaxPageIDIfNecessary(render_view_host);
  int32 max_page_id =
      GetMaxPageIDForSiteInstance(render_view_host->GetSiteInstance());

  if (!static_cast<RenderViewHostImpl*>(render_view_host)
           ->CreateRenderView(opener_frame_routing_id, proxy_routing_id,
                              max_page_id, replicated_frame_state,
                              created_with_opener_)) {
    return false;
  }

  SetHistoryOffsetAndLengthForView(render_view_host,
                                   controller_.GetLastCommittedEntryIndex(),
                                   controller_.GetEntryCount());

#if defined(OS_POSIX) && !defined(OS_MACOSX) && !defined(OS_ANDROID)
  // Force a ViewMsg_Resize to be sent, needed to make plugins show up on
  // linux. See crbug.com/83941.
  RenderWidgetHostView* rwh_view = render_view_host->GetWidget()->GetView();
  if (rwh_view) {
    if (RenderWidgetHost* render_widget_host = rwh_view->GetRenderWidgetHost())
      render_widget_host->WasResized();
  }
#endif

  return true;
}

bool WebContentsImpl::CreateRenderFrameForRenderManager(
    RenderFrameHost* render_frame_host,
    int proxy_routing_id,
    int opener_routing_id,
    int parent_routing_id,
    int previous_sibling_routing_id) {
  TRACE_EVENT0("browser,navigation",
               "WebContentsImpl::CreateRenderFrameForRenderManager");

  RenderFrameHostImpl* rfh =
      static_cast<RenderFrameHostImpl*>(render_frame_host);
  if (!rfh->CreateRenderFrame(proxy_routing_id, opener_routing_id,
                              parent_routing_id, previous_sibling_routing_id))
    return false;

  // TODO(nasko): When RenderWidgetHost is owned by RenderFrameHost, the passed
  // RenderFrameHost will have to be associated with the appropriate
  // RenderWidgetHostView or a new one should be created here.

  return true;
}

#if defined(OS_ANDROID) && !defined(USE_AURA)

base::android::ScopedJavaLocalRef<jobject>
WebContentsImpl::GetJavaWebContents() {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  return GetWebContentsAndroid()->GetJavaObject();
}

WebContentsAndroid* WebContentsImpl::GetWebContentsAndroid() {
  WebContentsAndroid* web_contents_android =
      static_cast<WebContentsAndroid*>(GetUserData(kWebContentsAndroidKey));
  if (!web_contents_android) {
    web_contents_android = new WebContentsAndroid(this);
    SetUserData(kWebContentsAndroidKey, web_contents_android);
  }
  return web_contents_android;
}

bool WebContentsImpl::CreateRenderViewForInitialEmptyDocument() {
  return CreateRenderViewForRenderManager(
      GetRenderViewHost(), MSG_ROUTING_NONE, MSG_ROUTING_NONE,
      frame_tree_.root()->current_replication_state());
}

#elif defined(OS_MACOSX)

void WebContentsImpl::SetAllowOtherViews(bool allow) {
  view_->SetAllowOtherViews(allow);
}

bool WebContentsImpl::GetAllowOtherViews() {
  return view_->GetAllowOtherViews();
}

#endif

void WebContentsImpl::OnDialogClosed(int render_process_id,
                                     int render_frame_id,
                                     IPC::Message* reply_msg,
                                     bool dialog_was_suppressed,
                                     bool success,
                                     const base::string16& user_input) {
  RenderFrameHostImpl* rfh = RenderFrameHostImpl::FromID(render_process_id,
                                                         render_frame_id);
  last_dialog_suppressed_ = dialog_was_suppressed;

  if (is_showing_before_unload_dialog_ && !success) {
    // If a beforeunload dialog is canceled, we need to stop the throbber from
    // spinning, since we forced it to start spinning in Navigate.
    if (rfh)
      DidStopLoading();
    controller_.DiscardNonCommittedEntries();

    FOR_EACH_OBSERVER(WebContentsObserver, observers_,
                      BeforeUnloadDialogCancelled());
  }

  is_showing_before_unload_dialog_ = false;
  if (rfh) {
    rfh->JavaScriptDialogClosed(reply_msg, success, user_input,
                                dialog_was_suppressed);
  } else {
    // Don't leak the sync IPC reply if the RFH or process is gone.
    delete reply_msg;
  }
}

void WebContentsImpl::SetEncoding(const std::string& encoding) {
  if (encoding == last_reported_encoding_)
    return;
  last_reported_encoding_ = encoding;

  canonical_encoding_ = GetContentClient()->browser()->
      GetCanonicalEncodingNameByAliasName(encoding);
}

bool WebContentsImpl::IsHidden() {
  return capturer_count_ == 0 && !should_normally_be_visible_;
}

int WebContentsImpl::GetOuterDelegateFrameTreeNodeID() {
  if (node_ && node_->outer_web_contents())
    return node_->outer_contents_frame_tree_node_id();

  return FrameTreeNode::kFrameTreeNodeInvalidID;
}

RenderFrameHostManager* WebContentsImpl::GetRenderManager() const {
  return frame_tree_.root()->render_manager();
}

WebContentsImpl* WebContentsImpl::GetOuterWebContents() {
  if (BrowserPluginGuestMode::UseCrossProcessFramesForGuests()) {
    if (node_)
      return node_->outer_web_contents();
  } else {
    if (GetBrowserPluginGuest())
      return GetBrowserPluginGuest()->embedder_web_contents();
  }
  return nullptr;
}

BrowserPluginGuest* WebContentsImpl::GetBrowserPluginGuest() const {
  return browser_plugin_guest_.get();
}

void WebContentsImpl::SetBrowserPluginGuest(BrowserPluginGuest* guest) {
  CHECK(!browser_plugin_guest_);
  CHECK(guest);
  browser_plugin_guest_.reset(guest);
}

BrowserPluginEmbedder* WebContentsImpl::GetBrowserPluginEmbedder() const {
  return browser_plugin_embedder_.get();
}

void WebContentsImpl::CreateBrowserPluginEmbedderIfNecessary() {
  if (browser_plugin_embedder_)
    return;
  browser_plugin_embedder_.reset(BrowserPluginEmbedder::Create(this));
}

void WebContentsImpl::ClearPowerSaveBlockers(
    RenderFrameHost* render_frame_host) {
  RemoveAllMediaPlayerEntries(render_frame_host, &active_audio_players_);
  RemoveAllMediaPlayerEntries(render_frame_host, &active_video_players_);
  MaybeReleasePowerSaveBlockers();
}

void WebContentsImpl::ClearAllPowerSaveBlockers() {
  active_audio_players_.clear();
  active_video_players_.clear();
  audio_power_save_blocker_.reset();
  video_power_save_blocker_.reset();
}

gfx::Size WebContentsImpl::GetSizeForNewRenderView() {
  gfx::Size size;
  if (delegate_)
    size = delegate_->GetSizeForNewRenderView(this);
  if (size.IsEmpty())
    size = GetContainerBounds().size();
  return size;
}

void WebContentsImpl::OnFrameRemoved(RenderFrameHost* render_frame_host) {
  FOR_EACH_OBSERVER(
      WebContentsObserver, observers_, FrameDeleted(render_frame_host));
}

void WebContentsImpl::OnPreferredSizeChanged(const gfx::Size& old_size) {
  if (!delegate_)
    return;
  const gfx::Size new_size = GetPreferredSize();
  if (new_size != old_size)
    delegate_->UpdatePreferredSize(this, new_size);
}

void WebContentsImpl::AddMediaPlayerEntry(int64 player_cookie,
                                          ActiveMediaPlayerMap* player_map) {
  if (!HasValidFrameSource())
    return;

  const uintptr_t key =
      reinterpret_cast<uintptr_t>(render_frame_message_source_);
  DCHECK(std::find((*player_map)[key].begin(),
                   (*player_map)[key].end(),
                   player_cookie) == (*player_map)[key].end());
  (*player_map)[key].push_back(player_cookie);
}

void WebContentsImpl::RemoveMediaPlayerEntry(int64 player_cookie,
                                             ActiveMediaPlayerMap* player_map) {
  if (!HasValidFrameSource())
    return;

  const uintptr_t key =
      reinterpret_cast<uintptr_t>(render_frame_message_source_);
  ActiveMediaPlayerMap::iterator it = player_map->find(key);
  if (it == player_map->end())
    return;

  // Remove the player.
  PlayerList::iterator player_it =
      std::find(it->second.begin(), it->second.end(), player_cookie);
  if (player_it != it->second.end())
    it->second.erase(player_it);

  // If there are no players left, remove the map entry.
  if (it->second.empty())
    player_map->erase(it);
}

void WebContentsImpl::RemoveAllMediaPlayerEntries(
    RenderFrameHost* render_frame_host,
    ActiveMediaPlayerMap* player_map) {
  ActiveMediaPlayerMap::iterator it =
      player_map->find(reinterpret_cast<uintptr_t>(render_frame_host));
  if (it == player_map->end())
    return;
  player_map->erase(it);
}

WebUI* WebContentsImpl::CreateWebUI(const GURL& url,
                                    const std::string& frame_name) {
  WebUIImpl* web_ui = new WebUIImpl(this, frame_name);
  WebUIController* controller = WebUIControllerFactoryRegistry::GetInstance()->
      CreateWebUIControllerForURL(web_ui, url);
  if (controller) {
    web_ui->AddMessageHandler(new GenericHandler());
    web_ui->SetController(controller);
    return web_ui;
  }

  delete web_ui;
  return NULL;
}

void WebContentsImpl::SetForceDisableOverscrollContent(bool force_disable) {
  force_disable_overscroll_content_ = force_disable;
  if (view_)
    view_->SetOverscrollControllerEnabled(CanOverscrollContent());
}

}  // namespace content
