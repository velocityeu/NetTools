#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <array>
#include <thread>
#include <functional>
#include <exception>
#include "veu/subnet_pane.hpp"
#include "veu/network_pane.hpp"
#include "veu/storage.hpp"
#include "veu/ui_common.hpp"
#include "veu/help.hpp"
#include "../resources/resource.h"
namespace {
struct App{HWND window{},tree{},subnet{},network{},status{};HFONT font{};int dpi=96,selected=10;bool dirty=false,loading=false,closing=false,saving=false;std::uint64_t revision=0;std::filesystem::path path;};
App* app{};
enum{Open=100,Save,SaveAs,Exit,Help,About,StopAll,Website,GitHub};
int px(int n){return MulDiv(n,app->dpi,96);}
void status(const wchar_t* s){SendMessageW(app->status,SB_SETTEXTW,0,reinterpret_cast<LPARAM>(s));}
void layout(){if(!app->tree)return;RECT r{};GetClientRect(app->window,&r);SendMessageW(app->status,WM_SIZE,0,0);int nav=px(195),h=r.bottom-px(26);MoveWindow(app->tree,px(8),px(8),nav-px(8),h-px(16),TRUE);MoveWindow(app->subnet,nav,0,std::max(200,int(r.right)-nav),h,TRUE);MoveWindow(app->network,nav,0,std::max(200,int(r.right)-nav),h,TRUE);}
void sync_tree(int n){if(!app->tree)return;auto walk=[&](auto&& self,HTREEITEM item)->HTREEITEM{for(;item;item=TreeView_GetNextSibling(app->tree,item)){TVITEMW v{};v.mask=TVIF_PARAM;v.hItem=item;TreeView_GetItem(app->tree,&v);if(v.lParam==n)return item;if(auto child=self(self,TreeView_GetChild(app->tree,item)))return child;}return nullptr;};auto item=walk(walk,TreeView_GetRoot(app->tree));if(item&&item!=TreeView_GetSelection(app->tree)){app->loading=true;TreeView_SelectItem(app->tree,item);app->loading=false;}}
void select(int n){sync_tree(n);app->selected=n;ShowWindow(app->subnet,n<10?SW_SHOW:SW_HIDE);ShowWindow(app->network,n>=10?SW_SHOW:SW_HIDE);if(n<10)veu::select_subnet_tool(app->subnet,unsigned(n));else veu::select_network_tool(app->network,static_cast<veu::net::Tool>(n-10));layout();}
void title(){SetWindowTextW(app->window,app->dirty?L"Velocity NetTools — development preview *":L"Velocity NetTools — development preview");}
void file_work(const std::function<void()>& work){
 if(app->saving)throw std::runtime_error("A file operation is already active.");
 HANDLE complete=CreateEventW(nullptr,TRUE,FALSE,nullptr);if(!complete)throw std::runtime_error("Cannot create file completion event.");
 std::exception_ptr failure;std::thread worker;
 try{worker=std::thread([&]{try{work();}catch(...){failure=std::current_exception();}SetEvent(complete);});}catch(...){CloseHandle(complete);throw;}
 app->saving=true;
 while(WaitForSingleObject(complete,0)!=WAIT_OBJECT_0){
  MsgWaitForMultipleObjectsEx(1,&complete,100,QS_ALLINPUT,MWMO_INPUTAVAILABLE);
  MSG m{};while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}
 }
 worker.join();CloseHandle(complete);app->saving=false;if(failure)std::rethrow_exception(failure);
}
bool save(bool choose=false){if(app->saving)return false;try{auto path=choose||app->path.empty()?veu::ui::file_dialog(app->window,true):app->path;if(path.empty())return false;veu::storage::Plan p;p.active_tool=unsigned(app->selected);p.fields=veu::subnet_plan_fields(app->subnet);auto fields=veu::network_plan_fields(app->network);p.fields.insert(fields.begin(),fields.end());auto revision=app->revision;status(L"Saving plan… the interface remains available.");file_work([&]{veu::storage::safe_save(path,veu::storage::encode(p));});app->path=path;app->dirty=app->revision!=revision;title();status(app->dirty?L"Saved the earlier snapshot; newer edits are still unsaved.":L"Plan saved. Previous destination bytes are retained in a sibling recovery file.");return !app->dirty;}catch(const std::exception& e){veu::ui::error(app->window,e);return false;}}
bool discard(){if(!app->dirty)return true;int r=MessageBoxW(app->window,L"Save changes to this plan?",L"Velocity NetTools",MB_YESNOCANCEL|MB_ICONQUESTION);return r==IDNO||(r==IDYES&&save());}
void open(){if(app->saving)return;try{auto path=veu::ui::file_dialog(app->window,false);if(path.empty())return;veu::storage::Plan p;file_work([&]{p=veu::storage::decode(veu::storage::read(path));});if(!((p.active_tool<5)||(p.active_tool>=10&&p.active_tool<=20)))throw std::runtime_error("This plan uses an unsupported tool.");if(!discard())return;
 auto oldSubnet=veu::subnet_plan_fields(app->subnet),oldNetwork=veu::network_plan_fields(app->network);app->loading=true;
 try{veu::load_subnet_plan_fields(app->subnet,p.fields);veu::load_network_plan_fields(app->network,p.fields);}catch(...){veu::load_subnet_plan_fields(app->subnet,oldSubnet);veu::load_network_plan_fields(app->network,oldNetwork);app->loading=false;throw;}
 select(int(p.active_tool));app->loading=false;app->path=path;app->dirty=false;title();status(L"Plan opened. Network probes were not started.");
 }catch(const std::exception& e){app->loading=false;veu::ui::error(app->window,e);}}
void help(){if(app->selected<10){HELPINFO hi{sizeof(hi)};SendMessageW(app->subnet,WM_HELP,0,reinterpret_cast<LPARAM>(&hi));}else{HELPINFO hi{sizeof(hi)};SendMessageW(app->network,WM_HELP,0,reinterpret_cast<LPARAM>(&hi));}}
void command(int id){switch(id){case Open:open();break;case Save:save();break;case SaveAs:save(true);break;case Exit:SendMessageW(app->window,WM_CLOSE,0,0);break;case Help:help();break;case About:veu::show_about(app->window);break;case StopAll:veu::stop_network(app->network);veu::stop_subnet(app->subnet);status(L"Stopping all jobs… active calls drain safely.");break;case Website:ShellExecuteW(app->window,L"open",L"https://www.velocity-eu.com/",nullptr,nullptr,SW_SHOWNORMAL);break;case GitHub:ShellExecuteW(app->window,L"open",L"https://github.com/velocityeu/NetTools",nullptr,nullptr,SW_SHOWNORMAL);break;}}
void init(){
 HDC dc=GetDC(app->window);app->dpi=GetDeviceCaps(dc,LOGPIXELSX);ReleaseDC(app->window,dc);
 NONCLIENTMETRICSW m{sizeof(m)};SystemParametersInfoW(SPI_GETNONCLIENTMETRICS,sizeof(m),&m,0);app->font=CreateFontIndirectW(&m.lfMessageFont);
 auto menu=CreateMenu(),file=CreatePopupMenu(),tools=CreatePopupMenu(),h=CreatePopupMenu();
 AppendMenuW(file,MF_STRING,Open,L"&Open plan…\tCtrl+O");AppendMenuW(file,MF_STRING,Save,L"&Save plan\tCtrl+S");AppendMenuW(file,MF_STRING,SaveAs,L"Save &as…");AppendMenuW(file,MF_SEPARATOR,0,nullptr);AppendMenuW(file,MF_STRING,Exit,L"E&xit");
 AppendMenuW(tools,MF_STRING,StopAll,L"&Stop all jobs");
 AppendMenuW(h,MF_STRING,Help,L"&Help and examples\tF1");AppendMenuW(h,MF_STRING,Website,L"Velocity EU &website");AppendMenuW(h,MF_STRING,GitHub,L"&GitHub repository");AppendMenuW(h,MF_STRING,About,L"&About Velocity NetTools");
 AppendMenuW(menu,MF_POPUP,UINT_PTR(file),L"&File");AppendMenuW(menu,MF_POPUP,UINT_PTR(tools),L"&Tools");AppendMenuW(menu,MF_POPUP,UINT_PTR(h),L"&Help");SetMenu(app->window,menu);
 app->subnet=veu::create_subnet_pane(app->window);app->network=veu::create_network_pane(app->window);
 app->tree=CreateWindowExW(WS_EX_CLIENTEDGE,WC_TREEVIEWW,L"Tools",WS_CHILD|WS_VISIBLE|WS_TABSTOP|TVS_HASLINES|TVS_LINESATROOT|TVS_HASBUTTONS|TVS_SHOWSELALWAYS,0,0,0,0,app->window,reinterpret_cast<HMENU>(20),GetModuleHandleW(nullptr),nullptr);SendMessageW(app->tree,WM_SETFONT,reinterpret_cast<WPARAM>(app->font),TRUE);
 auto add=[&](const wchar_t* group,std::initializer_list<std::pair<const wchar_t*,int>> entries){TVINSERTSTRUCTW ins{};ins.hParent=TVI_ROOT;ins.hInsertAfter=TVI_LAST;ins.item.mask=TVIF_TEXT|TVIF_PARAM;ins.item.pszText=const_cast<wchar_t*>(group);ins.item.lParam=-1;auto parent=TreeView_InsertItem(app->tree,&ins);for(auto [name,id]:entries){ins.hParent=parent;ins.item.pszText=const_cast<wchar_t*>(name);ins.item.lParam=id;auto item=TreeView_InsertItem(app->tree,&ins);if(id==10)TreeView_SelectItem(app->tree,item);}TreeView_Expand(app->tree,parent,TVE_EXPAND);};
 // Network values follow veu::net::Tool; subnet IDs are separate.
 add(L"Addressing",{{L"My PC & IP",10},{L"Subnet calculator",0},{L"Equal split",1},{L"VLSM planner",2},{L"Aggregate / ranges",3},{L"Compare",4}});
 add(L"Diagnostics",{{L"Visual ping",14},{L"Traceroute",15},{L"DNS lookup",16},{L"TCP connection",17},{L"HTTP & TLS",18}});
 add(L"Network information",{{L"Routes",11},{L"Neighbours",12}});
 add(L"Utilities",{{L"External IP (ipify)",13},{L"Wake-on-LAN",19},{L"MTU observations",20}});
 app->status=CreateWindowExW(0,STATUSCLASSNAMEW,L"Portable native Windows toolkit · VEU",WS_CHILD|WS_VISIBLE|SBARS_SIZEGRIP,0,0,0,0,app->window,nullptr,GetModuleHandleW(nullptr),nullptr);
 SendMessageW(app->status,WM_SETFONT,reinterpret_cast<WPARAM>(app->font),TRUE);veu::network_set_dpi(app->network,unsigned(app->dpi));SendMessageW(app->subnet,WM_DPICHANGED,MAKEWPARAM(app->dpi,app->dpi),0);select(10);SetTimer(app->window,1,100,nullptr);
}
LRESULT CALLBACK proc(HWND h,UINT msg,WPARAM wp,LPARAM lp){
 switch(msg){case WM_CREATE:app->window=h;init();return 0;
 case WM_SIZE:layout();return 0;
 case WM_DPICHANGED:{app->dpi=HIWORD(wp);auto r=reinterpret_cast<RECT*>(lp);SetWindowPos(h,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);veu::network_set_dpi(app->network,unsigned(app->dpi));SendMessageW(app->subnet,WM_DPICHANGED,wp,0);layout();return 0;}
 case WM_COMMAND:command(LOWORD(wp));return 0;
 case WM_NOTIFY:{auto n=reinterpret_cast<NMHDR*>(lp);if(n->hwndFrom==app->tree&&n->code==TVN_SELCHANGEDW){auto tv=reinterpret_cast<NMTREEVIEWW*>(lp);if(tv->itemNew.lParam>=0&&!app->loading)select(int(tv->itemNew.lParam));}return 0;}
 case WM_HELP:help();return TRUE;
 case WM_APP+11:app->selected=int(wp);sync_tree(int(wp));return 0;
 case WM_APP+10:if(!app->loading){++app->revision;app->dirty=true;title();}return 0;
 case WM_TIMER:if(app->closing&&!veu::network_busy(app->network)&&!veu::subnet_busy(app->subnet))DestroyWindow(h);return 0;
 case WM_CLOSE:if(app->saving||app->closing||!discard())return 0;app->closing=true;veu::stop_network(app->network);veu::stop_subnet(app->subnet);EnableWindow(app->tree,FALSE);EnableWindow(app->subnet,FALSE);EnableWindow(app->network,FALSE);status(L"Closing… waiting for active operations to drain.");if(!veu::network_busy(app->network)&&!veu::subnet_busy(app->subnet))DestroyWindow(h);return 0;
 case WM_DESTROY:KillTimer(h,1);PostQuitMessage(0);return 0;
 }return DefWindowProcW(h,msg,wp,lp);}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int show){
 HRESULT hr=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);INITCOMMONCONTROLSEX cc{sizeof(cc),ICC_WIN95_CLASSES|ICC_STANDARD_CLASSES};InitCommonControlsEx(&cc);App state;app=&state;
 WNDCLASSEXW wc{sizeof(wc)};wc.lpfnWndProc=proc;wc.hInstance=instance;wc.lpszClassName=L"VEU.NetTools";wc.hIcon=LoadIconW(instance,MAKEINTRESOURCEW(IDI_VEU_NETTOOLS));wc.hIconSm=wc.hIcon;wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hbrBackground=GetSysColorBrush(COLOR_BTNFACE);RegisterClassExW(&wc);
 RECT work{};SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0);int width=std::min(1220,int(work.right-work.left)),height=std::min(1000,int(work.bottom-work.top));
 HWND h=CreateWindowExW(WS_EX_CONTROLPARENT,wc.lpszClassName,L"Velocity NetTools — development preview",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,width,height,nullptr,nullptr,instance,nullptr);if(!h){if(SUCCEEDED(hr))CoUninitialize();return 1;}ShowWindow(h,show);UpdateWindow(h);
 ACCEL keys[]={{FVIRTKEY,VK_F1,Help},{FVIRTKEY|FCONTROL,'O',Open},{FVIRTKEY|FCONTROL,'S',Save}};auto acc=CreateAcceleratorTableW(keys,3);MSG msg{};
 while(GetMessageW(&msg,nullptr,0,0)>0)if(!TranslateAcceleratorW(h,acc,&msg)&&!IsDialogMessageW(h,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}
 DestroyAcceleratorTable(acc);DeleteObject(state.font);if(SUCCEEDED(hr))CoUninitialize();return int(msg.wParam);
}
