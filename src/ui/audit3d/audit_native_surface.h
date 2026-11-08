#pragma once
#include <QWidget>
#include <QPointer>
#include <QLibrary>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QTimer>
#include <QFile>
#include <QResource>
#include <QUrl>
#include <QUuid>
#include <QSet>
#include <atomic>
#include <algorithm>
#include <QJsonArray>
#include <functional>
#include <windows.h>
#include <shlwapi.h>
#include <WebView2.h>
#include "sdk_uuid_mingw.h"

namespace pgn::audit3d {
template<class Interface,class... Args> class Callback final:public Interface{
 std::atomic<ULONG> count_{1};std::function<HRESULT(Args...)> function_;
public:
 explicit Callback(std::function<HRESULT(Args...)> function):function_(std::move(function)){}
 HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out)override{
  if(!out)return E_POINTER;*out=nullptr;
  if(id==__uuidof(IUnknown)||id==__uuidof(Interface)){*out=static_cast<Interface*>(this);AddRef();return S_OK;}return E_NOINTERFACE;
 }
 ULONG STDMETHODCALLTYPE AddRef()override{return ++count_;}
 ULONG STDMETHODCALLTYPE Release()override{const auto n=--count_;if(!n)delete this;return n;}
 HRESULT STDMETHODCALLTYPE Invoke(Args... args)override{return function_(args...);}
};
class Surface final:public QWidget{
 ICoreWebView2Environment* environment_{};ICoreWebView2Controller* controller_{};ICoreWebView2* web_{};
 QLibrary loader_;QString error_,profile_,assets_,resourceFile_,resourcePrefix_,entry_{"animation.html"};QJsonObject latest_;bool loaded_{},coinit_{};
 using Create=HRESULT(STDAPICALLTYPE*)(PCWSTR,PCWSTR,ICoreWebView2EnvironmentOptions*,ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler*);
 void fail(const QString& text){error_=text;setProperty("pgn_audit3d_error",text);update();if(onError)onError(text);}
 void fit(){if(controller_){RECT r{0,0,LONG(width()*devicePixelRatioF()),LONG(height()*devicePixelRatioF())};controller_->put_Bounds(r);controller_->put_IsVisible(isVisible());}}
 HRESULT resourceResponse(ICoreWebView2WebResourceRequestedEventArgs* args,const QString& text){
  setProperty("pgn_audit3d_resource_requests",property("pgn_audit3d_resource_requests").toInt()+1);setProperty("pgn_audit3d_last_resource",text);
  const QUrl url(text);static const QSet<QString> names{"network-animation.html","audit_network_controller.js","audit_network_scene.mjs","audit_activity_model.mjs","four-audit-entities.glb","assets.json","WEBVIEW2-LICENSE.txt"};
  const auto name=url.path().mid(1);if(url.scheme()!="https"||url.host()!="pgn-audit.invalid"||url.port()!=-1||!url.userInfo().isEmpty()||!names.contains(name))return E_INVALIDARG;
  QResource resource(":"+resourcePrefix_+"/"+name);if(!resource.isValid()||resource.compressionAlgorithm()!=QResource::NoCompression||resource.size()<0||resource.size()>90*1024*1024){setProperty("pgn_audit3d_resource_error",QString("valid=%1 compression=%2 bytes=%3").arg(resource.isValid()).arg(int(resource.compressionAlgorithm())).arg(resource.size()));return E_FAIL;}
  IStream* stream=SHCreateMemStream(resource.data(),UINT(resource.size()));if(!stream)return E_OUTOFMEMORY;
  const QString mime=name.endsWith(".html")?"text/html; charset=utf-8":name.endsWith(".js")||name.endsWith(".mjs")?"application/javascript; charset=utf-8":name.endsWith(".glb")?"model/gltf-binary":name.endsWith(".json")?"application/json":"text/plain; charset=utf-8";
  const auto headers=("Content-Type: "+mime+"\r\nContent-Length: "+QString::number(resource.size())+"\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff").toStdWString();ICoreWebView2WebResourceResponse* response{};
  auto hr=environment_->CreateWebResourceResponse(stream,200,L"OK",headers.c_str(),&response);stream->Release();if(SUCCEEDED(hr)){hr=args->put_Response(response);response->Release();}return hr;
 }
 void configure(){
  const QPointer<Surface> guard(this);web_->get_Settings(&settings_);
  if(settings_){settings_->put_AreDevToolsEnabled(FALSE);settings_->put_AreDefaultContextMenusEnabled(FALSE);settings_->put_IsStatusBarEnabled(FALSE);settings_->put_IsZoomControlEnabled(FALSE);settings_->put_AreHostObjectsAllowed(FALSE);}
  if(resourceFile_.isEmpty()){
   ICoreWebView2_3* mapping{};
   if(FAILED(web_->QueryInterface(__uuidof(ICoreWebView2_3),reinterpret_cast<void**>(&mapping)))){fail("The installed WebView2 runtime does not support local 3D assets");return;}
   const auto folder=QDir::toNativeSeparators(assets_).toStdWString();const auto hr=mapping->SetVirtualHostNameToFolderMapping(L"pgn-audit.invalid",folder.c_str(),COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_DENY_CORS);mapping->Release();
   if(FAILED(hr)){fail("Local 3D assets could not be mapped");return;}
  }
  auto navigation=new Callback<ICoreWebView2NavigationStartingEventHandler,ICoreWebView2*,ICoreWebView2NavigationStartingEventArgs*>([](auto*,auto* args){LPWSTR uri{};if(SUCCEEDED(args->get_Uri(&uri))){const QString url=QString::fromWCharArray(uri);CoTaskMemFree(uri);if(!url.startsWith("https://pgn-audit.invalid/"))args->put_Cancel(TRUE);}return S_OK;});
  EventRegistrationToken token{};web_->add_NavigationStarting(navigation,&token);navigation->Release();
  // The renderer receives public display state, never a wallet/RPC bridge.
  // Deny external subresources as well as top-level navigation.
  web_->AddWebResourceRequestedFilter(L"*",COREWEBVIEW2_WEB_RESOURCE_CONTEXT_ALL);
  auto resources=new Callback<ICoreWebView2WebResourceRequestedEventHandler,ICoreWebView2*,ICoreWebView2WebResourceRequestedEventArgs*>([guard](auto*,auto* args){
   if(!guard)return S_OK;ICoreWebView2WebResourceRequest* request{};LPWSTR uri{};QString url;
   if(SUCCEEDED(args->get_Request(&request))&&request){if(SUCCEEDED(request->get_Uri(&uri))){url=QString::fromWCharArray(uri);CoTaskMemFree(uri);}request->Release();}
   if(url.startsWith("https://pgn-audit.invalid/")){if(guard->resourceFile_.isEmpty())return S_OK;if(SUCCEEDED(guard->resourceResponse(args,url)))return S_OK;}
   if(url.startsWith("blob:https://pgn-audit.invalid/")||url.startsWith("data:"))return S_OK;
   ICoreWebView2WebResourceResponse* denied{};
   if(SUCCEEDED(guard->environment_->CreateWebResourceResponse(nullptr,403,L"Forbidden",L"Content-Type: text/plain",&denied))){args->put_Response(denied);denied->Release();}
   guard->setProperty("pgn_audit3d_external_requests_blocked",guard->property("pgn_audit3d_external_requests_blocked").toInt()+1);return S_OK;
  });web_->add_WebResourceRequested(resources,&token);resources->Release();
  auto windows=new Callback<ICoreWebView2NewWindowRequestedEventHandler,ICoreWebView2*,ICoreWebView2NewWindowRequestedEventArgs*>([](auto*,auto* args){args->put_Handled(TRUE);return S_OK;});web_->add_NewWindowRequested(windows,&token);windows->Release();
  ICoreWebView2_4* downloads{};
  if(SUCCEEDED(web_->QueryInterface(__uuidof(ICoreWebView2_4),reinterpret_cast<void**>(&downloads)))){
   auto deny=new Callback<ICoreWebView2DownloadStartingEventHandler,ICoreWebView2*,ICoreWebView2DownloadStartingEventArgs*>([](auto*,auto* args){args->put_Cancel(TRUE);args->put_Handled(TRUE);return S_OK;});downloads->add_DownloadStarting(deny,&token);deny->Release();downloads->Release();
  }
  auto permission=new Callback<ICoreWebView2PermissionRequestedEventHandler,ICoreWebView2*,ICoreWebView2PermissionRequestedEventArgs*>([](auto*,auto* args){args->put_State(COREWEBVIEW2_PERMISSION_STATE_DENY);return S_OK;});web_->add_PermissionRequested(permission,&token);permission->Release();
  auto message=new Callback<ICoreWebView2WebMessageReceivedEventHandler,ICoreWebView2*,ICoreWebView2WebMessageReceivedEventArgs*>([guard](auto*,auto* args){
   if(!guard)return S_OK;LPWSTR origin{};if(FAILED(args->get_Source(&origin)))return S_OK;const QString source=QString::fromWCharArray(origin);CoTaskMemFree(origin);if(!source.startsWith("https://pgn-audit.invalid/"))return S_OK;
   LPWSTR value{};if(FAILED(args->TryGetWebMessageAsString(&value)))return S_OK;const QString text=QString::fromWCharArray(value);CoTaskMemFree(value);if(text.size()>32768){guard->fail("A 3D animation message exceeded its size limit");return S_OK;}
   const auto json=QJsonDocument::fromJson(text.toUtf8()).object();if(json.value("event")=="ready"){guard->loaded_=true;guard->setProperty("pgn_audit3d_ready",true);if(!guard->latest_.isEmpty())guard->present(guard->latest_);if(guard->onReady)guard->onReady();}else if(json.value("event")=="watched"&&guard->onWatched){const auto rows=json.value("requests").toArray();QJsonArray safe;if(rows.size()<=4)for(const auto& row:rows){const auto id=row.toString();if(id.size()==64&&std::all_of(id.begin(),id.end(),[](QChar c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');}))safe.append(id);}guard->onWatched(safe);}else if(json.value("event")=="probe"&&guard->onProbe)guard->onProbe(json);else if(json.value("event")=="error")guard->fail(json.value("message").toString().left(500));return S_OK;
  });web_->add_WebMessageReceived(message,&token);message->Release();fit();const auto uri=("https://pgn-audit.invalid/"+entry_).toStdWString();web_->Navigate(uri.c_str());
 }
 ICoreWebView2Settings* settings_{};
protected:
 void resizeEvent(QResizeEvent* e)override{QWidget::resizeEvent(e);fit();}
 void showEvent(QShowEvent* e)override{QWidget::showEvent(e);fit();}
 void hideEvent(QHideEvent* e)override{QWidget::hideEvent(e);fit();suspend();}
 void paintEvent(QPaintEvent*)override{if(!loaded_){QPainter p(this);p.fillRect(rect(),QColor("#03131b"));if(!error_.isEmpty()){p.setPen(QColor("#e1bc7d"));p.drawText(rect().adjusted(18,8,-18,-8),Qt::AlignCenter|Qt::TextWordWrap,error_);}}}
public:
 std::function<void()> onReady;std::function<void(QString)> onError;std::function<void(QJsonObject)> onProbe;std::function<void(QJsonArray)> onWatched;
 explicit Surface(QWidget* parent=nullptr):QWidget(parent){setAttribute(Qt::WA_NativeWindow);setMinimumHeight(180);}
 ~Surface(){if(controller_){controller_->Close();controller_->Release();}if(settings_)settings_->Release();if(web_)web_->Release();if(environment_)environment_->Release();if(!resourceFile_.isEmpty())QResource::unregisterResource(resourceFile_,resourcePrefix_);if(coinit_)CoUninitialize();}
 void start(const QString& assets,const QString& profile,const QString& loader,const QString& entry="animation.html"){
  if(web_||environment_)return;
  if(entry!="animation.html"&&entry!="network-animation.html"){fail("Unknown local animation entry");return;}entry_=entry;
  const QFileInfo root(assets);if(root.isSymLink()){fail("The 3D animation assets are unavailable");return;}
  if(root.isFile()&&root.suffix()=="rcc"&&root.size()<=100*1024*1024){resourcePrefix_="/pgn-audit-"+QUuid::createUuid().toString(QUuid::WithoutBraces);if(!QResource::registerResource(root.canonicalFilePath(),resourcePrefix_)){fail("The signed 3D resource collection could not be loaded");return;}resourceFile_=root.canonicalFilePath();}
  else if(!root.isDir()||!QFileInfo::exists(QDir(assets).filePath(entry))){fail("The 3D animation assets are unavailable");return;}
  assets_=root.canonicalFilePath();profile_=QDir::toNativeSeparators(profile);QDir().mkpath(profile_);
  const HRESULT co=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);coinit_=SUCCEEDED(co);if(FAILED(co)&&co!=RPC_E_CHANGED_MODE){fail("The 3D renderer could not initialize");return;}
  loader_.setFileName(QFileInfo(loader).absoluteFilePath());loader_.setLoadHints(QLibrary::PreventUnloadHint);if(!loader_.load()){fail("The local WebView2 loader is unavailable");return;}
  const auto create=reinterpret_cast<Create>(loader_.resolve("CreateCoreWebView2EnvironmentWithOptions"));if(!create){fail("The WebView2 loader is incompatible");return;}
  const QPointer<Surface> guard(this);
  auto environment=new Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler,HRESULT,ICoreWebView2Environment*>([guard](HRESULT hr,auto* env){
   if(!guard)return S_OK;if(FAILED(hr)||!env){guard->fail("Install the Microsoft WebView2 Runtime to enable the 3D animation");return S_OK;}env->AddRef();guard->environment_=env;
   auto controller=new Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler,HRESULT,ICoreWebView2Controller*>([guard](HRESULT result,auto* value){
    if(!guard)return S_OK;if(FAILED(result)||!value){guard->fail("The 3D display could not be created");return S_OK;}value->AddRef();guard->controller_=value;value->get_CoreWebView2(&guard->web_);if(guard->web_)guard->configure();return S_OK;});
   const auto result=env->CreateCoreWebView2Controller(reinterpret_cast<HWND>(guard->winId()),controller);controller->Release();if(FAILED(result))guard->fail("The 3D display could not be initialized");return S_OK;
  });
  const auto folder=profile_.toStdWString();const auto hr=create(nullptr,folder.c_str(),nullptr,environment);environment->Release();if(FAILED(hr))fail("The WebView2 Runtime is unavailable");
  QTimer::singleShot(20000,this,[guard]{if(guard&&!guard->loaded_&&guard->error_.isEmpty())guard->fail("The 3D renderer did not become ready; network work remains available");});
 }
 void suspend(){latest_={};if(web_)web_->ExecuteScript(L"window.pgnAuditSuspend?.()",nullptr);}
 void present(const QJsonObject& value){latest_=value;if(!loaded_||!web_)return;const auto script=(QString("window.pgnAuditApply?.(")+QString::fromUtf8(QJsonDocument(value).toJson(QJsonDocument::Compact))+QStringLiteral(");")).toStdWString();web_->ExecuteScript(script.c_str(),nullptr);}
 void preview(const QJsonObject& points={}){if(web_){const auto script=(QString("window.pgnAuditPreview?.(")+QString::fromUtf8(QJsonDocument(points).toJson(QJsonDocument::Compact))+QStringLiteral(");")).toStdWString();web_->ExecuteScript(script.c_str(),nullptr);}}
 void probe(){if(web_)web_->ExecuteScript(L"window.pgnAuditProbe?.()",nullptr);}
 void probePaths(){if(web_)web_->ExecuteScript(L"window.pgnAuditTrajectoryProbe?.()",nullptr);}
 void project(const QJsonObject& value){if(loaded_&&web_){const auto script=(QString("window.pgnAuditProject?.(")+QString::fromUtf8(QJsonDocument(value).toJson(QJsonDocument::Compact))+QStringLiteral(");")).toStdWString();web_->ExecuteScript(script.c_str(),nullptr);}}
 void capture(const QString& path,std::function<void(bool)> done){
  if(!web_){done(false);return;}IStream* stream{};if(FAILED(CreateStreamOnHGlobal(nullptr,TRUE,&stream))){done(false);return;}
  stream->AddRef();auto callback=new Callback<ICoreWebView2CapturePreviewCompletedHandler,HRESULT>([stream,path,done](HRESULT hr){
   bool saved=false;HGLOBAL memory{};STATSTG stat{};
   if(SUCCEEDED(hr)&&SUCCEEDED(stream->Stat(&stat,STATFLAG_NONAME))&&stat.cbSize.QuadPart<=8*1024*1024&&SUCCEEDED(GetHGlobalFromStream(stream,&memory))){
    if(void* bytes=GlobalLock(memory)){QFile file(path);if(file.open(QIODevice::WriteOnly))saved=file.write(static_cast<const char*>(bytes),stat.cbSize.QuadPart)==qint64(stat.cbSize.QuadPart);GlobalUnlock(memory);}
   }stream->Release();done(saved);return S_OK;
  });const auto hr=web_->CapturePreview(COREWEBVIEW2_CAPTURE_PREVIEW_IMAGE_FORMAT_PNG,stream,callback);callback->Release();stream->Release();if(FAILED(hr)){stream->Release();done(false);}
 }
};
}
