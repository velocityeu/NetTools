// Exercise the actual native frame and child panes; never probes public endpoints.
#define wWinMain embedded_app_entry
#include "../src/main.cpp"
#undef wWinMain
#include <iostream>
#include <chrono>
#include <thread>
#include <wincodec.h>
namespace {
void pump(){MSG m{};while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}}
bool drain(){auto end=std::chrono::steady_clock::now()+std::chrono::seconds(10);do{pump();if(!veu::subnet_busy(app->subnet)&&!veu::network_busy(app->network)&&veu::ui::text(GetDlgItem(app->subnet,11)).find(L"Calculating")==std::wstring::npos)return true;std::this_thread::sleep_for(std::chrono::milliseconds(10));}while(std::chrono::steady_clock::now()<end);return false;}
bool capture_png(HWND h,const wchar_t* path){RECT r{};GetWindowRect(h,&r);int w=r.right-r.left,hh=r.bottom-r.top;HDC dc=GetDC(h),mem=CreateCompatibleDC(dc);HBITMAP bitmap=CreateCompatibleBitmap(dc,w,hh);auto old=SelectObject(mem,bitmap);BOOL rendered=PrintWindow(h,mem,0);SelectObject(mem,old);DeleteDC(mem);ReleaseDC(h,dc);
 IWICImagingFactory* factory=nullptr;IWICBitmap* source=nullptr;IWICStream* stream=nullptr;IWICBitmapEncoder* encoder=nullptr;IWICBitmapFrameEncode* frame=nullptr;bool ok=false;
 if(rendered&&SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)))&&SUCCEEDED(factory->CreateBitmapFromHBITMAP(bitmap,nullptr,WICBitmapIgnoreAlpha,&source))&&SUCCEEDED(factory->CreateStream(&stream))&&SUCCEEDED(stream->InitializeFromFilename(path,GENERIC_WRITE))&&SUCCEEDED(factory->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder))&&SUCCEEDED(encoder->Initialize(stream,WICBitmapEncoderNoCache))&&SUCCEEDED(encoder->CreateNewFrame(&frame,nullptr))&&SUCCEEDED(frame->Initialize(nullptr))&&SUCCEEDED(frame->WriteSource(source,nullptr))&&SUCCEEDED(frame->Commit())&&SUCCEEDED(encoder->Commit()))ok=true;
 if(frame)frame->Release();if(encoder)encoder->Release();if(stream)stream->Release();if(source)source->Release();if(factory)factory->Release();DeleteObject(bitmap);return ok;}
}
int wmain(int argc,wchar_t** argv){
 CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);INITCOMMONCONTROLSEX cc{sizeof(cc),ICC_WIN95_CLASSES|ICC_STANDARD_CLASSES};InitCommonControlsEx(&cc);App state;app=&state;auto inst=GetModuleHandleW(nullptr);WNDCLASSW wc{};wc.hInstance=inst;wc.lpszClassName=L"VEU.FrameSmoke";wc.lpfnWndProc=proc;wc.hbrBackground=GetSysColorBrush(COLOR_BTNFACE);wc.hIcon=LoadIconW(inst,MAKEINTRESOURCEW(IDI_VEU_NETTOOLS));RegisterClassW(&wc);
 HWND h=CreateWindowExW(WS_EX_CONTROLPARENT,wc.lpszClassName,L"Velocity NetTools — development preview",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,0,0,1220,1000,nullptr,nullptr,inst,nullptr);
 int failures=0;auto check=[&](bool b,const char* label){if(!b){std::cerr<<"FAIL "<<label<<"\n";++failures;}};
 check(h&&state.subnet&&state.network,"native frame and panes created");check(!veu::network_busy(state.network),"no startup network operations");select(0);check(drain(),"initial calculator settles");
 auto fields=veu::subnet_plan_fields(state.subnet);fields["subnet.0.0"]="2001:db8::1";fields["subnet.0.1"]="64";veu::load_subnet_plan_fields(state.subnet,fields);SendMessageW(state.subnet,WM_COMMAND,100,0);check(drain(),"IPv6 result settles");check(ListView_GetItemCount(GetDlgItem(state.subnet,12))>0,"IPv6 result table populated");
 fields["subnet.0.0"]="192.168.10.42";fields["subnet.0.1"]="26";veu::load_subnet_plan_fields(state.subnet,fields);SendMessageW(state.subnet,WM_COMMAND,100,0);check(drain(),"IPv4 result settles");wchar_t cell[200]{};ListView_GetItemText(GetDlgItem(state.subnet,12),1,1,cell,200);check(std::wstring(cell)==L"192.168.10.0/26","frame uses exact reviewed engine");
 auto netFields=veu::network_plan_fields(state.network);auto saved=veu::storage::Plan{};saved.active_tool=0;saved.fields=fields;saved.fields.insert(netFields.begin(),netFields.end());check(veu::storage::decode(veu::storage::encode(saved)).fields==saved.fields,"actual pane state roundtrips through plan");
 app->dirty=false;auto revision=app->revision;file_work([&]{PostMessageW(h,WM_APP+10,0,0);std::this_thread::sleep_for(std::chrono::milliseconds(100));});pump();check(app->dirty&&app->revision>revision,"file work pumps edit notifications without losing newer dirty state");app->dirty=false;SendMessageW(h,WM_COMMAND,StopAll,0);check(drain(),"Stop All drains native jobs");status(L"Ready · VEU · Windows x64");
 if(argc>2 && std::wstring_view(argv[2])==L"--adapters"){select(10);SendMessageW(state.network,WM_COMMAND,100,0);check(drain(),"local addresses settle for grid review");SendMessageW(state.network,WM_TIMER,1,0);}
 if(argc>1){ShowWindow(h,SW_SHOWNORMAL);UpdateWindow(h);pump();check(capture_png(h,argv[1]),"native screenshot captured");}
 DestroyWindow(h);DeleteObject(state.font);CoUninitialize();std::cout<<"Frame smoke "<<(failures?"FAILED":"passed")<<"\n";return failures?1:0;
}
